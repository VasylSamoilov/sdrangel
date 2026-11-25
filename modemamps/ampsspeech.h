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

#ifndef MODEMAMPS_AMPSSPEECH_H_
#define MODEMAMPS_AMPSSPEECH_H_

#include <functional>
#include <utility>
#include <vector>

#include "biquad.h"
#include "compandor.h"
#include "dsp/fmpreemphasis.h"
#include "dsp/interpolator.h"
#include "emphasis.h"
#include "speechmeter.h"
#include "txaudiosource.h"
#include "export.h"

// The speech of an AMPS voice channel, osmocom-analog's tuned path (amps/dsp.c sender_send and
// sender_receive_audio), on SDRangel's and modemcellular's primitives. The radio side is the
// frequency deviation in Hz at the channel rate, the call side 8 kHz with 1.0 the speech level:
// 2.9 kHz deviation at 1 kHz.
namespace modemamps
{

constexpr double kSpeechDeviation = 2900.0;         // Hz at 1 kHz
// TIA/EIA-553-A 3.1.3.1.3: the voice is limited to 12 kHz deviation, the SAT not counted (osmocom)
constexpr double kVoiceMaxDeviation = 12000.0;
constexpr double kTxGainDb = 3.0;                   // osmocom: the speech gain before the compressor
constexpr double kTxKneeDb = 8.0;                   // osmocom: the knee of the compressor
constexpr double kRxNoiseGate = 0.10;               // osmocom: the noise gate of the received audio

// From the radio to the call: at the channel rate a 300 Hz high pass, the notch of the SAT (6000 Hz,
// Q 10, 2 sections) and the noise gate; down to 8 kHz (SDRangel's Interpolator); there the DC filter
// and de-emphasis, the voice band (300 Hz and 3.4 kHz, 2 sections each) and the expander.
class MODEMAMPS_API AmpsRxSpeech
{
public:
    using Sink = std::function<void(float* samples, int count)>;

    explicit AmpsRxSpeech(double sampleRate);
    AmpsRxSpeech(const AmpsRxSpeech&) = delete;
    AmpsRxSpeech& operator=(const AmpsRxSpeech&) = delete;

    void setSink(Sink sink) { m_sink = std::move(sink); }
    // the meter of the speech (null: none), fed at the level on the air: before the expander
    void setMeter(modemcellular::SpeechMeter* meter) { m_meter = meter; }
    // the deviation in Hz; blocks of 160 go to the sink
    void process(const float* samples, int length);
    void reset();

private:
    void callSample(double sample);

    modemcellular::SpeechMeter* m_meter = nullptr;
    double m_sampleRate;
    modemcellular::BiquadHighpass m_highpass;
    modemcellular::BiquadNotch m_notch;
    Interpolator m_down;
    Real m_distance;
    Real m_remain;
    modemcellular::Emphasis m_emphasis;
    modemcellular::BiquadHighpass m_voiceHighpass;
    modemcellular::BiquadLowpass m_voiceLowpass;
    modemcellular::Compandor m_compandor;
    Sink m_sink;
    std::vector<float> m_block;
};

// From the call to the radio: at 8 kHz the gain, the compressor (lookahead 4 ms, knee 8 dB at
// 500 Hz), the pre-emphasis (SDRangel's FMPreemphasis, 318.3 us to 2 kHz, halved to be neutral at
// 1 kHz); up to the channel rate (modemcellular's TxAudioSource); there a block whose peak is above
// the 12 kHz of the voice is scaled down to it (osmocom: per block sent). The SAT is not added.
class MODEMAMPS_API AmpsTxSpeech
{
public:
    using Pull = modemcellular::TxAudioSource::Pull;

    explicit AmpsTxSpeech(double sampleRate);
    AmpsTxSpeech(const AmpsTxSpeech&) = delete;
    AmpsTxSpeech& operator=(const AmpsTxSpeech&) = delete;

    void setPull(Pull pull) { m_pull = std::move(pull); }
    // the meter of the speech (null: none), fed at the level on the air: after the compressor
    void setMeter(modemcellular::SpeechMeter* meter) { m_meter = meter; }
    // writes length samples of deviation in Hz
    void generate(float* samples, int length);
    void reset();
    // blocks the limiter scaled down so far
    uint64_t limited() const { return m_limited; }

private:
    void pull(float* samples, int count);

    modemcellular::SpeechMeter* m_meter = nullptr;
    modemcellular::Compandor m_compandor;
    FMPreemphasis m_preemphasis;
    modemcellular::TxAudioSource m_source;
    Pull m_pull;
    uint64_t m_limited;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSSPEECH_H_
