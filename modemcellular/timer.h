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

#ifndef MODEMCELLULAR_TIMER_H_
#define MODEMCELLULAR_TIMER_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "clock.h"
#include "export.h"

namespace modemcellular
{

class Timer;

// Runs one-shot timers against a Clock. run() is called by the owner at block
// boundaries; timers therefore fire with the resolution of the block size.
//
// Not thread safe: the owner serialises start/stop/run and the callbacks.
// The service and the clock must outlive every Timer created on them.
class MODEMCELLULAR_API TimerService
{
public:
    explicit TimerService(const Clock& clock);
    TimerService(const TimerService&) = delete;
    TimerService& operator=(const TimerService&) = delete;

    // Fires every timer that is due. Due timers fire in order of expiry time,
    // equal expiry times in order of start. A callback may start or stop any
    // timer, including its own, and may destroy any timer, including its own.
    // A timer started while run() is active fires in a later run() call, so a
    // callback that restarts itself with zero delay cannot loop.
    void run();

    bool hasPending() const { return !m_entries.empty(); }

private:
    friend class Timer;

    struct Entry
    {
        Timer* timer;
        double expiry;
        uint64_t order;     // start order, breaks ties between equal expiry times
    };

    void arm(Timer* timer, double delaySeconds);
    void disarm(Timer* timer);

    const Clock& m_clock;
    std::vector<Entry> m_entries;
    uint64_t m_nextOrder;
};

// One-shot timer. The callback runs from TimerService::run().
class MODEMCELLULAR_API Timer
{
public:
    Timer(TimerService& service, std::function<void()> onExpire);
    ~Timer();
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // Starts the timer. A pending expiry is replaced. Negative delays count as zero.
    void start(double delaySeconds);
    // Stops the timer. Does nothing if it is not pending.
    void stop();
    bool active() const { return m_active; }

private:
    friend class TimerService;

    TimerService& m_service;
    std::function<void()> m_onExpire;
    bool m_active;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_TIMER_H_
