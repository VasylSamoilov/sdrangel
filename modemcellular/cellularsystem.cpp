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

#include "cellularsystem.h"

namespace modemcellular
{

CellularSystem::CellularSystem(const Clock& clock, ToneSet toneSet) :
    m_timers(clock),
    m_router(m_timers, toneSet),
    m_flushing(false),
    m_delivering(nullptr)
{
    // Runs inside router calls, which are always made with the mutex held.
    m_router.setNotificationHandler([this](CallRouter::Notification&& n) {
        Event ev;
        ev.notification = std::move(n);
        m_events.push_back(std::move(ev));
    });
}

CellularSystem::~CellularSystem() = default;

void CellularSystem::setFallback(CallRouter::Fallback fallback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_router.setFallback(fallback);
}

void CellularSystem::setFallbackFactory(CallRouter::EndpointFactory factory)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_router.setFallbackFactory(std::move(factory));
}

void CellularSystem::setAnswerDelay(double seconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_router.setAnswerDelay(seconds);
}

void CellularSystem::setToneTimeout(double seconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_router.setToneTimeout(seconds);
}

void CellularSystem::setSubscriberListener(SubscriberListener listener)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listener = std::move(listener);
}

bool CellularSystem::registerSubscriber(const std::string& number, MobileLeg& leg)
{
    bool ok;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ok = m_router.registerSubscriber(number, leg);

        if (ok)
        {
            Event ev;
            ev.subscriber = true;
            ev.number = number;
            ev.registered = true;
            m_events.push_back(std::move(ev));
        }
    }

    flush();
    return ok;
}

void CellularSystem::unregisterSubscriber(const std::string& number, MobileLeg& leg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_router.unregisterSubscriber(number, leg))
        {
            Event ev;
            ev.subscriber = true;
            ev.number = number;
            ev.registered = false;
            m_events.push_back(std::move(ev));
        }
    }

    flush();
}

bool CellularSystem::isRegistered(const std::string& number) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_router.isRegistered(number);
}

void CellularSystem::detach(MobileLeg& leg)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        for (const std::string& number : m_router.detach(leg))
        {
            Event ev;
            ev.subscriber = true;
            ev.number = number;
            ev.registered = false;
            m_events.push_back(std::move(ev));
        }

        // Nothing queued for the leg may be delivered after this function returns.
        m_events.erase(std::remove_if(m_events.begin(), m_events.end(),
            [&leg](const Event& e) { return !e.subscriber && (e.notification.leg == &leg); }),
            m_events.end());

        // A callback to the leg that is running on another thread must finish first.
        m_idle.wait(lock, [this, &leg] {
            return (m_delivering != &leg) || (m_deliveringThread == std::this_thread::get_id());
        });
    }

    flush();
}

CallRouter::SetupResult CellularSystem::setup(MobileLeg& origin, const std::string& callerId, const std::string& dialed)
{
    CallRouter::SetupResult result;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        result = m_router.setup(origin, callerId, dialed);
    }

    flush();
    return result;
}

void CellularSystem::alerting(CallRef callref)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_router.alerting(callref);
    }

    flush();
}

void CellularSystem::answer(CallRef callref)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_router.answer(callref);
    }

    flush();
}

void CellularSystem::release(CallRef callref, Cause cause)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_router.release(callref, cause);
    }

    flush();
}

void CellularSystem::audioFromMobile(CallRef callref, const float* samples, int count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_router.audioFromMobile(callref, samples, count);
}

void CellularSystem::audioToMobile(CallRef callref, float* samples, int count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_router.audioToMobile(callref, samples, count);
}

void CellularSystem::run()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timers.run();
    }

    flush();
}

size_t CellularSystem::callCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_router.callCount();
}

// Delivers queued events one at a time, outside the mutex, in the order they were queued.
// Only one thread delivers at a time. A thread that finds delivery in progress returns;
// the delivering thread picks up its events before it stops.
void CellularSystem::flush()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_flushing) {
            return;
        }

        m_flushing = true;
    }

    // Leaves the system in a consistent state even if a callback throws.
    struct Guard
    {
        CellularSystem& system;
        bool finished = false;

        explicit Guard(CellularSystem& s) : system(s) {}

        ~Guard()
        {
            if (!finished)
            {
                std::lock_guard<std::mutex> lock(system.m_mutex);
                system.m_flushing = false;
                system.m_delivering = nullptr;
                system.m_idle.notify_all();
            }
        }
    } guard(*this);

    for (;;)
    {
        Event ev;
        SubscriberListener listener;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_events.empty())
            {
                m_flushing = false;
                m_idle.notify_all();
                guard.finished = true;
                return;
            }

            ev = std::move(m_events.front());
            m_events.pop_front();

            if (ev.subscriber) {
                listener = m_listener;
            } else {
                m_delivering = ev.notification.leg;
                m_deliveringThread = std::this_thread::get_id();
            }
        }

        if (ev.subscriber)
        {
            if (listener) {
                listener(ev.number, ev.registered);
            }
        }
        else
        {
            CallRouter::deliver(ev.notification);

            std::lock_guard<std::mutex> lock(m_mutex);
            m_delivering = nullptr;
            m_idle.notify_all();
        }
    }
}

} // namespace modemcellular
