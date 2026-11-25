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

#include "ampsspeech.h"

#include <algorithm>
#include <cmath>

#include "calltypes.h"

namespace modemamps
{

namespace
{

const double kSatHz = 6000.0;
const double kSatNotchQ = 10.0;
// SDRangel's Interpolator from the channel rate to 8 kHz, as modemnmt's chain takes it down
const int kPhases = 96;
const double kDownCutoff = 4000.0;
const double kDownTapsPerPhase = 192.0 / kPhases;
// osmocom: the pre-emphasis shelf at 8 kHz, and its gain at 1 kHz taken out
const double kPreemphasisTau = 318.3e-6;
const double kPreemphasisHigh = 2000.0;
const double kPreemphasisScale = 0.5;
const double kLookaheadMs = 4.0;
const double kKneeEmphasisHz = 500.0;

} // namespace

AmpsRxSpeech::AmpsRxSpeech(double sampleRate) :
    m_sampleRate(sampleRate),
    m_highpass(300.0, sampleRate, 1),
    m_notch(kSatHz, sampleRate, 2, kSatNotchQ),
    m_distance(static_cast<Real>(sampleRate / modemcellular::kAudioSampleRate)),
    m_remain(0.0f),
    m_emphasis(modemcellular::kAudioSampleRate),
    m_voiceHighpass(300.0, modemcellular::kAudioSampleRate, 2),
    m_voiceLowpass(3400.0, modemcellular::kAudioSampleRate, 2),
    m_compandor(modemcellular::kAudioSampleRate, 3.0, 13.5)
{
    m_down.create(kPhases, sampleRate, kDownCutoff, kDownTapsPerPhase);
    m_block.reserve(modemcellular::TxAudioSource::kBlock);
}

void AmpsRxSpeech::reset()
{
    m_highpass.reset();
    m_notch.reset();
    m_down.create(kPhases, m_sampleRate, kDownCutoff, kDownTapsPerPhase);
    m_remain = 0.0f;
    m_emphasis = modemcellular::Emphasis(modemcellular::kAudioSampleRate);
    m_voiceHighpass.reset();
    m_voiceLowpass.reset();
    m_compandor.reset();
    m_block.clear();
}

void AmpsRxSpeech::process(const float* samples, int length)
{
    for (int i = 0; i < length; i++)
    {
        double x = std::isfinite(samples[i]) ? samples[i] / kSpeechDeviation : 0.0;

        x = m_notch.process(m_highpass.process(x));
        // the noise gate, above it the threshold taken off and made up again (osmocom)
        const double a = std::fabs(x);
        x = (a < kRxNoiseGate) ? 0.0 : ((x >= 0.0) ? 1.0 : -1.0) * (a - kRxNoiseGate) / (1.0 - kRxNoiseGate);

        Complex out;
        if (m_down.decimate(&m_remain, Complex(static_cast<Real>(x), 0.0f), &out))
        {
            m_remain += m_distance;
            callSample(out.real());
        }
    }
}

void AmpsRxSpeech::callSample(double sample)
{
    m_block.push_back(static_cast<float>(sample));
    if (m_block.size() < static_cast<size_t>(modemcellular::TxAudioSource::kBlock)) {
        return;
    }

    float* s = m_block.data();
    const int n = static_cast<int>(m_block.size());

    m_emphasis.de(s, n);
    for (int i = 0; i < n; i++) {
        s[i] = static_cast<float>(m_voiceLowpass.process(m_voiceHighpass.process(s[i])));
    }
    if (m_meter) {
        m_meter->process(s, n);         // as on the air, before the expander (as NMT's)
    }
    m_compandor.expand(s, n);
    if (m_sink) {
        m_sink(s, n);
    }
    m_block.clear();
}

AmpsTxSpeech::AmpsTxSpeech(double sampleRate) :
    m_compandor(modemcellular::kAudioSampleRate, 3.0, 13.5),
    m_preemphasis(modemcellular::kAudioSampleRate, static_cast<Real>(kPreemphasisTau), static_cast<Real>(kPreemphasisHigh)),
    m_source(sampleRate),
    m_limited(0)
{
    m_compandor.setCompressorLookahead(kLookaheadMs);
    m_compandor.setCompressorKnee(std::pow(10.0, kTxKneeDb / 20.0), kKneeEmphasisHz);
    m_source.setPull([this](float* samples, int count) { pull(samples, count); });
}

void AmpsTxSpeech::reset()
{
    m_compandor.reset();
    m_preemphasis.configure(modemcellular::kAudioSampleRate, static_cast<Real>(kPreemphasisTau), static_cast<Real>(kPreemphasisHigh));
    m_source.reset();
}

// 8 kHz from the call: gain, compressor, pre-emphasis
void AmpsTxSpeech::pull(float* samples, int count)
{
    std::fill(samples, samples + count, 0.0f);
    if (m_pull) {
        m_pull(samples, count);
    }

    const double gain = std::pow(10.0, kTxGainDb / 20.0);
    for (int i = 0; i < count; i++) {
        samples[i] = std::isfinite(samples[i]) ? static_cast<float>(samples[i] * gain) : 0.0f;
    }
    m_compandor.compress(samples, count);
    if (m_meter) {
        m_meter->process(samples, count);   // as on the air, after the compressor (as NMT's)
    }
    for (int i = 0; i < count; i++) {
        samples[i] = m_preemphasis.filter(static_cast<Real>(samples[i] * kPreemphasisScale));
    }
}

void AmpsTxSpeech::generate(float* samples, int length)
{
    m_source.generate(samples, length, false, 1.0);

    // the voice limit, the block scaled down as a whole (osmocom)
    const double limit = kVoiceMaxDeviation / kSpeechDeviation;
    double peak = 0.0;

    for (int i = 0; i < length; i++) {
        peak = std::max(peak, static_cast<double>(std::fabs(samples[i])));
    }
    const double scale = (peak > limit) ? limit / peak : 1.0;
    if (peak > limit) {
        m_limited++;
    }
    for (int i = 0; i < length; i++) {
        samples[i] = static_cast<float>(samples[i] * scale * kSpeechDeviation);
    }
}

} // namespace modemamps
