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

#include "calltypes.h"
#include "echoendpoint.h"

namespace modemcellular
{

EchoEndpoint::EchoEndpoint(double delaySeconds)
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
            samples[i] = m_queue.front();
            m_queue.pop_front();
        }
    }
}

} // namespace modemcellular
