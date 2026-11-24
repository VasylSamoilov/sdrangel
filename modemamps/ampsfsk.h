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

#ifndef MODEMAMPS_AMPSFSK_H_
#define MODEMAMPS_AMPSFSK_H_

#include <functional>
#include <utility>
#include <vector>

#include "export.h"

// The Manchester coded FSK of the AMPS data channels (TIA/EIA-553-A 3.7.1, 2.7.1): 10 kbit/s, the
// frequency goes from low to high in the middle of a 1 and from high to low in the middle of a 0,
// 8 kHz deviation. As osmocom-analog's amps/dsp.c (it works with the phones); the audio side is the
// frequency deviation in Hz, at any sample rate from 48 kHz (the bit windows are taken between the
// samples, osmocom needs 96 kHz for whole samples).
namespace modemamps
{

constexpr double kFskBitRate = 10000.0;
constexpr double kFskDeviation = 8000.0;           // Hz

// Each half bit holds the frequency, or turns it over with half a cosine (osmocom's ramp).
class MODEMAMPS_API ManchesterModulator
{
public:
    // Returns the next bit (0 or 1), or -1 if there is nothing more to send.
    using BitSource = std::function<int()>;

    ManchesterModulator(double sampleRate, double bitRate, double deviation);

    void setBitSource(BitSource source) { m_source = std::move(source); }
    // Renders up to length samples (replace, or add to the buffer). Returns fewer samples if the
    // source ran out of bits (at the end of a bit); the frequency is kept for the next bits.
    int render(float* samples, int length, bool add);
    void reset();

private:
    BitSource m_source;
    double m_deviation;
    double m_halvesPerSample;
    double m_halfPos;          // half bits, 0..1
    int m_half;                // 0 the first half of the bit, 1 the second, -1 no bit
    double m_from;             // the frequency at the start of the half bit
    double m_to;               // and at its end
    double m_last;             // the frequency at the end of the last half bit
    int m_bit;
};

// The bit clock is set by the dotting (101010..: one zero crossing in every bit, one bit apart); in
// a frame it follows the crossings in the middle of the bits (osmocom runs it free). A bit is the mean of the quarter bit after its zero crossing minus the mean
// of the quarter bit before it: 1 if the frequency went up. While no frame is received, the dotting is
// looked for all the time, a better one sets the clock again; 12 bits after the last dotting the
// clock stops if no frame started.
class MODEMAMPS_API ManchesterDemodulator
{
public:
    // quality: the difference of the two quarter bits relative to twice the deviation, over
    // osmocom's best (0.68; a clean bit of this modulator is about 0.94); level: the swing of the
    // bit relative to twice the deviation
    using BitSink = std::function<void(int bit, double quality, double level)>;

    ManchesterDemodulator(double sampleRate, double bitRate, double deviation);

    void setBitSink(BitSink sink) { m_sink = std::move(sink); }
    void process(const float* samples, int length);
    // A frame is received: the clock runs on without looking for dotting (osmocom's sync state).
    void startFrame();
    // The frame is done (or there was none): the clock stops until the next dotting.
    void endFrame();
    bool clocked() const { return m_state != StateNone; }

    // The bits between the last 8 zero crossings, the newest first (7). True if each crossing is
    // within half a bit of one bit apart from the newest; phase is then the mean of their offsets:
    // how late the newest crossing came (osmocom divides their sum by 8, a mean of 7/8).
    static constexpr int kDottingIntervals = 7;
    static bool dottingPhase(const double* intervals, double& phase);
    // How far a crossing in the middle of a bit pulls the clock in a frame (0: free running, as
    // osmocom)
    static constexpr double kDefaultTrackGain = 0.1;
    void setTrackGain(double gain) { m_trackGain = gain; }

private:
    enum State { StateNone, StateDotting, StateFrame };

    void crossing(double elapsed, double sinceCrossing);
    void track(double position);
    void bit(double back);
    // the samples interpolated at back samples before the last one
    double at(double back) const;

    BitSink m_sink;
    double m_deviation;
    double m_samplesPerBit;
    double m_bitsPerSample;
    std::vector<float> m_window;            // the last samples, ring
    int m_pos;                              // the next place in m_window
    int m_count;                            // samples in m_window
    float m_last;
    double m_elapsed;                       // bits since the last zero crossing
    double m_dotting[256];                  // the bits between the last zero crossings, ring
    unsigned char m_dottingPos;
    double m_dottingAverage;
    int m_dottingLife;
    double m_bitCount;                      // bits, a bit is decided when it reaches 1
    double m_trackGain;
    State m_state;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSFSK_H_
