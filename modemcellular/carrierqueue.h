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

#ifndef MODEMCELLULAR_CARRIERQUEUE_H_
#define MODEMCELLULAR_CARRIERQUEUE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

#include "export.h"

namespace modemcellular
{

// The audio of one carrier on its way to the transmitter. The network writes what it rendered
// for a received block; the transmitter reads at the rate of its own device. Both sample clocks
// differ a little (two crystals), so the reader resamples by a ratio close to 1 that keeps the
// queue at its target fill: the lead of the transmitter over the receiver. The lead is the part of
// the round trip that this queue adds. An empty queue gives silence and is counted.
// Written by one thread, read by another.
class MODEMCELLULAR_API CarrierQueue
{
public:
    // target: samples of lead the queue keeps; the queue starts with as much silence
    explicit CarrierQueue(size_t target);

    void write(const float* samples, size_t count);
    // count samples for the transmitter, ratio input samples per output sample (from ratio())
    void read(float* samples, size_t count, double ratio);
    // the ratio that brings the fill back to the target; at most +-maxPpm from 1
    double ratio(double maxPpm = 200.0);

    size_t fill() const;
    size_t target() const { return m_target; }
    uint64_t underruns() const;
    void reset();

private:
    mutable std::mutex m_mutex;
    std::deque<float> m_queue;
    size_t m_target;
    double m_position;              // 0..1 between the first two samples
    double m_smoothed;              // fill error, smoothed
    uint64_t m_underruns;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CARRIERQUEUE_H_
