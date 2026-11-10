///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Andreas Eversberg <jolly@eversberg.eu>                     //
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

#include "emphasis.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace modemnmt
{

namespace
{

const double kPi = 3.14159265358979323846;

// 100 ms of a 1 kHz cosine (at least 1000 samples)
std::vector<float> testTone(double sampleRate)
{
    const int count = std::max(1000, std::min(100000, static_cast<int>(sampleRate / 10.0)));
    std::vector<float> tone(static_cast<size_t>(count));

    for (int i = 0; i < count; i++) {
        tone[static_cast<size_t>(i)] = static_cast<float>(std::cos(2.0 * kPi * 1000.0 / sampleRate * i));
    }
    return tone;
}

// the peak of the second half, after the filters settled
double level(const std::vector<float>& samples)
{
    double peak = 0.0;

    for (size_t i = samples.size() / 2; i < samples.size(); i++) {
        peak = std::max(peak, static_cast<double>(samples[i]));
    }
    return peak;
}

} // namespace

Emphasis::Emphasis(double sampleRate) :
    m_factor(std::exp(-2.0 * kPi * kCutOff / sampleRate)),
    m_preAmp(1.0),
    m_deAmp(1.0),
    m_xLast(0.0),
    m_yLast(0.0),
    m_lowpass(kLowpass, sampleRate, 2),
    m_highpass(kHighpass, sampleRate, 1)
{
    // neutral at 1 kHz: measured with a tone, the state then starts again
    std::vector<float> tone = testTone(sampleRate);
    preRaw(tone.data(), static_cast<int>(tone.size()));
    m_preAmp = 1.0 / level(tone);

    tone = testTone(sampleRate);
    deRaw(tone.data(), static_cast<int>(tone.size()));
    m_deAmp = 1.0 / level(tone);

    m_xLast = 0.0;
    m_yLast = 0.0;
    m_lowpass.reset();
}

void Emphasis::preRaw(float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        const double x = m_lowpass.process(samples[i]);
        const double y = x - m_factor * m_xLast;

        m_xLast = x;
        samples[i] = static_cast<float>(m_preAmp * y);
    }
}

void Emphasis::deRaw(float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        const double y = samples[i] + m_factor * m_yLast;

        m_yLast = y;
        samples[i] = static_cast<float>(m_deAmp * y);
    }
}

void Emphasis::pre(float* samples, int count)
{
    preRaw(samples, count);
}

void Emphasis::de(float* samples, int count)
{
    for (int i = 0; i < count; i++) {
        samples[i] = static_cast<float>(m_highpass.process(samples[i]));
    }
    deRaw(samples, count);
}

} // namespace modemnmt
