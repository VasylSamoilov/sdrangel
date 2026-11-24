///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

#include "ampscellularbridge.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include "echosuppressor.h"

namespace modemamps
{

// The router's view of one phone.
class AmpsCellularBridge::Leg : public modemcellular::MobileLeg
{
public:
    Leg(AmpsCellularBridge& bridge, const AmpsSubscriber& subscriber) :
        m_bridge(bridge),
        m_number(subscriber.number()),
        m_subscriber(subscriber),
        m_callref(0)
    {
    }

    const std::string& number() const { return m_number; }
    const AmpsSubscriber& subscriber() const { return m_subscriber; }
    modemcellular::CallRef callref() const { return m_callref; }
    void setCallref(modemcellular::CallRef callref) { m_callref = callref; }

    // The speech of the phone towards the far end, its echo of the far end suppressed.
    const float* fromPhone(modemcellular::CallRef callref, const float* samples, int count)
    {
        echoFor(callref);
        m_scratch.assign(samples, samples + count);
        m_echo.processRx(m_scratch.data(), count);
        return m_scratch.data();
    }

    // The far end towards the phone: the reference of the echo suppressor.
    void toPhone(modemcellular::CallRef callref, const float* samples, int count)
    {
        echoFor(callref);
        m_echo.processTx(samples, count);
    }

    // These come from the router, on any thread, and are only queued.
    void setup(modemcellular::CallRef callref, const std::string&, modemcellular::NumberType, const std::string&) override
    {
        Event event;
        event.type = Event::Type::Setup;
        event.number = m_number;
        event.callref = callref;
        m_bridge.queue(std::move(event));
    }
    void proceeding(modemcellular::CallRef) override {}
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
    // A new call starts with a new echo suppressor (its delay is measured again).
    void echoFor(modemcellular::CallRef callref)
    {
        if (callref != m_echoRef)
        {
            m_echo = modemcellular::EchoSuppressor(modemcellular::kAudioSampleRate);
            m_echoRef = callref;
        }
    }

