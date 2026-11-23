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

#include "compandor.h"

#include <algorithm>
#include <cmath>

namespace modemcellular
{

namespace
{

// The envelope has changed by these factors after the attack and the recovery time: within 2 dB
// of the final value after a 12 dB step, as G.162 defines the times (an exponential of 4/6).
const double kAttackFactor = 3.0;
const double kRecoveryFactor = 0.33;
// The envelope is held between -30 dB (the compressor lifts quiet sound by 15 dB at most) and
// +20 dB; the expander does not go above 1.0.
const double kEnvelopeMin = 0.032;
const double kEnvelopeMax = 9.990;
const double kExpanderEnvelopeMax = 1.0;
// the square root of the envelope is taken in steps of this size
const double kEnvelopeStep = 0.001;
const double kPi = 3.14159265358979323846;

} // namespace

Compandor::Compandor(double sampleRate, double attackMs, double recoveryMs) :
    m_sampleRate(sampleRate),
    m_stepUp(std::pow(kAttackFactor, 1000.0 / attackMs / sampleRate)),
    m_stepDown(std::pow(kRecoveryFactor, 1000.0 / recoveryMs / sampleRate)),
    m_expanderMax(kExpanderEnvelopeMax),
    m_lookahead(0),
    m_laPos(0),
    m_knee(0.0),
    m_keA(0.0),
    m_keNorm(1.0),
    m_keXl(0.0),
    m_kePk(0.0),
    m_keEnv(kEnvelopeMin)
{
    reset();
}

void Compandor::setCompressorLookahead(double ms)
{
    const int n = static_cast<int>(m_sampleRate * ms / 1000.0 + 0.5);

    m_lookahead = std::max(0, std::min(kMaxLookahead, n));
    m_laBuf.assign(static_cast<size_t>(m_lookahead), 0.0);
    m_kgBuf.assign(static_cast<size_t>(m_lookahead), 1.0);
    m_laPos = 0;
}

void Compandor::setCompressorKnee(double knee, double emphasisHz)
{
    m_knee = knee > 0.0 ? knee : 0.0;
    m_keA = std::exp(-2.0 * kPi * emphasisHz / m_sampleRate);
    // the first order pre-emphasis x - a x[-1], 0 dB at 1 kHz
    m_keNorm = 1.0 / std::sqrt(1.0 + m_keA * m_keA - 2.0 * m_keA * std::cos(2.0 * kPi * 1000.0 / m_sampleRate));
    m_keXl = 0.0;
    m_kePk = 0.0;
    m_keEnv = kEnvelopeMin;
    std::fill(m_kgBuf.begin(), m_kgBuf.end(), 1.0);
}

void Compandor::reset()
{
    m_compressor.peak = 1.0;
    m_compressor.envelope = 1.0;
    m_expander.peak = 1.0;
    m_expander.envelope = 1.0;
    std::fill(m_laBuf.begin(), m_laBuf.end(), 0.0);
    std::fill(m_kgBuf.begin(), m_kgBuf.end(), 1.0);
    m_laPos = 0;
    m_keXl = 0.0;
    m_kePk = 0.0;
    m_keEnv = kEnvelopeMin;
}

// The peak rises at once and falls with the recovery time; the envelope follows the peak with
// the attack and the recovery time (the syllables of speech, not single cycles).
void Compandor::track(Tracker& t, double value, double maxEnvelope) const
{
    const double level = std::fabs(value);

    if (level > t.peak) {
        t.peak = level;
    } else {
        t.peak *= m_stepDown;
    }

    if (t.peak > t.envelope) {
        t.envelope *= m_stepUp;
    } else {
        t.envelope *= m_stepDown;
    }

    if (t.envelope < kEnvelopeMin) {
        t.envelope = kEnvelopeMin;
    }
    if (t.envelope > maxEnvelope) {
        t.envelope = maxEnvelope;
    }
}

void Compandor::compress(float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        const double value = std::isfinite(samples[i]) ? samples[i] : 0.0;

        track(m_compressor, value, kEnvelopeMax);
        const double quantised = std::floor(m_compressor.envelope / kEnvelopeStep) * kEnvelopeStep;
        double gain = 1.0 / std::sqrt(quantised);

        if (m_lookahead > 0)
        {
            // the envelope follows the new sample, the gain goes to the delayed one
            const double delayed = m_laBuf[m_laPos];

            if (m_knee > 0.0)
            {
                // the envelope of the new sample, compressed and pre-emphasized
                const double x = value * gain;
                const double e = std::fabs((x - m_keA * m_keXl) * m_keNorm);
                m_keXl = x;
                m_kePk = e > m_kePk ? e : m_kePk * m_stepDown;
                m_keEnv *= m_kePk > m_keEnv ? m_stepUp : m_stepDown;
                m_keEnv = std::max(m_keEnv, kEnvelopeMin);
                m_kgBuf[m_laPos] = m_keEnv > m_knee ? m_knee / m_keEnv : 1.0;
                gain *= *std::min_element(m_kgBuf.begin(), m_kgBuf.end());
            }
            m_laBuf[m_laPos] = value;
            m_laPos = (m_laPos + 1) % static_cast<size_t>(m_lookahead);
            samples[i] = static_cast<float>(delayed * gain);
        }
        else
        {
            samples[i] = static_cast<float>(value * gain);
        }
    }
}

void Compandor::expand(float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        const double value = std::isfinite(samples[i]) ? samples[i] : 0.0;

        track(m_expander, value, m_expanderMax);
        samples[i] = static_cast<float>(value * m_expander.envelope);
    }
}

} // namespace modemcellular
