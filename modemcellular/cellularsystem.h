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

#ifndef MODEMCELLULAR_CELLULARSYSTEM_H_
#define MODEMCELLULAR_CELLULARSYSTEM_H_

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "callrouter.h"
#include "clock.h"
#include "export.h"
#include "timer.h"

namespace modemcellular
{

// The state shared by all channels of one base station: timers, registered subscribers
// and calls. Every function may be called from any thread; one mutex serialises them.
//
// The mutex is never held while a MobileLeg or the subscriber listener is called. Calls to
// them are queued and made, in order, by the thread that is inside the system at the time,
// after it has released the mutex. Consequences:
//  - a callback may call back into the system;
//  - a callback can arrive on any thread that calls into the system (including run()), and
//    after the call that caused it has returned, so a leg must hand the work over to its
//    own thread or lock;
//  - detach() returns only when no callback to that leg is running on another thread, so the
//    leg can be destroyed afterwards. Do not hold a lock that a callback of that leg takes
//    while calling detach().
//
// The clock must be readable from any thread (SampleClock and SteadyClock are). run() has
// to be called regularly, for example once per audio block of one channel or from a
// timer; it fires the timers that are due.
class MODEMCELLULAR_API CellularSystem
{
public:
    // Called when a subscriber number becomes reachable (registered = true) or is removed.
    using SubscriberListener = std::function<void(const std::string& number, bool registered)>;

    // One system is one base station of one standard; toneSet is the tone region of that
    // standard (NMT: Denmark, AMPS: American, TACS: Uk, JTACS: Japan).
    CellularSystem(const Clock& clock, ToneSet toneSet);
    ~CellularSystem();
    CellularSystem(const CellularSystem&) = delete;
    CellularSystem& operator=(const CellularSystem&) = delete;

    // Configuration (see CallRouter).
    void setFallback(CallRouter::Fallback fallback);
    void setFallbackFactory(CallRouter::EndpointFactory factory);
    void setAnswerDelay(double seconds);
    void setToneTimeout(double seconds);
    void setSubscriberListener(SubscriberListener listener);

    // Subscriber hooks for the protocol layers: call when a mobile registers, de-registers
    // or times out. A number registered again moves to the new leg.
    bool registerSubscriber(const std::string& number, MobileLeg& leg);
    void unregisterSubscriber(const std::string& number, MobileLeg& leg);
    bool isRegistered(const std::string& number) const;
    // For a channel that goes away: unregisters its numbers and ends its calls.
    void detach(MobileLeg& leg);

    // Calls, as in CallRouter.
    CallRouter::SetupResult setup(MobileLeg& origin, const std::string& callerId, const std::string& dialed);
    void alerting(CallRef callref);
    void answer(CallRef callref);
    void release(CallRef callref, Cause cause);
    void audioFromMobile(CallRef callref, const float* samples, int count);
    void audioToMobile(CallRef callref, float* samples, int count);

    // Fires due timers, then delivers queued notifications.
    void run();

    size_t callCount() const;

private:
    struct Event
    {
        bool subscriber = false;
        CallRouter::Notification notification;
        std::string number;
        bool registered = false;
    };

    void flush();

    mutable std::mutex m_mutex;
    std::condition_variable m_idle;
    TimerService m_timers;
    CallRouter m_router;
    SubscriberListener m_listener;
    std::deque<Event> m_events;
    bool m_flushing;
    MobileLeg* m_delivering;                // leg whose callback is running
    std::thread::id m_deliveringThread;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CELLULARSYSTEM_H_
