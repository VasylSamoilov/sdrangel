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

// Level of a frequency in the window, the peak value of the tone (Goertzel with a Hamming window).
double goertzel(const std::vector<float>& samples, const std::vector<double>& hamming, double coeff)
{
    const size_t length = samples.size();
    double sk = 0.0, sk1 = 0.0, sk2 = 0.0;

    for (size_t n = 0; n < length; n++)
    {
        sk = coeff * sk1 - sk2 + samples[n] * hamming[n * kHammingPoints / length];
        sk2 = sk1;
        sk1 = sk;
    }

    return std::sqrt(sk * sk - coeff * sk * sk2 + sk2 * sk2) / static_cast<double>(length) * 4.0 / 1.08;
}

} // namespace

SupervisoryDetector::SupervisoryDetector(double sampleRate, int number) :
    m_enabled(validNumber(number)),
    m_filled(0),
    m_coeffTone(0.0),
    m_coeffNoise(0.0),
    m_hamming(kHammingPoints),
    m_detected(false),
    m_count(0)
{
    if (!m_enabled) {
        return;
    }

    m_window.resize(static_cast<size_t>(sampleRate * kWindowSeconds + 0.5));
    m_coeffTone = 2.0 * std::cos(2.0 * kPi * kSupervisoryHz[number - 1] / sampleRate);
    m_coeffNoise = 2.0 * std::cos(2.0 * kPi * kSupervisoryNoiseHz / sampleRate);
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
    const double tone = goertzel(m_window, m_hamming, m_coeffTone);
    const double noise = goertzel(m_window, m_hamming, m_coeffNoise);
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
    m_step(m_enabled ? kSupervisoryHz[number - 1] / sampleRate : 0.0),
    m_level(level * kTxPeakSupervisory),
    m_phase(0.0)
{
}

void SupervisoryTone::add(float* samples, int length)
{
    if (!m_enabled) {
        return;
    }

    for (int i = 0; i < length; i++)
    {
        samples[i] += static_cast<float>(std::sin(2.0 * kPi * m_phase) * m_level);
        m_phase += m_step;
        m_phase -= std::floor(m_phase);
    }
}

RxAudioChain::RxAudioChain(double sampleRate, int supervisory) :
    m_ratio(sampleRate / kCallRate),
    m_highpass(ButterworthFilter::Type::Highpass, 300.0, sampleRate, 8),
    m_notch(validNumber(supervisory) ? kSupervisoryHz[supervisory - 1] : 1000.0, sampleRate, 2, 20.0),
    // a notch at or above the Nyquist frequency would be unstable
    m_useNotch(validNumber(supervisory) && kSupervisoryHz[supervisory - 1] < 0.45 * sampleRate),
    m_lowpass(ButterworthFilter::Type::Lowpass, 3400.0, sampleRate, 16),
    m_next(m_ratio),
    m_previous(0.0)
{
    m_block.reserve(kCallBlock);
}

// Linear interpolation between the filtered input samples; the low pass in front keeps the
// band below the new Nyquist frequency.
void RxAudioChain::reset()
{
    m_highpass.reset();
    m_notch.reset();
    m_lowpass.reset();
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
        double y = m_highpass.process(x);
        if (m_useNotch) {
            y = m_notch.process(y);
        }
        y = m_lowpass.process(y);

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

TxAudioSource::TxAudioSource(double sampleRate) :
    m_step(kCallRate / sampleRate),
    m_lowpass(ButterworthFilter::Type::Lowpass, 3600.0, sampleRate, 8),
    m_block(static_cast<size_t>(kCallBlock), 0.0f),
    m_blockPos(static_cast<size_t>(kCallBlock)),
    m_p{0.0, 0.0, 0.0, 0.0},
    m_pos(0.0),
    m_started(false)
{
}

void TxAudioSource::reset()
{
    m_blockPos = static_cast<size_t>(kCallBlock);
    m_p[0] = m_p[1] = m_p[2] = m_p[3] = 0.0;
    m_pos = 0.0;
    m_started = false;
    m_lowpass.reset();
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
    if (!m_started)
    {
        m_p[0] = 0.0;
        m_p[1] = 0.0;
        m_p[2] = nextCallSample();
        m_p[3] = nextCallSample();
        m_pos = 0.0;
        m_started = true;
    }

    for (int i = 0; i < length; i++)
    {
        // cubic interpolation (Catmull-Rom) between the two middle samples; a straight line
        // would cut the upper part of the speech band by several dB
        const double t = m_pos;
        const double interpolated = m_p[1] + 0.5 * t * (m_p[2] - m_p[0]
            + t * (2.0 * m_p[0] - 5.0 * m_p[1] + 4.0 * m_p[2] - m_p[3]
            + t * (3.0 * (m_p[1] - m_p[2]) + m_p[3] - m_p[0])));
        const double value = m_lowpass.process(interpolated) * gain;
        if (add) {
            samples[i] += static_cast<float>(value);
        } else {
            samples[i] = static_cast<float>(value);
        }

        m_pos += m_step;
        while (m_pos >= 1.0)
        {
            m_pos -= 1.0;
            m_p[0] = m_p[1];
            m_p[1] = m_p[2];
            m_p[2] = m_p[3];
            m_p[3] = nextCallSample();
        }
    }
}

} // namespace modemnmt
