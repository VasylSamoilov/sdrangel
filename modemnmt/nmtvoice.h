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

#ifndef MODEMNMT_NMTVOICE_H_
#define MODEMNMT_NMTVOICE_H_

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "dsp/firfilter.h"
#include "dsp/goertzel.h"
#include "dsp/interpolator.h"

#include "biquad.h"
#include "emphasis.h"
#include "export.h"

namespace modemnmt
{

// The speech path of a traffic channel: audio of the call is 8 kHz mono float, the channel runs
// at any sample rate from 8 kHz.
constexpr int kCallRate = 8000;
// Samples of call audio per block handed over
constexpr int kCallBlock = 160;

// Supervisory tones, 30 Hz apart, plus a frequency below them to measure the noise
// (NMT Doc 450-1 para 4.5 and 900-1 para 4.5; the 450-1 print of the first tone is a scan error).
constexpr double kSupervisoryHz[4] = {3955.0, 3985.0, 4015.0, 4045.0};
constexpr double kSupervisoryNoiseHz = 3895.0;
// Deviation of the supervisory tone: 0.3 kHz at 4015 Hz, which after pre-emphasis equals this
// fraction of the speech deviation (3 kHz at 1 kHz).
constexpr double kTxPeakSupervisory = 300.0 / 4015.0 * 1000.0 / 3000.0;
// The largest deviation of the transmitter (4.7 kHz), the speech deviation (3 kHz at 1 kHz) and
// the deviation of the supervisory tone on air (NMT Doc 450-4: "the supervisory signal (4000 Hz +
// 0,3 kHz deviation)"). The tone towards the phone is set so that it has this deviation after the
// pre-emphasis of the transmitter, whose low pass at 3.4 kHz gives far less than 6 dB/octave there
// (SupervisoryTone).
constexpr double kMaxDeviation = 4700.0;
constexpr double kSpeechDeviationHz = 3000.0;
constexpr double kSupervisoryDeviation = 300.0;

// Deviation limiter of the speech towards the radio, as the AMPS transmitter of the reference
// does it (TIA/EIA-553 2.1.3.1.3): the peak of each block after the pre-emphasis of the
// transmitter is measured, and a block that would go beyond the largest deviation, less the room
// of the supervisory tone, is scaled down as a whole (no clipping). The samples are at the speech
// level (1.0 = 3 kHz deviation at 1 kHz) before the pre-emphasis.
class MODEMNMT_API SpeechLimiter
{
public:
    // supervisory: the tone is sent with the speech and needs its room
    SpeechLimiter(double sampleRate, bool supervisory);

    void process(float* samples, int length);
    // the largest peak after the pre-emphasis, in speech level
    double limit() const { return m_limit; }
    // blocks that were scaled down so far
    uint64_t limited() const { return m_limited; }

private:
    Emphasis m_emphasis;            // the pre-emphasis of the transmitter, to measure the peak
    double m_limit;
    uint64_t m_limited;
    std::vector<float> m_probe;
};

// Detects one of the supervisory tones. The input is cut into windows of 1/15 s (the Goertzel
// filter is 30 Hz wide at that length, so the neighbouring tones are blocked); a window counts
// when the tone is clearly above the noise measured next to it. The tone is detected after 6
// windows in a row and lost after 4 without it.
class MODEMNMT_API SupervisoryDetector
{
public:
    // tone and quality (0..1) of a change
    using Sink = std::function<void(bool detected, double quality)>;

    // number is 1..4, other values switch the detector off
    SupervisoryDetector(double sampleRate, int number);

    void setSink(Sink sink) { m_sink = std::move(sink); }
    bool enabled() const { return m_enabled; }
    bool detected() const { return m_detected; }
    void process(const float* samples, int length);
    // Forget the state and the partial window, so that a tone that is there is detected again.
    void reset();

private:
    void decode();

    bool m_enabled;
    Sink m_sink;
    std::vector<float> m_window;
    size_t m_filled;
    Goertzel m_tone;                    // SDRangel's Goertzel filters, at the tone and the noise
    Goertzel m_noise;
    std::vector<double> m_hamming;      // 256 points as in the reference
    bool m_detected;
    int m_count;
};

// The supervisory tone towards the phone.
class MODEMNMT_API SupervisoryTone
{
public:
    // level scales the tone (the deviation factor of the transmitter)
    SupervisoryTone(double sampleRate, int number, double level);

    bool enabled() const { return m_enabled; }
    // adds the tone to the samples
    void add(float* samples, int length);

private:
    bool m_enabled;
    double m_step;          // cycles per sample
    double m_level;
    double m_phase;
};

// Threshold of the noise gate of the audio from the phone (1.0 = the reference level): 0.10 =
// 300 Hz deviation, -20 dB (osmocom-analog's NMT and AMPS).
constexpr double kRxNoiseGate = 0.10;

// Audio from the radio towards the call: band pass 300 Hz to 3.4 kHz (which takes the supervisory
// tone out, more than 100 dB down), the noise gate, low pass 3.4 kHz (the products of the gate
// and the band of the call), then down to 8 kHz in blocks of 160 samples. The filters are
// SDRangel's FIR filters as its NFM demodulator uses them ((rate / 48) | 1 taps, about 260 Hz
// wide edges, -6 dB at 300 Hz and 3.4 kHz, a delay of 10 ms each).
class MODEMNMT_API RxAudioChain
{
public:
    // The samples are valid during the call only; the sink may change them in place.
    using Sink = std::function<void(float* samples, int count)>;

    explicit RxAudioChain(double sampleRate);

    void setSink(Sink sink) { m_sink = std::move(sink); }
    // The first muted samples are taken as silence. Samples that are not finite are taken as
    // silence as well, so one bad sample does not poison the filters.
    void process(const float* samples, int length, int muted);
    // Clears the filters and the partial block.
    void reset();

private:
    double m_sampleRate;
    int m_taps;
    double m_ratio;                     // input samples per output sample
    Bandpass<Real> m_bandpass;
    Lowpass<Real> m_lowpass;
    Sink m_sink;
    double m_next;                      // time of the next output, relative to the previous input
    double m_previous;
    std::vector<float> m_block;
};

// Audio from the call towards the radio: 8 kHz to the channel rate, pulled in blocks of 160. The
// resampler is SDRangel's polyphase Interpolator as its DSD demodulator uses it from 8 kHz (16
// phases, 3.4 kHz): flat to 3 kHz, the images more than 63 dB down.
class MODEMNMT_API TxAudioSource
{
public:
    using Pull = std::function<void(float* samples, int count)>;

    explicit TxAudioSource(double sampleRate);
    // the Interpolator holds its taps by pointer and must not be copied
    TxAudioSource(const TxAudioSource&) = delete;
    TxAudioSource& operator=(const TxAudioSource&) = delete;

    void setPull(Pull pull) { m_pull = std::move(pull); }
    // Writes length samples (replace, or add), scaled by gain
    void generate(float* samples, int length, bool add, double gain);
    void reset();

private:
    double nextCallSample();

    Real m_step;                        // call samples per output sample
    Interpolator m_interpolator;
    Real m_remain;                      // position of the output after the last call sample taken
    Pull m_pull;
    std::vector<float> m_block;
    size_t m_blockPos;
};

} // namespace modemnmt

#endif // MODEMNMT_NMTVOICE_H_
