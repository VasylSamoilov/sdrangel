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

#include "carrierqueue.h"

#include <algorithm>

namespace modemcellular
{

namespace
{

// How strongly the fill error moves the ratio: an error of the whole target is 0.002 (2000 ppm),
// so 50 ppm of drift leave 2.5 % of the target as error, and the fill settles within minutes.
const double kGain = 0.002;
// smoothing of the fill error per read, against the steps of block-wise writing
const double kSmoothing = 0.01;

} // namespace

CarrierQueue::CarrierQueue(size_t target) :
    m_target(target),
    m_position(0.0),
    m_smoothed(0.0),
    m_underruns(0)
{
    reset();
}

void CarrierQueue::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_queue.assign(m_target, 0.0f);
    m_position = 0.0;
    m_smoothed = 0.0;
    m_underruns = 0;
}

void CarrierQueue::write(const float* samples, size_t count)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_queue.insert(m_queue.end(), samples, samples + count);
}

void CarrierQueue::read(float* samples, size_t count, double ratio)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    bool empty = false;

    for (size_t i = 0; i < count; i++)
    {
        if (m_queue.size() < 2)
        {
            samples[i] = 0.0f;
            empty = true;
            continue;
        }
        // linear interpolation between the first two samples
        samples[i] = static_cast<float>(m_queue[0] * (1.0 - m_position) + m_queue[1] * m_position);
        m_position += ratio;
        while (m_position >= 1.0 && !m_queue.empty())
        {
            m_queue.pop_front();
            m_position -= 1.0;
        }
    }
    if (empty) {
        m_underruns++;
    }
}

double CarrierQueue::ratio(double maxPpm)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const double target = m_target ? static_cast<double>(m_target) : 1.0;
    const double error = (static_cast<double>(m_queue.size()) - target) / target;

    m_smoothed += kSmoothing * (error - m_smoothed);
    const double limit = maxPpm * 1e-6;

    return 1.0 + std::max(-limit, std::min(limit, kGain * m_smoothed));
}

size_t CarrierQueue::fill() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_queue.size();
}

uint64_t CarrierQueue::underruns() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_underruns;
}

} // namespace modemcellular
