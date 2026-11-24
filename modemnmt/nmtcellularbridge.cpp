///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov                                             //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include "nmtcellularbridge.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include "dsp/dv/dvdtmf.h"
#include "echosuppressor.h"
#include "nmtvoice.h"

namespace modemnmt
{

// The router's view of one phone.
class NmtCellularBridge::Leg : public modemcellular::MobileLeg
{
public:
    Leg(NmtCellularBridge& bridge, const NmtSubscriber& subscriber) :
        m_bridge(bridge),
        m_number(NmtCellularBridge::numberOf(subscriber)),
        m_subscriber(subscriber),
        m_callref(0)
    {
        m_dtmf.setLevel(kDtmfLevel);
    }

    const std::string& number() const { return m_number; }
    const NmtSubscriber& subscriber() const { return m_subscriber; }
    modemcellular::CallRef callref() const { return m_callref; }
    void setCallref(modemcellular::CallRef callref) { m_callref = callref; }

    // The digits the phone sends in the MFT state go to the far end as push-button tones (ITU-T
    // Q.23: 100 ms of tone, 50 ms of pause).
    void digit(char digit) { m_dtmf.queue(digit, 100, 50); }
    // The speech of the phone towards the far end: its echo of the far end suppressed, then the
    // tones of the digits added.
    const float* fromPhone(modemcellular::CallRef callref, const float* samples, int count)
    {
        echoFor(callref);
        m_scratch.assign(samples, samples + count);
        m_echo.processRx(m_scratch.data(), count);
        if (m_dtmf.active())
        {
            m_tones.resize(static_cast<size_t>(count));
            const int made = m_dtmf.generate(m_tones.data(), count);
            for (int i = 0; i < made; i++) {
                m_scratch[static_cast<size_t>(i)] += m_tones[static_cast<size_t>(i)];
            }
        }
        return m_scratch.data();
    }
    // The far end towards the phone: the reference of the echo suppressor.
    void toPhone(modemcellular::CallRef callref, const float* samples, int count)
    {
        echoFor(callref);
        m_echo.processTx(samples, count);
    }
    void clearTones() { m_dtmf.reset(); }

    // These come from the router, on any thread, and are only queued.
    void setup(modemcellular::CallRef callref, const std::string& callerId, modemcellular::NumberType callerType,
               const std::string&) override
    {
        Event event;

        event.type = Event::Type::Setup;
        event.number = m_number;
        event.callref = callref;
        event.callerId = callerId;
        event.callerType = callerType;
        m_bridge.queue(std::move(event));
    }

    void proceeding(modemcellular::CallRef) override {}     // the phone has no signal for it
    void alerting(modemcellular::CallRef) override {}       // the ringback tone comes as audio
    void answer(modemcellular::CallRef) override {}         // so does the far end

    void release(modemcellular::CallRef callref, modemcellular::Cause cause) override
    {
        Event event;

        event.type = Event::Type::Release;
        event.number = m_number;
        event.callref = callref;
        event.cause = cause;
        m_bridge.queue(std::move(event));
    }

private:
    // A new call starts with a new echo suppressor (its delay is measured again). Called from the
    // audio handlers only, under the station lock.
    void echoFor(modemcellular::CallRef callref)
    {
        if (callref != m_echoRef)
        {
            m_echo = modemcellular::EchoSuppressor(kCallRate);
            m_echoRef = callref;
        }
    }

