///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

#include "txaudiosource.h"

#include <algorithm>
#include <cmath>

#include "calltypes.h"

namespace modemcellular
{

namespace
{

const int kPhases = 96;
const double kTapsPerPhase = 24.0 / kPhases;

} // namespace

TxAudioSource::TxAudioSource(double sampleRate) :
    m_step(static_cast<Real>(kAudioSampleRate / sampleRate)),
    m_remain(0.0f),
    m_block(static_cast<size_t>(kBlock), 0.0f),
    m_blockPos(static_cast<size_t>(kBlock))
{
    m_interpolator.create(kPhases, kAudioSampleRate, 3400.0, kTapsPerPhase);
}

void TxAudioSource::reset()
{
    m_blockPos = static_cast<size_t>(kBlock);
    m_remain = 0.0f;
    m_interpolator.create(kPhases, kAudioSampleRate, 3400.0, kTapsPerPhase);   // clears the history
}

double TxAudioSource::nextCallSample()
{
    if (m_blockPos == static_cast<size_t>(kBlock))
    {
        std::fill(m_block.begin(), m_block.end(), 0.0f);
        if (m_pull) {
            m_pull(m_block.data(), kBlock);
        }
        for (float& v : m_block)
        {
            if (!std::isfinite(v)) {
                v = 0.0f;
            }
        }
        m_blockPos = 0;
    }

    return m_block[m_blockPos++];
}

void TxAudioSource::generate(float* samples, int length, bool add, double gain)
{
    for (int i = 0; i < length; i++)
    {
        // the interpolator takes the next call sample when the output has passed the last one
        // (at most one per output, the channel rate being above 8 kHz)
        const Complex in(m_remain >= 1.0f ? static_cast<Real>(nextCallSample()) : 0.0f, 0.0f);
        Complex out;
        m_interpolator.interpolate(&m_remain, in, &out);
        m_remain += m_step;

        const double value = out.real() * gain;
        if (add) {
            samples[i] += static_cast<float>(value);
        } else {
            samples[i] = static_cast<float>(value);
        }
    }
}

} // namespace modemcellular
