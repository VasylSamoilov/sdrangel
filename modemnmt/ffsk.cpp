///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2017 Andreas Eversberg <jolly@eversberg.eu>                     //
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

#include "ffsk.h"

#include <cmath>

namespace modemnmt
{

namespace
{

const double kPi = 3.14159265358979323846;

bool halfWavesFit(double frequency, double bitRate)
{
    const double waves = frequency / bitRate;
    return std::fabs(std::round(waves * 2.0) - waves * 2.0) <= 0.001;
}

double wrapCycles(double phase)
{
    phase -= std::floor(phase);
    return phase;
}

} // namespace

FfskModulator::FfskModulator(double sampleRate, double bitRate, double f0, double f1, double level) :
    m_valid(sampleRate > 0.0 && bitRate > 0.0 && f0 > 0.0 && f1 > 0.0
            && halfWavesFit(f0, bitRate) && halfWavesFit(f1, bitRate)),
    m_level(level),
    m_bitsPerSample(0.0),
    m_phaseShift{0.0, 0.0},
    m_cyclesPerBit{0.0, 0.0},
    m_phase(0.0),
    m_bitPos(0.0),
    m_bit(-1)
{
    if (m_valid)
    {
        m_bitsPerSample = bitRate / sampleRate;
        m_phaseShift[0] = f0 / sampleRate;
        m_phaseShift[1] = f1 / sampleRate;
        m_cyclesPerBit[0] = f0 / bitRate;
        m_cyclesPerBit[1] = f1 / bitRate;
    }
}

void FfskModulator::reset()
{
    m_phase = 0.0;
    m_bitPos = 0.0;
    m_bit = -1;
}

// Fetches the next bit. On every bit change the phase is rounded to the nearest zero crossing,
// which keeps rounding errors from adding up, then moved forward to the current position in the bit.
bool FfskModulator::nextBit(double& phase)
{
    const int bit = m_source ? m_source() : -1;

    if (bit < 0)
    {
        reset();
        phase = 0.0;
        return false;
    }

    m_bit = bit & 1;
    phase = (phase > 0.25 && phase < 0.75) ? 0.5 : 0.0;
    phase = wrapCycles(phase + m_bitPos * m_cyclesPerBit[m_bit]);
    return true;
}

int FfskModulator::render(float* samples, int length, bool add)
{
    if (!m_valid || length <= 0) {
        return 0;
    }

    int count = 0;
    double phase = m_phase;

    if (m_bit < 0 && !nextBit(phase))
    {
        m_phase = phase;
        return 0;
    }

    for (;;)
    {
        const double shift = m_phaseShift[m_bit];

        while (count < length && m_bitPos < 1.0)
        {
            const float value = static_cast<float>(std::sin(2.0 * kPi * phase) * m_level);
            if (add) {
                samples[count++] += value;
            } else {
                samples[count++] = value;
            }
            phase = wrapCycles(phase + shift);
            m_bitPos += m_bitsPerSample;
        }

        if (m_bitPos < 1.0) {
            break;
        }

        // bit finished: go back to the phase at the moment the bit ended, then take the next one
        m_bitPos -= 1.0;
        phase = wrapCycles(phase - m_bitPos * m_cyclesPerBit[m_bit]);
        if (!nextBit(phase)) {
            break;
        }
    }

    m_phase = phase;
    return count;
}

FfskDemodulator::FfskDemodulator(double sampleRate, double bitRate, double f0, double f1, double bitAdjust) :
    m_sampleRate(sampleRate),
    m_f0Deviation((f0 - f1) / 2.0),
    m_f1Deviation((f1 - f0) / 2.0),
    m_lowBit(f0 < f1 ? 0 : 1),
    m_highBit(f0 < f1 ? 1 : 0),
    m_rotation(-(f0 + f1) / 2.0 / sampleRate),
    m_mixPhase(0.0),
    m_lastPhase(0.0),
    // 4th order (two sections) at half of the filter bandwidth, for I and Q each
    m_lowpassI(std::fabs(f0 - f1), sampleRate, 2),
    m_lowpassQ(std::fabs(f0 - f1), sampleRate, 2),
    m_bitsPerSample(bitRate / sampleRate),
    m_bitAdjust(bitAdjust),
    m_bitPos(0.0),
    m_bit(-1),
    m_change(false)
{
}

// Samples each bit half a bit after the tone changed. On a tone change the bit clock is moved
// only slightly towards the middle of the bit: noise can fake a change, and a full correction
// would cause bit slips.
void FfskDemodulator::process(const float* samples, int length)
{
    for (int i = 0; i < length; i++)
    {
        // mix down to the centre frequency
        m_mixPhase = wrapCycles(m_mixPhase + m_rotation);
        const double x = samples[i];
        const double iVal = m_lowpassI.process(x * std::cos(2.0 * kPi * m_mixPhase));
        const double qVal = m_lowpassQ.process(x * std::sin(2.0 * kPi * m_mixPhase));

        // frequency offset from the centre
        const double phase = std::atan2(qVal, iVal);
        double dev = (phase - m_lastPhase) / (2.0 * kPi);
        m_lastPhase = phase;
        if (dev < -0.49) {
            dev += 1.0;
        } else if (dev > 0.49) {
            dev -= 1.0;
        }
        const double f = dev * m_sampleRate;

        const int bit = (f < 0.0) ? m_lowBit : m_highBit;
        // the unwanted mixing product is filtered out, so the vector is half the peak amplitude
        const double level = std::sqrt(iVal * iVal + qVal * qVal) * 2.0;

        if (m_bit != bit)
        {
            m_bit = bit;
            if (m_bitPos < 0.5)
            {
                m_bitPos += m_bitAdjust;
                if (m_bitPos > 0.5) {
                    m_bitPos = 0.5;
                }
            }
            else if (m_bitPos > 0.5)
            {
                m_bitPos -= m_bitAdjust;
                if (m_bitPos < 0.5) {
                    m_bitPos = 0.5;
                }
            }
            // another change before a bit was sampled: noise, pass it on with zero quality
            if (m_change && m_sink) {
                m_sink(bit, 0.0, level);
            }
            m_change = true;
        }

        if (m_bitPos >= 1.0)
        {
            const double deviation = (bit == 0) ? m_f0Deviation : m_f1Deviation;
            double quality = 1.0 - std::fabs((f - deviation) / deviation);
            if (quality < 0.0) {
                quality = 0.0;
            }
            if (m_sink) {
                m_sink(bit, quality, level);
            }
            m_bitPos -= 1.0;
            m_change = false;
        }
        m_bitPos += m_bitsPerSample;
    }
}

} // namespace modemnmt
