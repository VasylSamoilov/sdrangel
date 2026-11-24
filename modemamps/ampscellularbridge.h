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

#ifndef MODEMAMPS_AMPSCELLULARBRIDGE_H_
#define MODEMAMPS_AMPSCELLULARBRIDGE_H_

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "ampssystem.h"
#include "calltypes.h"
#include "cellularsystem.h"
#include "mobileleg.h"
#include "export.h"

// Connects an AMPS base station to the call router of modemcellular, as modemnmt's
// NmtCellularBridge does for NMT: calls from the phones go to the router (registered numbers, music,
// echo), its tones and audio come back, and a call that the far end releases releases the phone.
// Registered phones are known to the router under their number, the 10 digits of the MIN; a phone
// that powers down is removed. The router's call to a phone pages it (AmpsSystem::startCall; a
// phone that is not registered is paged as well, legOf() gives its leg), its ringing and answer go
// back to the router.
//
// Threads as NmtCellularBridge: the AmpsSystem is used by one thread at a time (render, receive and
// run of its base station, run() of the bridge, its construction and destruction under one lock);
// the router's notifications to the legs are queued and acted on in the next run().
namespace modemamps
{

class MODEMAMPS_API AmpsCellularBridge
{
public:
    // Both have to outlive the bridge.
    AmpsCellularBridge(AmpsSystem& amps, modemcellular::CellularSystem& cellular);
    ~AmpsCellularBridge();
    AmpsCellularBridge(const AmpsCellularBridge&) = delete;
    AmpsCellularBridge& operator=(const AmpsCellularBridge&) = delete;

    // Call once per audio block, after the base station's process(): runs the timers of the router
    // and acts on what it has told the phones since the last call.
    void run();
    // The leg of the phone with this number (10 digits), registered or not; null if it is not such
    // a number. From the thread of the AmpsSystem.
    modemcellular::MobileLeg* legOf(const std::string& number);

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
    };

    Leg& legFor(const AmpsSubscriber& subscriber);
    void queue(Event&& event);

    AmpsSystem& m_amps;
    modemcellular::CellularSystem& m_cellular;
    std::map<std::string, std::unique_ptr<Leg>> m_legs;
    std::mutex m_mutex;                 // the queue only
    std::deque<Event> m_events;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSCELLULARBRIDGE_H_
