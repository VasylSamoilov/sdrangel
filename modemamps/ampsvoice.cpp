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

#include "ampsvoice.h"

#include <algorithm>
#include <cmath>

#include "ampsfsk.h"
#include "ampsmessages.h"

namespace modemamps
{

namespace
{

const double kPi = 3.14159265358979323846;
const double kWindowSeconds = 1.0 / 15.0;
const int kHammingPoints = 256;
const double kNoiseHz = 5790.0;
// osmocom dsp.c
const double kSatQuality = 0.85;
const double kSatLevelHigh = 0.50;          // to detect a SAT, relative to its deviation
const double kSatLevelLow = 0.35;           // to keep it
const double kSatFreqRatio = 2.0;           // the strongest 6 dB above the others
const int kSatDetectCount = 5;
const int kSatLostCount = 5;
const int kSatChangeCount = 5;
const double kSigQuality = 0.80;
const double kSigLevel = 0.20;              // relative to the FSK deviation
const int kSigDetectCount = 6;
const int kSigLostCount = 4;

} // namespace

SatTone::SatTone(double sampleRate, int scc) :
    m_enabled(satFrequency(scc) > 0.0)
{
    if (m_enabled) {
        m_nco.setFreq(static_cast<Real>(satFrequency(scc)), static_cast<Real>(sampleRate));
    }
}

void SatTone::add(float* samples, int length)
{
    if (!m_enabled) {
        return;
    }

    for (int i = 0; i < length; i++) {
        samples[i] += static_cast<float>(m_nco.next() * kSatDeviation);
    }
}

SatDetector::SatDetector(double sampleRate) :
    m_window(static_cast<size_t>(sampleRate * kWindowSeconds + 0.5)),
    m_filled(0),
    m_hamming(kHammingPoints),
    m_goertzel{{Goertzel(satFrequency(0), static_cast<int>(sampleRate)), Goertzel(satFrequency(1), static_cast<int>(sampleRate)),
        Goertzel(satFrequency(2), static_cast<int>(sampleRate)), Goertzel(kNoiseHz, static_cast<int>(sampleRate)),
        Goertzel(kSignallingToneHz, static_cast<int>(sampleRate))}},
    m_state(-1),
    m_pending(-1),
    m_pendingCount(0),
    m_tone(false),
    m_toneCount(0)
{
    for (int i = 0; i < kHammingPoints; i++) {
        m_hamming[static_cast<size_t>(i)] = 0.54 - 0.46 * std::cos(2.0 * kPi * i / kHammingPoints);
    }
}

void SatDetector::reset()
{
    m_filled = 0;
    m_state = -1;
    m_pending = -1;
    m_pendingCount = 0;
    m_tone = false;
    m_toneCount = 0;
}

void SatDetector::process(const float* samples, int length)
{
    for (int i = 0; i < length; i++)
    {
        m_window[m_filled++] = samples[i];
        if (m_filled == m_window.size())
        {
            m_filled = 0;
            decode();
        }
    }
}

// The amplitude of the frequency in the window: SDRangel's Goertzel over the window with the
// Hamming window, which takes the amplitude to 0.54 of it (osmocom's audio_goertzel: the amplitude)
double SatDetector::amplitude(Goertzel& goertzel)
{
    const size_t length = m_window.size();

    goertzel.reset();
    for (size_t n = 0; n + 1 < length; n++) {
        goertzel.filter(m_window[n] * m_hamming[n * kHammingPoints / length]);
    }
    goertzel.goertzel(m_window[length - 1] * m_hamming[(length - 1) * kHammingPoints / length]);
    return goertzel.mag() / 0.54;
}

void SatDetector::decode()
{
    double results[5];

    for (int i = 0; i < 5; i++) {
        results[i] = amplitude(m_goertzel[static_cast<size_t>(i)]);
    }

    // the SAT: the strongest of the three, 6 dB above the others
    double levels[3];
    int strongest = 0;
    double second = 0.0;

    for (int i = 0; i < 3; i++) {
        levels[i] = results[i] / kSatDeviation;
    }
    for (int i = 1; i < 3; i++)
    {
        if (levels[i] > levels[strongest]) {
            strongest = i;
        }
    }
    for (int i = 0; i < 3; i++)
    {
        if (i != strongest && levels[i] > second) {
            second = levels[i];
        }
    }
    const double satLevel = levels[strongest];
    const double noiseLevel = results[3] / kSatDeviation;
    const bool valid = satLevel >= second * kSatFreqRatio;
    double satQuality = (satLevel > noiseLevel && satLevel > 0.001) ? (satLevel - noiseLevel) / satLevel : 0.0;
    satQuality = std::max(satQuality, 0.0);

    int state = -1;
    if (valid && satQuality > kSatQuality && satLevel >= ((m_state < 0) ? kSatLevelHigh : kSatLevelLow)) {
        state = strongest;
    }

    if (m_state >= 0 && m_qualitySink) {
        m_qualitySink(satQuality);
    }

    // a new state counts after some windows of it
    if (state == m_pending) {
        m_pendingCount++;
    }
    else
    {
        m_pending = state;
        m_pendingCount = 1;
    }
    int required = 1;
    if (state < 0 && m_state >= 0) {
        required = kSatLostCount;
    } else if (state >= 0 && m_state < 0) {
        required = kSatDetectCount;
    } else if (state != m_state) {
        required = kSatChangeCount;
    }
    if (m_pendingCount >= required && state != m_state)
    {
        m_state = state;
        if (m_satSink) {
            m_satSink(state, (state >= 0) ? satQuality : 0.0);
        }
    }

    // the signalling tone
    const double sigLevel = results[4] / kFskDeviation;
    double sigQuality = (results[4] > results[3] && results[4] > 0.001) ? (results[4] - results[3]) / results[4] : 0.0;
    sigQuality = std::max(sigQuality, 0.0);

    if (sigQuality > kSigQuality && sigLevel > kSigLevel)
    {
        if (!m_tone)
        {
            if (++m_toneCount >= kSigDetectCount)
            {
                m_tone = true;
                m_toneCount = 0;
                if (m_toneSink) {
                    m_toneSink(true, sigQuality);
                }
            }
        }
        else {
            m_toneCount = 0;
        }
    }
    else
    {
        if (m_tone)
        {
            if (++m_toneCount == kSigLostCount)
            {
                m_tone = false;
                m_toneCount = 0;
                if (m_toneSink) {
                    m_toneSink(false, 0.0);
                }
            }
        }
        else {
            m_toneCount = 0;
        }
    }
}

} // namespace modemamps