    NmtCellularBridge& m_bridge;
    std::string m_number;
    NmtSubscriber m_subscriber;
    // set from the router (any thread), read by the audio handlers under the station lock
    std::atomic<modemcellular::CallRef> m_callref;
    // 0.2 peak for each of the two tones (8 kHz, the call's rate)
    static constexpr float kDtmfLevel = 0.2f;
    DVDTMF::Generator m_dtmf;               // SDRangel's push-button tones
    std::vector<float> m_scratch;
    std::vector<float> m_tones;
    // osmocom-analog's echo suppressor of the call, on as there by default
    modemcellular::EchoSuppressor m_echo;
    modemcellular::CallRef m_echoRef = 0;
};

std::string NmtCellularBridge::numberOf(const NmtSubscriber& subscriber)
{
    return std::string(1, subscriber.country) + subscriber.number;
}

NmtCellularBridge::NmtCellularBridge(NmtSystem& nmt, modemcellular::CellularSystem& cellular) :
    m_nmt(nmt),
    m_cellular(cellular)
{
    // A phone that roamed in can be called.
    m_nmt.setRegistrationListener([this](const NmtSubscriber& subscriber) {
        Leg& leg = legFor(subscriber);
        m_cellular.registerSubscriber(leg.number(), leg);
    });

    // A call from a phone: to the router, which answers with a tone, music, or another phone.
    m_nmt.setCallHandler([this](const NmtSubscriber& subscriber, const std::string& dialled) {
        Leg& leg = legFor(subscriber);

        // a call that is still in the router belongs to a transaction that ended unnoticed
        if (leg.callref())
        {
            m_cellular.release(leg.callref(), modemcellular::Cause::Normal);
            leg.setCallref(0);
        }

        // A phone number (country digit and six digits) that is not registered here is paged all
        // the same, as osmocom-analog's NMT does: a phone that kept its traffic area makes no
        // roaming update after the network starts. If it does not answer, the caller hears the
        // tone of the cause. Other numbers go to the fallback.
        modemcellular::MobileLeg *called = nullptr;
        if (!m_cellular.isRegistered(dialled) && dialled != leg.number()) {
            called = legOf(dialled);
        }
        const modemcellular::CallRouter::SetupResult result = m_cellular.setup(leg, leg.number(), dialled, called);

        if (!result.ok()) {
            return false;
        }
        leg.clearTones();
        leg.setCallref(result.callref);
        return true;
    });

    // phones that roamed in before the bridge was there can be called as well
    m_nmt.forEachRegistered([this](const NmtSubscriber& subscriber) {
        Leg& leg = legFor(subscriber);
        m_cellular.registerSubscriber(leg.number(), leg);
    });

    m_nmt.setClearHandler([this](const NmtSubscriber& subscriber) {
        Leg& leg = legFor(subscriber);
        const modemcellular::CallRef callref = leg.callref();

        leg.setCallref(0);
        if (callref) {
            m_cellular.release(callref, modemcellular::Cause::Normal);
        }
    });

    // progress of a call to a phone
    m_nmt.setCallToPhoneHandlers(
        [this](const NmtSubscriber& subscriber) {
            const modemcellular::CallRef callref = legFor(subscriber).callref();
            if (callref) {
                m_cellular.alerting(callref);
            }
        },
        [this](const NmtSubscriber& subscriber) {
            const modemcellular::CallRef callref = legFor(subscriber).callref();
            if (callref) {
                m_cellular.answer(callref);
            }
        },
        [this](const NmtSubscriber& subscriber, modemcellular::Cause cause) {
            Leg& leg = legFor(subscriber);
            const modemcellular::CallRef callref = leg.callref();

            leg.setCallref(0);
            if (callref) {
                m_cellular.release(callref, cause);
            }
        });

    // the MFT converter: the digits of the phone go to the far end as push-button tones
    m_nmt.setMftHandler([this](const NmtSubscriber& subscriber, char digit) {
        Leg& leg = legFor(subscriber);

        if (leg.callref()) {
            leg.digit(digit);
        }
    });

    m_nmt.setAudioHandlers(
        [this](const NmtSubscriber& subscriber, const float* samples, int count) {
            Leg& leg = legFor(subscriber);
            const modemcellular::CallRef callref = leg.callref();
            if (callref) {
                m_cellular.audioFromMobile(callref, leg.fromPhone(callref, samples, count), count);
            }
        },
        [this](const NmtSubscriber& subscriber, float* samples, int count) {
            Leg& leg = legFor(subscriber);
            const modemcellular::CallRef callref = leg.callref();
            if (callref)
            {
                m_cellular.audioToMobile(callref, samples, count);
                leg.toPhone(callref, samples, count);
            }
        });
}

NmtCellularBridge::~NmtCellularBridge()
{
    // the phones in a call are released: nobody is left to carry their audio
    for (auto& entry : m_legs)
    {
        Leg& leg = *entry.second;

        if (leg.callref())
        {
            leg.setCallref(0);
            m_nmt.releaseCall(leg.subscriber().country, leg.subscriber().number);
        }
    }

    m_nmt.setRegistrationListener(nullptr);
    m_nmt.setCallHandler(nullptr);
    m_nmt.setClearHandler(nullptr);
    m_nmt.setCallToPhoneHandlers(nullptr, nullptr, nullptr);
    m_nmt.setAudioHandlers(nullptr, nullptr);
    m_nmt.setMftHandler(nullptr);

    // waits for notifications that are being delivered to a leg
    for (auto& entry : m_legs) {
        m_cellular.detach(*entry.second);
    }
}

NmtCellularBridge::Leg& NmtCellularBridge::legFor(const NmtSubscriber& subscriber)
{
    const std::string number = numberOf(subscriber);
    auto it = m_legs.find(number);

    if (it == m_legs.end()) {
        it = m_legs.emplace(number, std::make_unique<Leg>(*this, subscriber)).first;
    }

    return *it->second;
}

modemcellular::MobileLeg* NmtCellularBridge::legOf(const std::string& number)
{
    if (number.size() != 7 || std::any_of(number.begin(), number.end(), [](char c) { return c < '0' || c > '9'; })) {
        return nullptr;
    }

    NmtSubscriber subscriber;
    subscriber.country = number[0];
    subscriber.number = number.substr(1);

    return &legFor(subscriber);
}

void NmtCellularBridge::queue(Event&& event)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_events.push_back(std::move(event));
}

void NmtCellularBridge::run()
{
    // timers of the router fire here, and so do the notifications they cause
    m_cellular.run();

    for (;;)
    {
        Event event;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_events.empty()) {
                return;
            }
            event = std::move(m_events.front());
            m_events.pop_front();
        }

        const auto it = m_legs.find(event.number);
        if (it == m_legs.end()) {
            continue;
        }
        Leg& leg = *it->second;

        if (event.type == Event::Type::Setup)
        {
            // a call for the phone: it is paged; a phone that has a call is busy, and the call
            // is rejected at once if there is no channel for it
            modemcellular::Cause cause = modemcellular::Cause::Busy;

            if (leg.callref()) {
                m_cellular.release(event.callref, modemcellular::Cause::Busy);
            }
            else if (!m_nmt.startCall(leg.subscriber().country, leg.subscriber().number, event.callerId,
                                      event.callerType, &cause)) {
                m_cellular.release(event.callref, cause);
            }
            else {
                leg.setCallref(event.callref);
            }
        }
        else if (event.callref == leg.callref())
        {
            // the far end ended the call of the phone
            leg.setCallref(0);
            m_nmt.releaseCall(leg.subscriber().country, leg.subscriber().number);
        }
    }
}

} // namespace modemnmt
