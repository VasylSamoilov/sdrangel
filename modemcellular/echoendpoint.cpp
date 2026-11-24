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
#include <cmath>

#include "calltypes.h"
#include "echoendpoint.h"

namespace modemcellular
{

namespace
{

const double kAnnounceSeconds = 0.2;
const double kAnnounceHz = 1000.0;
const double kAnnounceLevel = 0.316;        // -10 dB of the reference

} // namespace

EchoEndpoint::EchoEndpoint(double delaySeconds, double levelDb, bool announce) :
    m_gain(static_cast<float>(std::pow(10.0, std::min(levelDb, 0.0) / 20.0))),
    m_announce(announce ? static_cast<int>(kAnnounceSeconds * kAudioSampleRate) : 0),
    m_announced(0)
{
    const double delay = delaySeconds > 0.0 ? delaySeconds : 0.0;
    const size_t delaySamples = static_cast<size_t>(delay * kAudioSampleRate + 0.5);

    // The queue starts with the delay worth of silence and may hold one more second.
    m_queue.assign(delaySamples, 0.0f);
    m_maxQueued = delaySamples + kAudioSampleRate;
}

void EchoEndpoint::fromMobile(const float* samples, int count)
{
    for (int i = 0; i < count; i++) {
        m_queue.push_back(samples[i]);
    }

    while (m_queue.size() > m_maxQueued) {
        m_queue.pop_front();
    }
}

void EchoEndpoint::toMobile(float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (m_queue.empty()) {
            samples[i] = 0.0f;
        } else {
            samples[i] = m_queue.front() * m_gain;
            m_queue.pop_front();
        }
        // the tone of the answer over the first of the delay (silence then)
        if (m_announced < m_announce)
        {
            samples[i] += static_cast<float>(kAnnounceLevel * std::sin(2.0 * M_PI * kAnnounceHz * m_announced / kAudioSampleRate));
            m_announced++;
        }
    }
}

} // namespace modemcellular
