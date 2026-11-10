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

#ifndef MODEMNMT_NMTCELLULARBRIDGE_H_
#define MODEMNMT_NMTCELLULARBRIDGE_H_

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "calltypes.h"
#include "cellularsystem.h"
#include "export.h"
#include "mobileleg.h"
#include "nmtsystem.h"

namespace modemnmt
{

// Connects an NMT base station to the call router of modemcellular: calls from the phones go to
// the router (registered numbers, music, echo, later SIP), the router's progress tones and
// audio go back, and a call that the far end releases releases the phone. Phones that roam in
// are registered with the router under their number: the country digit and the six digits.
// Calls to a phone are not supported yet: the router is told that the phone cannot be reached.
//
// The bridge takes over the call, clear, audio and registration handlers of the NmtSystem.
//
// Threads: the NmtSystem is used by one thread at a time (the caller serialises render, receive
// and run of its channels, run() of the bridge, and the construction and destruction of the
// bridge, all under one lock). The CellularSystem may be used from other threads; its
// notifications to the legs of the bridge are only queued, from whatever thread calls into the
// router, and are acted on in the next run() (one audio block later), so they never enter the
// NmtSystem from the thread of the router. The plugin must not hold its lock while calling into
// the router from a thread that is not an audio thread of the station.
//
// The bridge must be destroyed before the NmtSystem and the CellularSystem. Phones that are in a
// call when it is destroyed are released. Phones that roamed in earlier are registered by the
// constructor. The router endpoints have to be cheap and must not block: they run under the
// router's lock, in the audio threads.
class MODEMNMT_API NmtCellularBridge
{
public:
    // Both have to outlive the bridge.
    NmtCellularBridge(NmtSystem& nmt, modemcellular::CellularSystem& cellular);
    ~NmtCellularBridge();
    NmtCellularBridge(const NmtCellularBridge&) = delete;
    NmtCellularBridge& operator=(const NmtCellularBridge&) = delete;

    // Call once per audio block, after NmtSystem::run(): runs the timers of the router and acts
    // on what the router has told the phones since the last call.
    void run();

    // The number a phone has in the router: country digit and number.
    static std::string numberOf(const NmtSubscriber& subscriber);

private:
    class Leg;
    struct Event
    {
        enum class Type
        {
            Setup,
            Release
        };

        Type type = Type::Release;
        std::string number;
        modemcellular::CallRef callref = 0;
        modemcellular::Cause cause = modemcellular::Cause::Normal;
        std::string callerId;                       // Setup
        modemcellular::NumberType callerType = modemcellular::NumberType::NotAvailable;
    };

    Leg& legFor(const NmtSubscriber& subscriber);
    void queue(Event&& event);

    NmtSystem& m_nmt;
    modemcellular::CellularSystem& m_cellular;
    std::map<std::string, std::unique_ptr<Leg>> m_legs;
    std::mutex m_mutex;                 // the queue only
    std::deque<Event> m_events;
};

} // namespace modemnmt

#endif // MODEMNMT_NMTCELLULARBRIDGE_H_
