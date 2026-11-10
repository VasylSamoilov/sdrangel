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

} // namespace

Compandor::Compandor(double sampleRate, double attackMs, double recoveryMs) :
    m_stepUp(std::pow(kAttackFactor, 1000.0 / attackMs / sampleRate)),
    m_stepDown(std::pow(kRecoveryFactor, 1000.0 / recoveryMs / sampleRate))
{
    reset();
}

void Compandor::reset()
{
    m_compressor.peak = 1.0;
    m_compressor.envelope = 1.0;
    m_expander.peak = 1.0;
    m_expander.envelope = 1.0;
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
        samples[i] = static_cast<float>(value / std::sqrt(quantised));
    }
}

void Compandor::expand(float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        const double value = std::isfinite(samples[i]) ? samples[i] : 0.0;

        track(m_expander, value, kExpanderEnvelopeMax);
        samples[i] = static_cast<float>(value * m_expander.envelope);
    }
}

} // namespace modemcellular
