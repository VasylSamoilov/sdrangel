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

#ifndef MODEMCELLULAR_CLOCK_H_
#define MODEMCELLULAR_CLOCK_H_

#include <chrono>
#include <cstdint>
#include <mutex>

#include "export.h"

namespace modemcellular
{

// Source of protocol time in seconds. Protocol timers (call set-up supervision,
// paging retries, release) read it through a TimerService. now() may be called
// from any thread.
class MODEMCELLULAR_API Clock
{
public:
    virtual ~Clock() = default;
    virtual double now() const = 0;
};

// Clock driven by a sample counter. The owner calls advance() with the number of
// samples processed. Time is the integer sample count divided by the rate, so it
// does not accumulate floating point error over long runs. Thread safe.
class MODEMCELLULAR_API SampleClock : public Clock
{
public:
    explicit SampleClock(double sampleRate = 48000.0);

    // Changing the rate keeps the time reached so far.
    void setSampleRate(double sampleRate);
    double sampleRate() const;

    void advance(int64_t samples);
    double now() const override;

private:
    double nowLocked() const;

    mutable std::mutex m_mutex;
    double m_sampleRate;
    double m_baseSeconds;   // time accumulated under earlier sample rates
    int64_t m_samples;      // samples counted under the current rate
};

// Clock on the monotonic system clock, zero at construction. Thread safe.
class MODEMCELLULAR_API SteadyClock : public Clock
{
public:
    SteadyClock();
    double now() const override;

private:
    std::chrono::steady_clock::time_point m_start;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CLOCK_H_
