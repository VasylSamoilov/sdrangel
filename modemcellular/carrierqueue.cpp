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
// smoothing of the fill error per read (10 s at reads of 10 ms), against the steps of block-wise
// writing and short stalls of the writer: a stall of the whole lead moves the ratio about 20 ppm,
// inside the bit rate tolerance of the signalling; with the gain the loop stays damped (no overshoot
// beyond the drift)
const double kSmoothing = 0.001;
// samples read while the queue measures the fill it keeps: one second of audio
const size_t kMeasure = 48000;

} // namespace

CarrierQueue::CarrierQueue(size_t target) :
    m_target(target),
    m_position(0.0),
    m_smoothed(0.0),
    m_measured(0),
    m_fillSum(0.0),
    m_reference(0.0),
    m_underruns(0),
    m_priming(false)
{
    reset();
}

void CarrierQueue::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_queue.assign(m_target, 0.0f);
    m_position = 0.0;
    m_smoothed = 0.0;
    m_measured = 0;
    m_fillSum = 0.0;
    m_reference = 0.0;
    m_underruns = 0;
    m_priming = false;
}

void CarrierQueue::write(const float* samples, size_t count)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_queue.insert(m_queue.end(), samples, samples + count);
}

void CarrierQueue::read(float* samples, size_t count, double ratio)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // the samples this read reaches: the last output interpolates up to the one after its position
    const double reach = count ? m_position + static_cast<double>(count - 1) * ratio + 2.0 : 0.0;
    const double size = static_cast<double>(m_queue.size());

    // a read is given in full or is silence: a queue that cannot give it in full runs empty (not one
    // that hangs on to its last samples while the lead is gone), and after running empty it is silent
    // until the lead is there again
    if (m_priming && size >= std::max(static_cast<double>(m_target), reach)) {
        m_priming = false;
    } else if (!m_priming && size < reach) {
        runEmpty();
    }
    if (m_priming)
    {
        std::fill(samples, samples + count, 0.0f);
        return;
    }
    if (m_measured < kMeasure)
    {
        m_fillSum += size * static_cast<double>(count);
        m_measured += count;
        if (m_measured >= kMeasure) {
            m_reference = m_fillSum / static_cast<double>(m_measured);
        }
    }
    for (size_t i = 0; i < count; i++)
    {
        // linear interpolation between the first two samples
        samples[i] = static_cast<float>(m_queue[0] * (1.0 - m_position) + m_queue[1] * m_position);
        m_position += ratio;
        while (m_position >= 1.0 && !m_queue.empty())
        {
            m_queue.pop_front();
            m_position -= 1.0;
        }
    }
}

// under the lock
void CarrierQueue::runEmpty()
{
    m_priming = true;
    m_underruns++;
    // the fill to keep is measured again once the lead is back
    m_measured = 0;
    m_fillSum = 0.0;
    m_smoothed = 0.0;
}

double CarrierQueue::ratio(double maxPpm)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_measured < kMeasure) {
        return 1.0;
    }
    const double target = m_target ? static_cast<double>(m_target) : 1.0;
    const double error = (static_cast<double>(m_queue.size()) - m_reference) / target;

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
