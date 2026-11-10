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
    }

    const std::string& number() const { return m_number; }
    const NmtSubscriber& subscriber() const { return m_subscriber; }
    modemcellular::CallRef callref() const { return m_callref; }
    void setCallref(modemcellular::CallRef callref) { m_callref = callref; }

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
    NmtCellularBridge& m_bridge;
    std::string m_number;
    NmtSubscriber m_subscriber;
    // set from the call, clear and release paths, read by the audio handlers: the audio threads
    // of a station are not the same
    std::atomic<modemcellular::CallRef> m_callref;
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

        const modemcellular::CallRouter::SetupResult result = m_cellular.setup(leg, leg.number(), dialled);

        if (!result.ok()) {
            return false;
        }
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

    m_nmt.setAudioHandlers(
        [this](const NmtSubscriber& subscriber, const float* samples, int count) {
            const modemcellular::CallRef callref = legFor(subscriber).callref();
            if (callref) {
                m_cellular.audioFromMobile(callref, samples, count);
            }
        },
        [this](const NmtSubscriber& subscriber, float* samples, int count) {
            const modemcellular::CallRef callref = legFor(subscriber).callref();
            if (callref) {
                m_cellular.audioToMobile(callref, samples, count);
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
            // a call for the phone: it is paged; a phone that has a call is busy
            if (!m_nmt.canCall()) {
                m_cellular.release(event.callref, modemcellular::Cause::NoChannel);
            }
            else if (leg.callref() ||
                !m_nmt.startCall(leg.subscriber().country, leg.subscriber().number, event.callerId, event.callerType))
            {
                m_cellular.release(event.callref, modemcellular::Cause::Busy);
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