    AmpsCellularBridge& m_bridge;
    std::string m_number;
    AmpsSubscriber m_subscriber;
    std::atomic<modemcellular::CallRef> m_callref;
    std::vector<float> m_scratch;
    // osmocom-analog's echo suppressor of the call, on as there by default
    modemcellular::EchoSuppressor m_echo;
    modemcellular::CallRef m_echoRef = 0;
};

AmpsCellularBridge::AmpsCellularBridge(AmpsSystem& amps, modemcellular::CellularSystem& cellular) :
    m_amps(amps),
    m_cellular(cellular)
{
    m_amps.setRegistrationHandler([this](const AmpsSubscriber& subscriber, bool powerDown) {
        Leg& leg = legFor(subscriber);
        if (powerDown) {
            m_cellular.unregisterSubscriber(leg.number(), leg);
        } else {
            m_cellular.registerSubscriber(leg.number(), leg);
        }
    });
    m_amps.setCallHandler([this](const AmpsSubscriber& subscriber, const std::string& dialled) {
        Leg& leg = legFor(subscriber);
        // a call that is still in the router belongs to a transaction that ended unnoticed
        if (leg.callref())
        {
            m_cellular.release(leg.callref(), modemcellular::Cause::Normal);
            leg.setCallref(0);
        }
        // A phone's number (10 digits) that is not registered here is paged all the same, as
        // osmocom-analog does; other numbers go to the fallback.
        modemcellular::MobileLeg *called = nullptr;
        if (!m_cellular.isRegistered(dialled) && dialled != leg.number()) {
            called = legOf(dialled);
        }
        const modemcellular::CallRouter::SetupResult result = m_cellular.setup(leg, leg.number(), dialled, called);
        if (!result.ok()) {
            return false;
        }
        leg.setCallref(result.callref);
        return true;
    });
    m_amps.setClearHandler([this](const AmpsSubscriber& subscriber) {
        Leg& leg = legFor(subscriber);
        const modemcellular::CallRef callref = leg.callref();
        leg.setCallref(0);
        if (callref) {
            m_cellular.release(callref, modemcellular::Cause::Normal);
        }
    });
    // progress of a call to a phone
    m_amps.setCallToPhoneHandlers(
        [this](const AmpsSubscriber& subscriber) {
            const modemcellular::CallRef callref = legFor(subscriber).callref();
            if (callref) {
                m_cellular.alerting(callref);
            }
        },
        [this](const AmpsSubscriber& subscriber) {
            const modemcellular::CallRef callref = legFor(subscriber).callref();
            if (callref) {
                m_cellular.answer(callref);
            }
        },
        [this](const AmpsSubscriber& subscriber, modemcellular::Cause cause) {
            Leg& leg = legFor(subscriber);
            const modemcellular::CallRef callref = leg.callref();
            leg.setCallref(0);
            if (callref) {
                m_cellular.release(callref, cause);
            }
        });
    m_amps.setAudioHandlers(
        [this](const AmpsSubscriber& subscriber, const float* samples, int count) {
            Leg& leg = legFor(subscriber);
            const modemcellular::CallRef callref = leg.callref();
            if (callref) {
                m_cellular.audioFromMobile(callref, leg.fromPhone(callref, samples, count), count);
            }
        },
        [this](const AmpsSubscriber& subscriber, float* samples, int count) {
            Leg& leg = legFor(subscriber);
            const modemcellular::CallRef callref = leg.callref();
            if (callref)
            {
                m_cellular.audioToMobile(callref, samples, count);
                leg.toPhone(callref, samples, count);
            }
        });
}

AmpsCellularBridge::~AmpsCellularBridge()
{
    // the phones in a call are released: nobody is left to carry their audio
    for (auto& entry : m_legs)
    {
        Leg& leg = *entry.second;
        if (leg.callref())
        {
            leg.setCallref(0);
            m_amps.releaseCall(leg.number());
        }
    }
    m_amps.setRegistrationHandler(nullptr);
    m_amps.setCallHandler(nullptr);
    m_amps.setClearHandler(nullptr);
    m_amps.setCallToPhoneHandlers(nullptr, nullptr, nullptr);
    m_amps.setAudioHandlers(nullptr, nullptr);
    // waits for notifications that are being delivered to a leg
    for (auto& entry : m_legs) {
        m_cellular.detach(*entry.second);
    }
}

AmpsCellularBridge::Leg& AmpsCellularBridge::legFor(const AmpsSubscriber& subscriber)
{
    const std::string number = subscriber.number();
    auto it = m_legs.find(number);

    if (it == m_legs.end()) {
        it = m_legs.emplace(number, std::make_unique<Leg>(*this, subscriber)).first;
    }
    return *it->second;
}

modemcellular::MobileLeg* AmpsCellularBridge::legOf(const std::string& number)
{
    if (number.size() != 10 || std::any_of(number.begin(), number.end(), [](char c) { return c < '0' || c > '9'; })) {
        return nullptr;
    }
    AmpsSubscriber subscriber;
    subscriber.min2 = ReccAssembler::min2Of(number.substr(0, 3));
    subscriber.min1 = ReccAssembler::min1Of(number.substr(3));
    return &legFor(subscriber);
}

void AmpsCellularBridge::queue(Event&& event)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(std::move(event));
}

void AmpsCellularBridge::run()
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
            // a call for the phone: it is paged; a phone that has a call is busy, and the call is
            // rejected at once if it cannot be paged
            modemcellular::Cause cause = modemcellular::Cause::Busy;
            if (leg.callref()) {
                m_cellular.release(event.callref, modemcellular::Cause::Busy);
            } else if (!m_amps.startCall(leg.number(), &cause)) {
                m_cellular.release(event.callref, cause);
            } else {
                leg.setCallref(event.callref);
            }
        }
        else if (event.callref == leg.callref())
        {
            // the far end ended the call of the phone
            leg.setCallref(0);
            m_amps.releaseCall(leg.number());
        }
    }
}

} // namespace modemamps
