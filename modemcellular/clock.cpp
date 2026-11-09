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

#include "clock.h"

namespace modemcellular
{

SampleClock::SampleClock(double sampleRate) :
    m_sampleRate(sampleRate > 0.0 ? sampleRate : 1.0),
    m_baseSeconds(0.0),
    m_samples(0)
{
}

void SampleClock::setSampleRate(double sampleRate)
{
    if (sampleRate <= 0.0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_baseSeconds = nowLocked();
    m_samples = 0;
    m_sampleRate = sampleRate;
}

double SampleClock::sampleRate() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sampleRate;
}

void SampleClock::advance(int64_t samples)
{
    if (samples > 0)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_samples += samples;
    }
}

double SampleClock::now() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return nowLocked();
}

double SampleClock::nowLocked() const
{
    return m_baseSeconds + static_cast<double>(m_samples) / m_sampleRate;
}

SteadyClock::SteadyClock() :
    m_start(std::chrono::steady_clock::now())
{
}

double SteadyClock::now() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
}

} // namespace modemcellular
