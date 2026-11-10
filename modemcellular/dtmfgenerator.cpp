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

#include "dtmfgenerator.h"

#include <cmath>

namespace modemcellular
{

namespace
{

const double kRate = 8000.0;
const int kToneSamples = 800;               // 100 ms
const int kPauseSamples = 400;              // 50 ms
const int kRamp = 40;                       // 5 ms
const double kLevel = 0.2;
const double kTwoPi = 6.283185307179586;

} // namespace

DtmfGenerator::DtmfGenerator()
{
    reset();
}

void DtmfGenerator::reset()
{
    m_queue.clear();
    m_remaining = 0;
    m_low = 0.0;
    m_high = 0.0;
    m_phaseLow = 0.0;
    m_phaseHigh = 0.0;
    m_pos = 0;
}

bool DtmfGenerator::frequencies(char digit, double& low, double& high)
{
    static const double rows[4] = {697.0, 770.0, 852.0, 941.0};
    static const double columns[4] = {1209.0, 1336.0, 1477.0, 1633.0};
    static const char keys[] = "123A456B789C*0#D";

    if (digit >= 'a' && digit <= 'd') {
        digit = static_cast<char>(digit - 'a' + 'A');
    }
    for (int i = 0; i < 16; i++)
    {
        if (keys[i] == digit)
        {
            low = rows[i / 4];
            high = columns[i % 4];
            return true;
        }
    }

    return false;
}

bool DtmfGenerator::queue(char digit)
{
    double low, high;

    if (!frequencies(digit, low, high) || static_cast<int>(m_queue.size()) >= kMaxQueued) {
        return false;
    }
    m_queue.push_back(digit);
    return true;
}

void DtmfGenerator::mix(float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (m_remaining <= 0)
        {
            if (m_queue.empty()) {
                return;
            }
            frequencies(m_queue.front(), m_low, m_high);
            m_queue.pop_front();
            m_remaining = kToneSamples + kPauseSamples;
            m_pos = 0;
            m_phaseLow = 0.0;
            m_phaseHigh = 0.0;
        }
        if (m_pos < kToneSamples)
        {
            double ramp = 1.0;

            // the edges of the tone are ramped (no clicks)
            if (m_pos < kRamp) {
                ramp = static_cast<double>(m_pos) / kRamp;
            } else if (m_pos >= kToneSamples - kRamp) {
                ramp = static_cast<double>(kToneSamples - m_pos) / kRamp;
            }
            samples[i] += static_cast<float>(ramp * kLevel * (std::sin(m_phaseLow) + std::sin(m_phaseHigh)));
            m_phaseLow += kTwoPi * m_low / kRate;
            m_phaseHigh += kTwoPi * m_high / kRate;
        }
        m_pos++;
        m_remaining--;
    }
}

} // namespace modemcellular
