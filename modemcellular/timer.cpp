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

#include <algorithm>
#include <utility>

#include "timer.h"

namespace modemcellular
{

TimerService::TimerService(const Clock& clock) :
    m_clock(clock),
    m_nextOrder(0)
{
}

void TimerService::arm(Timer* timer, double delaySeconds)
{
    disarm(timer);
    const double delay = delaySeconds > 0.0 ? delaySeconds : 0.0;
    m_entries.push_back(Entry{timer, m_clock.now() + delay, m_nextOrder++});
    timer->m_active = true;
}

void TimerService::disarm(Timer* timer)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [timer](const Entry& e) { return e.timer == timer; });

    if (it != m_entries.end()) {
        m_entries.erase(it);
    }

    timer->m_active = false;
}

void TimerService::run()
{
    const double now = m_clock.now();
    const uint64_t firstNewOrder = m_nextOrder;   // timers started from here on wait for the next run

    for (;;)
    {
        // The list is searched again after every callback because a callback
        // may have changed it in any way.
        const Entry* best = nullptr;

        for (const Entry& e : m_entries)
        {
            if ((e.order >= firstNewOrder) || (e.expiry > now)) {
                continue;
            }

            if (!best || (e.expiry < best->expiry)
                || ((e.expiry == best->expiry) && (e.order < best->order))) {
                best = &e;
            }
        }

        if (!best) {
            break;
        }

        Timer* timer = best->timer;
        disarm(timer);
        // Copy: the callback may destroy the timer that owns it.
        const std::function<void()> callback = timer->m_onExpire;

        if (callback) {
            callback();
        }
    }
}

Timer::Timer(TimerService& service, std::function<void()> onExpire) :
    m_service(service),
    m_onExpire(std::move(onExpire)),
    m_active(false)
{
}

Timer::~Timer()
{
    m_service.disarm(this);
}

void Timer::start(double delaySeconds)
{
    m_service.arm(this, delaySeconds);
}

void Timer::stop()
{
    m_service.disarm(this);
}

} // namespace modemcellular
