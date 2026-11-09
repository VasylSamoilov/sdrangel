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

#include <cmath>

#include "calltypes.h"
#include "tonegenerator.h"

namespace modemcellular
{

namespace
{

// One step of a cadence: a burst of tone or a pause.
struct Step
{
    int milliseconds;
    bool on;
};

// A tone: up to two frequencies, optional amplitude modulation, and a repeating cadence.
struct ToneDef
{
    double frequency1;          // Hz
    double frequency2;          // Hz, 0 if there is a single frequency
    double modulation;          // Hz, 0 if the tone is not amplitude modulated
    int steps;
    Step cadence[4];
};

constexpr double kPeak = 0.25;
constexpr int kRampSamples = 40;                // 5 ms
constexpr double kTwoPi = 6.283185307179586;

// Values from ITU-T E.180, ITU Operational Bulletin No. 781 (see tonegenerator.h).
const ToneDef kAmericanRingback   = {440.0, 480.0, 0.0, 2, {{2000, true}, {4000, false}}};
const ToneDef kAmericanBusy       = {480.0, 620.0, 0.0, 2, {{500, true}, {500, false}}};
const ToneDef kAmericanCongestion = {480.0, 620.0, 0.0, 2, {{300, true}, {200, false}}};

const ToneDef kDenmarkRingback    = {425.0, 0.0, 0.0, 2, {{1000, true}, {4000, false}}};
const ToneDef kDenmarkBusy        = {425.0, 0.0, 0.0, 2, {{250, true}, {250, false}}};
const ToneDef kDenmarkCongestion  = {425.0, 0.0, 0.0, 2, {{250, true}, {250, false}}};

const ToneDef kUkRingback         = {400.0, 450.0, 0.0, 4, {{400, true}, {200, false}, {400, true}, {2000, false}}};
const ToneDef kUkBusy             = {400.0, 0.0, 0.0, 2, {{375, true}, {375, false}}};
const ToneDef kUkCongestion       = {400.0, 0.0, 0.0, 4, {{400, true}, {350, false}, {225, true}, {525, false}}};

const ToneDef kJapanRingback      = {400.0, 0.0, 16.0, 2, {{1000, true}, {2000, false}}};
const ToneDef kJapanBusy          = {400.0, 0.0, 0.0, 2, {{500, true}, {500, false}}};

const ToneDef* lookup(ToneSet toneSet, Tone tone)
{
    switch (toneSet)
    {
    case ToneSet::American:
        if (tone == Tone::Ringback) { return &kAmericanRingback; }
        if (tone == Tone::Busy) { return &kAmericanBusy; }
        if (tone == Tone::Congestion) { return &kAmericanCongestion; }
        break;
    case ToneSet::Denmark:
        if (tone == Tone::Ringback) { return &kDenmarkRingback; }
        if (tone == Tone::Busy) { return &kDenmarkBusy; }
        if (tone == Tone::Congestion) { return &kDenmarkCongestion; }
        break;
    case ToneSet::Uk:
        if (tone == Tone::Ringback) { return &kUkRingback; }
        if (tone == Tone::Busy) { return &kUkBusy; }
        if (tone == Tone::Congestion) { return &kUkCongestion; }
        break;
    case ToneSet::Japan:
        if (tone == Tone::Ringback) { return &kJapanRingback; }
        if (tone == Tone::Busy || tone == Tone::Congestion) { return &kJapanBusy; }
        break;
    }

    return nullptr;
}

int64_t samplesOf(int milliseconds)
{
    return static_cast<int64_t>(milliseconds) * kAudioSampleRate / 1000;
}

} // namespace

ToneGenerator::ToneGenerator(ToneSet toneSet) :
    m_toneSet(toneSet),
    m_tone(Tone::Off),
    m_pos(0),
    m_total(0),
    m_phase1(0.0),
    m_phase2(0.0)
{
}

void ToneGenerator::setTone(Tone tone)
{
    m_tone = tone;
    m_pos = 0;
    m_total = 0;
    m_phase1 = 0.0;
    m_phase2 = 0.0;
}

void ToneGenerator::read(float* samples, int count)
{
    const ToneDef* def = (m_tone == Tone::Off) ? nullptr : lookup(m_toneSet, m_tone);

    if (!def)
    {
        for (int i = 0; i < count; i++) {
            samples[i] = 0.0f;
        }

        return;
    }

    int64_t cycle = 0;

    for (int s = 0; s < def->steps; s++) {
        cycle += samplesOf(def->cadence[s].milliseconds);
    }

    const double amplitude = (def->frequency2 > 0.0) ? kPeak / 2.0 : kPeak;
    const double step1 = kTwoPi * def->frequency1 / kAudioSampleRate;
    const double step2 = kTwoPi * def->frequency2 / kAudioSampleRate;
    const double stepMod = kTwoPi * def->modulation / kAudioSampleRate;

    for (int i = 0; i < count; i++)
    {
        // Find the step this sample is in.
        int64_t start = 0;
        int index = 0;

        while ((index < def->steps - 1) && (m_pos >= start + samplesOf(def->cadence[index].milliseconds)))
        {
            start += samplesOf(def->cadence[index].milliseconds);
            index++;
        }

        double value = 0.0;

        if (def->cadence[index].on)
        {
            const int64_t length = samplesOf(def->cadence[index].milliseconds);
            const int64_t fromStart = m_pos - start;
            const int64_t fromEnd = length - 1 - fromStart;
            const int64_t edge = fromStart < fromEnd ? fromStart : fromEnd;
            const double envelope = (edge >= kRampSamples) ? 1.0 : static_cast<double>(edge) / kRampSamples;

            value = std::sin(m_phase1);

            if (def->frequency2 > 0.0) {
                value += std::sin(m_phase2);
            }

            if (def->modulation > 0.0) {
                value *= 0.5 + 0.5 * std::cos(stepMod * static_cast<double>(m_total));
            }

            value *= amplitude * envelope;
        }

        samples[i] = static_cast<float>(value);

        m_phase1 += step1;
        m_phase2 += step2;

        if (m_phase1 >= kTwoPi) {
            m_phase1 -= kTwoPi;
        }

        if (m_phase2 >= kTwoPi) {
            m_phase2 -= kTwoPi;
        }

        m_total++;
        m_pos++;

        if (m_pos >= cycle) {
            m_pos = 0;
        }
    }
}

} // namespace modemcellular
