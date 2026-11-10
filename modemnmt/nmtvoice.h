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

#include <functional>
#include <utility>
#include <vector>

#include "biquad.h"
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
    double m_coeffTone;
    double m_coeffNoise;
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

// Audio from the radio towards the call: high pass 300 Hz, notch at the supervisory tone,
// low pass 3.4 kHz, then down to 8 kHz in blocks of 160 samples.
class MODEMNMT_API RxAudioChain
{
public:
    // The samples are valid during the call only; the sink may change them in place.
    using Sink = std::function<void(float* samples, int count)>;

    // supervisory 1..4 puts the notch on that tone (if it is below 0.45 of the sample rate),
    // other values leave it out
    RxAudioChain(double sampleRate, int supervisory);

    void setSink(Sink sink) { m_sink = std::move(sink); }
    // The first muted samples are taken as silence. Samples that are not finite are taken as
    // silence as well, so one bad sample does not poison the filters.
    void process(const float* samples, int length, int muted);
    // Clears the filters and the partial block.
    void reset();

private:
    double m_ratio;                     // input samples per output sample
    ButterworthFilter m_highpass;
    BiquadNotch m_notch;
    bool m_useNotch;
    ButterworthFilter m_lowpass;
    Sink m_sink;
    double m_next;                      // time of the next output, relative to the previous input
    double m_previous;
    std::vector<float> m_block;
};

// Audio from the call towards the radio: 8 kHz to the channel rate, pulled in blocks of 160.
class MODEMNMT_API TxAudioSource
{
public:
    using Pull = std::function<void(float* samples, int count)>;

    explicit TxAudioSource(double sampleRate);

    void setPull(Pull pull) { m_pull = std::move(pull); }
    // Writes length samples (replace, or add), scaled by gain
    void generate(float* samples, int length, bool add, double gain);
    void reset();

private:
    double nextCallSample();

    double m_step;                      // call samples per output sample
    ButterworthFilter m_lowpass;        // removes the images of the interpolation
    Pull m_pull;
    std::vector<float> m_block;
    size_t m_blockPos;
    double m_p[4];                      // call samples around the output position
    double m_pos;                       // 0..1 between m_p[1] and m_p[2]
    bool m_started;
};

} // namespace modemnmt

#endif // MODEMNMT_NMTVOICE_H_
