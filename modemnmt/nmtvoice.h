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
#include "dsp/nco.h"

#include "biquad.h"
#include "emphasis.h"
#include "txaudiosource.h"
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

// Speech limiter of the transmitter, osmocom-analog's NMT lookahead limiter (NMT Doc 900-4 4.8.1
// and 4.10: largest deviation 4.7 kHz; 4.9: the supervisory tone does not pass through it). The
// limit applies after the pre-emphasis: each input sample is pre-emphasized to find the gain it
// needs; the speech is delayed by the lookahead (2 ms), the gain is the running minimum of the
// needed gains over the lookahead, smoothed over it, and comes back by 1 dB per 10 ms. No
// clipping. The limit is the largest deviation less the supervisory tone's, divided by the gain
// the transmitter applies after the limiter, with a margin of 5% for what the smoothing does to
// the pre-emphasis. The samples are at the speech level (1.0 = 3 kHz deviation at 1 kHz) before
// the pre-emphasis.
class MODEMNMT_API SpeechLimiter
{
public:
    // supervisory: the tone is sent with the speech and needs its room; gain: what the
    // transmitter multiplies the speech by after the limiter
    SpeechLimiter(double sampleRate, bool supervisory, double gain = 1.0);

    void process(float* samples, int length);
    // a new stretch of speech starts without the old one in the lookahead
    void reset();
    // the largest peak after the pre-emphasis, in speech level
    double limit() const { return m_limit; }
    // calls of process() in which the gain went below 1 (-0.01 dB) so far
    uint64_t limited() const { return m_limited; }

    static constexpr double kMargin = 0.95;
    static constexpr double kLookaheadMs = 2.0;
    static constexpr double kReleaseDbPerMs = 1.0 / 10.0;

private:
    double m_sampleRate;
    modemcellular::Emphasis m_emphasis; // the pre-emphasis of the transmitter, to find the peaks
    double m_limit;
    double m_release;                   // factor per sample the gain may rise
    int m_delay;                        // the lookahead in samples
    std::vector<float> m_x;             // ring of the delayed input, delay + 1
    std::vector<double> m_r;            // ring of the gain each input sample needs
    std::vector<double> m_m;            // ring of the running minimum of m_r
    std::vector<int64_t> m_dq;          // monotonic deque of positions for the running minimum
    int m_dqHead;
    int m_dqTail;
    int64_t m_pos;
    double m_mSum;
    double m_gain;
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
    NCO m_nco;              // SDRangel's oscillator
    double m_level;
};

// SDRangel's Interpolator in the speech path. It takes the nearest of its phases (no
// interpolation between them), so the phases are a multiple of 6: from 8 to 48 kHz and from
// 48 to 16 kHz every output falls on a phase; at other rates the timing error is 1/96 sample.
// Interpolator::create() takes its "taps per phase" times the phases as the taps of each phase,
// so 24 taps per phase are 24 / 96. Measured at 48 kHz with 24 taps: folding into the band 62 dB
// down from 48 to 16 kHz, images 60 dB down from 8 to 48 kHz, 3 kHz -0.35 dB; 72 taps (the
// default) are 3.6 times the work for a few dB more.
constexpr int kInterpolatorPhases = 96;
constexpr double kInterpolatorTapsPerPhase = 24.0 / kInterpolatorPhases;

// Threshold of the noise gate of the audio from the phone (1.0 = the reference level): 0.10 =
// 300 Hz deviation, -20 dB (osmocom-analog's NMT and AMPS).
constexpr double kRxNoiseGate = 0.10;

// Audio from the radio towards the call, with SDRangel's resampler and FIR filters:
// - the channel rate to 16 kHz (Interpolator, any ratio; only the kept samples are computed);
// - at 16 kHz a band pass that keeps 300 Hz to 3.4 kHz flat and takes the supervisory tones
//   (NMT Doc 450-4 5.15: at least 30 dB at 4 kHz +-55 Hz) and the hum out before the gate;
// - up to 48 kHz, the noise gate there (its products then are osmocom-analog's: at 16 kHz they
//   folded into the band, 23 dB below a tone);
// - down to 8 kHz (Interpolator, the 8 kHz samples only), in blocks of 160.
// The sharp band edges are made at 16 kHz, a third of the work they would be at 48 kHz.
class MODEMNMT_API RxAudioChain
{
public:
    // The samples are valid during the call only; the sink may change them in place.
    using Sink = std::function<void(float* samples, int count)>;

    explicit RxAudioChain(double sampleRate);
    // the Interpolator holds its taps by pointer and must not be copied
    RxAudioChain(const RxAudioChain&) = delete;
    RxAudioChain& operator=(const RxAudioChain&) = delete;

    void setSink(Sink sink) { m_sink = std::move(sink); }
    // The first muted samples are taken as silence. Samples that are not finite are taken as
    // silence as well, so one bad sample does not poison the filters.
    void process(const float* samples, int length, int muted);
    // Clears the filters and the partial block.
    void reset();

private:
    void create();
    void filtered(double sample);       // one sample at 16 kHz
    void gated(double sample);          // one sample at 48 kHz

    double m_sampleRate;
    Interpolator m_resampler;           // the channel rate to 16 kHz
    Real m_distance;                    // input samples per 16 kHz sample
    Real m_remain;
    Bandpass<Real> m_bandpass;          // at 16 kHz
    Interpolator m_up;                  // 16 kHz to 48 kHz, for the gate
    Real m_upRemain;
    Interpolator m_down;                // 48 kHz to 8 kHz
    Real m_downRemain;
    Sink m_sink;
    std::vector<float> m_block;
};

// Audio from the call towards the radio: modemcellular's TxAudioSource (8 kHz to the channel rate).

} // namespace modemnmt

#endif // MODEMNMT_NMTVOICE_H_
