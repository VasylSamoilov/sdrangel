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
#include <numeric>

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
    m_level(m_enabled ? level * kSupervisoryDeviation / kSpeechDeviationHz / modemcellular::Emphasis::preGain(kSupervisoryHz[number - 1], sampleRate) : 0.0)
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

namespace
{

// the band pass runs at twice the call's rate, the gate at osmocom-analog's 48 kHz
const double kBandRate = 2.0 * kCallRate;
const double kGateRate = 6.0 * kCallRate;
// The resampler to 16 kHz: only what would fold into the band has to go (from 12.6 kHz up, at
// 16 kHz), the band edge is made after it.
const double kResamplerCutoff = 6000.0;
// The band pass at 16 kHz: edges 250 Hz wide (Blackman: 5.5 / width taps), -6 dB at 175 Hz and
// 3650 Hz, so 300 Hz to 3.4 kHz is flat and the supervisory tones (from 3955 Hz) are gone.
const double kBandLow = 175.0;
const double kBandHigh = 3650.0;
const int kBandTaps = static_cast<int>(5.5 * kBandRate / 250.0) | 1;
// Up to 48 kHz for the gate: the band is below 3.65 kHz, its images (from 12.35 kHz) go; the
// edge of the short filter (about 2 kHz wide) stays above the band.
const double kUpCutoff = 5000.0;
// From 48 kHz to 8 kHz after the gate: flat to 3.4 kHz, the gate's products gone from about
// 4.6 kHz, so they do not fold into the band; computed for the 8 kHz samples only, so the
// filter can be long (192 taps per phase, Hamming).
const double kDownCutoff = 4000.0;
const double kDownTapsPerPhase = 192.0 / kInterpolatorPhases;

} // namespace

RxAudioChain::RxAudioChain(double sampleRate) :
    m_sampleRate(sampleRate),
    m_distance(static_cast<Real>(sampleRate / kBandRate)),
    m_remain(0.0f),
    m_upRemain(0.0f),
    m_downRemain(0.0f)
{
    create();
    m_block.reserve(kCallBlock);
}

void RxAudioChain::create()
{
    // below 16 kHz the resampler goes up, its band is what the input has
    const double cutoff = std::min(kResamplerCutoff, 0.45 * std::min(m_sampleRate, kBandRate));

    m_resampler.create(kInterpolatorPhases, m_sampleRate, cutoff, kInterpolatorTapsPerPhase);
    m_bandpass.create(kBandTaps, kBandRate, kBandLow, kBandHigh);
    m_up.create(kInterpolatorPhases, kBandRate, kUpCutoff, kInterpolatorTapsPerPhase);
    m_down.create(kInterpolatorPhases, kGateRate, kDownCutoff, kDownTapsPerPhase);
}

void RxAudioChain::reset()
{
    create();                           // creating the filters again clears their history
    m_remain = 0.0f;
    m_upRemain = 0.0f;
    m_downRemain = 0.0f;
    m_block.clear();
}

// One sample at 16 kHz: the band pass, then up to 48 kHz.
void RxAudioChain::filtered(double sample)
{
    const Complex in(m_bandpass.filter(static_cast<Real>(sample)), 0.0f);
    Complex out;
    const Real step = static_cast<Real>(kBandRate / kGateRate);

    while (!m_up.interpolate(&m_upRemain, in, &out))
    {
        gated(out.real());
        m_upRemain += step;
    }
}

// One sample at 48 kHz: the gate, then down to 8 kHz.
void RxAudioChain::gated(double y)
{
    // Noise gate, as osmocom-analog's NMT and AMPS (37fe472), at its rate (48 kHz), so that its
    // products are those of osmocom-analog: what the phone's microphone picks up when nobody
    // speaks (the room, its own earpiece), lifted by the phone's compressor, stays below the
    // threshold and does not go to the network; above it the threshold is taken off and made up
    // again.
    const double a = std::fabs(y);
    y = (a < kRxNoiseGate) ? 0.0 : ((y >= 0.0) ? 1.0 : -1.0) * (a - kRxNoiseGate) / (1.0 - kRxNoiseGate);

    Complex out;
    if (!m_down.decimate(&m_downRemain, Complex(static_cast<Real>(y), 0.0f), &out)) {
        return;
    }
    m_downRemain += static_cast<Real>(kGateRate / kCallRate);
    if (!m_sink) {
        return;
    }
    m_block.push_back(out.real());
    if (m_block.size() == static_cast<size_t>(kCallBlock))
    {
        m_sink(m_block.data(), kCallBlock);
        m_block.clear();
    }
}

