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

BiquadLowpass::BiquadLowpass(double cutoffHz, double sampleRate, int sections) :
    m_z1(static_cast<size_t>(std::max(sections, 1)), 0.0),
    m_z2(static_cast<size_t>(std::max(sections, 1)), 0.0)
{
    const double pi = 3.14159265358979323846;
    const double q = std::pow(std::sqrt(0.5), 1.0 / static_cast<double>(m_z1.size()));
    const double k = std::tan(pi * cutoffHz / sampleRate);
    const double norm = 1.0 / (1.0 + k / q + k * k);

    m_a0 = k * k * norm;
    m_a1 = 2.0 * m_a0;
    m_a2 = m_a0;
    m_b1 = 2.0 * (k * k - 1.0) * norm;
    m_b2 = (1.0 - k / q + k * k) * norm;
}

double BiquadLowpass::process(double in)
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

void BiquadLowpass::reset()
{
    std::fill(m_z1.begin(), m_z1.end(), 0.0);
    std::fill(m_z2.begin(), m_z2.end(), 0.0);
}

} // namespace modemnmt
