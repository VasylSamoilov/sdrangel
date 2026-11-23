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

#include "nmtvoice.h"

#include <algorithm>
#include <cmath>

namespace modemnmt
{

namespace
{

const double kPi = 3.14159265358979323846;
// quality of a window to count as tone
const double kMinQuality = 0.7;
const int kDetectWindows = 6;
const int kLostWindows = 4;
// the Goertzel filter is as wide as the reciprocal of its length: 30 Hz tone spacing, so a
// window of 1/15 s blocks the neighbours completely
const double kWindowSeconds = 1.0 / 15.0;
const int kHammingPoints = 256;

bool validNumber(int number)
{
    return number >= 1 && number <= 4;
}

// Level of a frequency in the window (SDRangel's Goertzel filter over the window with a Hamming
// window applied; the scale is the same for the tone and the noise).
double level(Goertzel& goertzel, const std::vector<float>& samples, const std::vector<double>& hamming)
{
    const size_t length = samples.size();

    goertzel.reset();
    for (size_t n = 0; n + 1 < length; n++) {
        goertzel.filter(samples[n] * hamming[n * kHammingPoints / length]);
    }
    goertzel.goertzel(samples[length - 1] * hamming[(length - 1) * kHammingPoints / length]);
    return goertzel.mag();
}

} // namespace

SupervisoryDetector::SupervisoryDetector(double sampleRate, int number) :
    m_enabled(validNumber(number)),
    m_filled(0),
    m_tone(validNumber(number) ? kSupervisoryHz[number - 1] : kSupervisoryHz[0], static_cast<int>(sampleRate)),
    m_noise(kSupervisoryNoiseHz, static_cast<int>(sampleRate)),
    m_hamming(kHammingPoints),
    m_detected(false),
    m_count(0)
{
    if (!m_enabled) {
        return;
    }

    m_window.resize(static_cast<size_t>(sampleRate * kWindowSeconds + 0.5));
    for (int i = 0; i < kHammingPoints; i++) {
        m_hamming[static_cast<size_t>(i)] = 0.54 - 0.46 * std::cos(2.0 * kPi * i / kHammingPoints);
    }
}

void SupervisoryDetector::reset()
{
    m_detected = false;
    m_count = 0;
    m_filled = 0;
}

void SupervisoryDetector::process(const float* samples, int length)
{
    if (!m_enabled) {
        return;
    }

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

// Compares the tone with the noise just below the tones.
void SupervisoryDetector::decode()
{
    const double tone = level(m_tone, m_window, m_hamming);
    const double noise = level(m_noise, m_window, m_hamming);
    double quality = (tone > 0.0) ? (tone - noise) / tone : 0.0;

    if (quality < 0.0) {
        quality = 0.0;
    }

    if (quality > kMinQuality)
    {
        if (!m_detected)
        {
            if (++m_count == kDetectWindows)
            {
                m_detected = true;
                m_count = 0;
                if (m_sink) {
                    m_sink(true, quality);
                }
            }
        }
        else {
            m_count = 0;
        }
    }
    else
    {
        if (m_detected)
        {
            if (++m_count == kLostWindows)
            {
                m_detected = false;
                m_count = 0;
                if (m_sink) {
                    m_sink(false, 0.0);
                }
            }
        }
        else {
            m_count = 0;
        }
    }
}

SupervisoryTone::SupervisoryTone(double sampleRate, int number, double level) :
    m_enabled(validNumber(number)),
    m_level(m_enabled ? level * kSupervisoryDeviation / kSpeechDeviationHz / Emphasis::preGain(kSupervisoryHz[number - 1], sampleRate) : 0.0)
{
    if (m_enabled) {
        m_nco.setFreq(static_cast<Real>(kSupervisoryHz[number - 1]), static_cast<Real>(sampleRate));
    }
}

void SupervisoryTone::add(float* samples, int length)
{
    if (!m_enabled) {
        return;
    }

    for (int i = 0; i < length; i++) {
        samples[i] += static_cast<float>(m_nco.next() * m_level);
    }
}

RxAudioChain::RxAudioChain(double sampleRate) :
    m_sampleRate(sampleRate),
    m_taps(static_cast<int>(sampleRate / 48.0) | 1),     // odd, as the FIR filters want
    m_ratio(sampleRate / kCallRate),
    m_next(m_ratio),
    m_previous(0.0)
{
    m_bandpass.create(m_taps, m_sampleRate, 300.0, 3400.0);
    m_lowpass.create(m_taps, m_sampleRate, 3400.0);
    m_block.reserve(kCallBlock);
}

// Linear interpolation between the filtered input samples; the low pass in front keeps the
// band below the new Nyquist frequency.
void RxAudioChain::reset()
{
    // creating the filters again clears their history
    m_bandpass.create(m_taps, m_sampleRate, 300.0, 3400.0);
    m_lowpass.create(m_taps, m_sampleRate, 3400.0);
    m_next = m_ratio;
    m_previous = 0.0;
    m_block.clear();
}

void RxAudioChain::process(const float* samples, int length, int muted)
{
    const bool deliver = static_cast<bool>(m_sink);

    for (int i = 0; i < length; i++)
    {
        const double x = (i < muted || !std::isfinite(samples[i])) ? 0.0 : samples[i];
        double y = m_bandpass.filter(static_cast<Real>(x));
        // Noise gate, as osmocom-analog's NMT and AMPS (37fe472): what the phone's microphone
        // picks up when nobody speaks (the room, its own earpiece), lifted by the phone's
        // compressor, stays below the threshold and does not go to the network; above it the
        // threshold is taken off and made up again.
        const double a = std::fabs(y);
        y = (a < kRxNoiseGate) ? 0.0 : ((y >= 0.0) ? 1.0 : -1.0) * (a - kRxNoiseGate) / (1.0 - kRxNoiseGate);
        y = m_lowpass.filter(static_cast<Real>(y));

        while (m_next <= 1.0)
        {
            if (deliver)
            {
                m_block.push_back(static_cast<float>(m_previous + (y - m_previous) * m_next));
                if (m_block.size() == static_cast<size_t>(kCallBlock))
                {
                    if (m_sink) {
                        m_sink(m_block.data(), kCallBlock);
                    }
                    m_block.clear();
                }
            }
            m_next += m_ratio;
        }
        m_next -= 1.0;
        m_previous = y;
    }
}

SpeechLimiter::SpeechLimiter(double sampleRate, bool supervisory) :
    m_emphasis(sampleRate),
    m_limit((kMaxDeviation - (supervisory ? kSupervisoryDeviation : 0.0)) / kSpeechDeviationHz),
    m_limited(0)
{
}

void SpeechLimiter::process(float* samples, int length)
{
    if (length <= 0) {
        return;
    }

    // the peak after the pre-emphasis, with the state of the filter as it is
    m_probe.assign(samples, samples + length);
    Emphasis probe = m_emphasis;
    probe.pre(m_probe.data(), length);
    double peak = 0.0;
    for (float v : m_probe) {
        peak = std::max(peak, static_cast<double>(std::fabs(v)));
    }

    if (peak > m_limit)
    {
        const float scale = static_cast<float>(m_limit / peak);

        for (int i = 0; i < length; i++) {
            samples[i] *= scale;
        }
        m_limited++;
    }

    // the filter goes on with what is sent
    m_probe.assign(samples, samples + length);
    m_emphasis.pre(m_probe.data(), length);
}

TxAudioSource::TxAudioSource(double sampleRate) :
    m_step(static_cast<Real>(kCallRate / sampleRate)),
    m_remain(0.0f),
    m_block(static_cast<size_t>(kCallBlock), 0.0f),
    m_blockPos(static_cast<size_t>(kCallBlock))
{
    m_interpolator.create(16, kCallRate, 3400.0);
}

void TxAudioSource::reset()
{
    m_blockPos = static_cast<size_t>(kCallBlock);
    m_remain = 0.0f;
    m_interpolator.create(16, kCallRate, 3400.0);       // clears the history
}

double TxAudioSource::nextCallSample()
{
    if (m_blockPos == static_cast<size_t>(kCallBlock))
    {
        std::fill(m_block.begin(), m_block.end(), 0.0f);
        if (m_pull) {
            m_pull(m_block.data(), kCallBlock);
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

} // namespace modemnmt