void RxAudioChain::process(const float* samples, int length, int muted)
{
    for (int i = 0; i < length; i++)
    {
        const Real x = (i < muted || !std::isfinite(samples[i])) ? 0.0f : samples[i];
        const Complex in(x, 0.0f);
        Complex out;

        // as SDRangel's DSD demodulator drives the Interpolator
        if (m_distance < 1.0f)
        {
            while (!m_resampler.interpolate(&m_remain, in, &out))
            {
                filtered(out.real());
                m_remain += m_distance;
            }
        }
        else if (m_resampler.decimate(&m_remain, in, &out))
        {
            filtered(out.real());
            m_remain += m_distance;
        }
    }
}

SpeechLimiter::SpeechLimiter(double sampleRate, bool supervisory, double gain) :
    m_sampleRate(sampleRate),
    m_emphasis(sampleRate),
    m_limit((kMaxDeviation - (supervisory ? kSupervisoryDeviation : 0.0)) / kSpeechDeviationHz
        / (gain > 0.0 ? gain : 1.0) * kMargin),
    m_release(std::pow(10.0, kReleaseDbPerMs / 20.0 * 1000.0 / sampleRate)),
    m_delay(std::max(1, static_cast<int>(sampleRate * kLookaheadMs / 1000.0 + 0.5))),
    m_x(static_cast<size_t>(m_delay + 1)),
    m_r(static_cast<size_t>(m_delay + 1)),
    m_m(static_cast<size_t>(m_delay + 1)),
    m_dq(static_cast<size_t>(m_delay + 2)),
    m_limited(0)
{
    reset();
}

void SpeechLimiter::reset()
{
    std::fill(m_x.begin(), m_x.end(), 0.0f);
    std::fill(m_r.begin(), m_r.end(), 1.0);
    std::fill(m_m.begin(), m_m.end(), 1.0);
    m_dqHead = m_dqTail = 0;
    m_pos = 0;
    m_mSum = static_cast<double>(m_delay + 1);
    m_gain = 1.0;
    m_emphasis = modemcellular::Emphasis(m_sampleRate);
}

void SpeechLimiter::process(float* samples, int length)
{
    if (length <= 0) {
        return;
    }

    const int n = m_delay + 1;
    const int dqn = m_delay + 2;
    double lowest = 1.0;

    // the needed gains come from the pre-emphasized input
    m_probe.assign(samples, samples + length);
    m_emphasis.pre(m_probe.data(), length);

    for (int i = 0; i < length; i++)
    {
        const float in = samples[i];
        const double e = std::fabs(m_probe[static_cast<size_t>(i)]);
        const int64_t p = m_pos;
        const int slot = static_cast<int>(p % n);
        const double r = e > m_limit ? m_limit / e : 1.0;

        // the new sample enters, the one of position p - delay leaves
        m_x[static_cast<size_t>(slot)] = in;
        m_r[static_cast<size_t>(slot)] = r;
        const float out = m_x[static_cast<size_t>((slot + 1) % n)];

        // running minimum of r over the positions p - delay .. p (a monotonic deque)
        while (m_dqTail != m_dqHead)
        {
            const int last = (m_dqTail + dqn - 1) % dqn;
            if (m_r[static_cast<size_t>(m_dq[static_cast<size_t>(last)] % n)] < r) {
                break;
            }
            m_dqTail = last;
        }
        m_dq[static_cast<size_t>(m_dqTail)] = p;
        m_dqTail = (m_dqTail + 1) % dqn;
        while (m_dq[static_cast<size_t>(m_dqHead)] < p - m_delay) {
            m_dqHead = (m_dqHead + 1) % dqn;
        }
        const double m = m_r[static_cast<size_t>(m_dq[static_cast<size_t>(m_dqHead)] % n)];

        // smoothed over the lookahead (the sum is recomputed once per round against drift)
        m_mSum += m - m_m[static_cast<size_t>(slot)];
        m_m[static_cast<size_t>(slot)] = m;
        if (slot == n - 1) {
            m_mSum = std::accumulate(m_m.begin(), m_m.end(), 0.0);
        }
        double g = m_mSum / static_cast<double>(n);
        g = std::min(g, m_gain * m_release);
        g = std::min(g, 1.0);
        m_gain = g;
        lowest = std::min(lowest, g);
        samples[i] = static_cast<float>(out * g);
        m_pos++;
    }

    // a call counts as limited from -0.01 dB on
    if (lowest < 0.999) {
        m_limited++;
    }
}

} // namespace modemnmt
