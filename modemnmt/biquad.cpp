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

#include "biquad.h"

#include <algorithm>
#include <cmath>

namespace modemnmt
{

namespace
{

const double kPi = 3.14159265358979323846;

int sectionCount(int sections)
{
    return std::max(sections, 1);
}

double butterworthQ(int sections)
{
    return std::pow(std::sqrt(0.5), 1.0 / static_cast<double>(sectionCount(sections)));
}

} // namespace

BiquadCascade::BiquadCascade(int sections) :
    m_a0(1.0), m_a1(0.0), m_a2(0.0), m_b1(0.0), m_b2(0.0),
    m_z1(static_cast<size_t>(sectionCount(sections)), 0.0),
    m_z2(static_cast<size_t>(sectionCount(sections)), 0.0)
{
}

double BiquadCascade::process(double in)
{
    for (size_t i = 0; i < m_z1.size(); i++)
    {
        const double out = in * m_a0 + m_z1[i];
        m_z1[i] = in * m_a1 + m_z2[i] - m_b1 * out;
        m_z2[i] = in * m_a2 - m_b2 * out;
        in = out;
    }

    return in;
}

void BiquadCascade::reset()
{
    std::fill(m_z1.begin(), m_z1.end(), 0.0);
    std::fill(m_z2.begin(), m_z2.end(), 0.0);
}

BiquadLowpass::BiquadLowpass(double cutoffHz, double sampleRate, int sections) :
    BiquadCascade(sections)
{
    const double q = butterworthQ(sections);
    const double k = std::tan(kPi * cutoffHz / sampleRate);
    const double norm = 1.0 / (1.0 + k / q + k * k);

    m_a0 = k * k * norm;
    m_a1 = 2.0 * m_a0;
    m_a2 = m_a0;
    m_b1 = 2.0 * (k * k - 1.0) * norm;
    m_b2 = (1.0 - k / q + k * k) * norm;
}

BiquadHighpass::BiquadHighpass(double cutoffHz, double sampleRate, int sections) :
    BiquadCascade(sections)
{
    const double q = butterworthQ(sections);
    const double k = std::tan(kPi * cutoffHz / sampleRate);
    const double norm = 1.0 / (1.0 + k / q + k * k);

    m_a0 = norm;
    m_a1 = -2.0 * m_a0;
    m_a2 = m_a0;
    m_b1 = 2.0 * (k * k - 1.0) * norm;
    m_b2 = (1.0 - k / q + k * k) * norm;
}

ButterworthFilter::ButterworthFilter(Type type, double cutoffHz, double sampleRate, int order)
{
    const int pairs = std::max((order + 1) / 2, 1);
    const int n = 2 * pairs;
    const double k = std::tan(kPi * cutoffHz / sampleRate);

    for (int i = 1; i <= pairs; i++)
    {
        // pole pair i of the Butterworth polynomial of order n
        const double q = 1.0 / (2.0 * std::sin(kPi * (2.0 * i - 1.0) / (2.0 * n)));
        const double norm = 1.0 / (1.0 + k / q + k * k);
        Section s;

        if (type == Type::Lowpass)
        {
            s.a0 = k * k * norm;
            s.a1 = 2.0 * s.a0;
        }
        else
        {
            s.a0 = norm;
            s.a1 = -2.0 * s.a0;
        }
        s.a2 = s.a0;
        s.b1 = 2.0 * (k * k - 1.0) * norm;
        s.b2 = (1.0 - k / q + k * k) * norm;
        s.z1 = s.z2 = 0.0;
        m_sections.push_back(s);
    }
}

double ButterworthFilter::process(double in)
{
    for (Section& s : m_sections)
    {
        const double out = in * s.a0 + s.z1;
        s.z1 = in * s.a1 + s.z2 - s.b1 * out;
        s.z2 = in * s.a2 - s.b2 * out;
        in = out;
    }

    return in;
}

void ButterworthFilter::reset()
{
    for (Section& s : m_sections) {
        s.z1 = s.z2 = 0.0;
    }
}

BiquadNotch::BiquadNotch(double frequencyHz, double sampleRate, int sections, double q) :
    BiquadCascade(sections)
{
    const double k = std::tan(kPi * frequencyHz / sampleRate);
    const double norm = 1.0 / (1.0 + k / q + k * k);

    m_a0 = (1.0 + k * k) * norm;
    m_a1 = 2.0 * (k * k - 1.0) * norm;
    m_a2 = m_a0;
    m_b1 = m_a1;
    m_b2 = (1.0 - k / q + k * k) * norm;
}

} // namespace modemnmt
