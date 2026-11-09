///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2017 Andreas Eversberg <jolly@eversberg.eu>                     //
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

#ifndef MODEMNMT_FFSK_H_
#define MODEMNMT_FFSK_H_

#include <functional>
#include <utility>

#include "biquad.h"
#include "export.h"

namespace modemnmt
{

// Fast frequency shift keying on an audio signal: each bit holds a whole number of half waves, so
// every symbol starts and ends at a zero crossing.

class MODEMNMT_API FfskModulator
{
public:
    // Returns the next bit (0 or 1), or -1 if there is nothing more to send.
    using BitSource = std::function<int()>;

    // level is the peak amplitude of the tone. f0 and f1 are the tones for bit 0 and bit 1.
    // valid() is false if the half waves of a tone do not fit exactly into one bit.
    FfskModulator(double sampleRate, double bitRate, double f0, double f1, double level);

    bool valid() const { return m_valid; }
    void setBitSource(BitSource source) { m_source = std::move(source); }

    // Renders up to length samples (replace, or add to the buffer). Returns fewer samples if the
    // source ran out of bits; the next call starts again with a clean phase.
    int render(float* samples, int length, bool add);
    void reset();

private:
    bool nextBit(double& phase);

    bool m_valid;
    BitSource m_source;
    double m_level;
    double m_bitsPerSample;
    double m_phaseShift[2];    // cycles per sample
    double m_cyclesPerBit[2];
    double m_phase;            // cycles, 0..1
    double m_bitPos;           // bits, 0..1
    int m_bit;                 // -1 if none
};

class MODEMNMT_API FfskDemodulator
{
public:
    // quality is 1.0 if the tone hit its nominal frequency, falling to 0.0 at the centre frequency
    // and at twice the deviation. level is the peak amplitude of the tone.
    using BitSink = std::function<void(int bit, double quality, double level)>;

    // bitAdjust is how far the bit clock is pulled towards the middle of a bit on a tone change.
    FfskDemodulator(double sampleRate, double bitRate, double f0, double f1, double bitAdjust);

    void setBitSink(BitSink sink) { m_sink = std::move(sink); }
    void process(const float* samples, int length);

private:
    BitSink m_sink;
    double m_sampleRate;
    double m_f0Deviation;
    double m_f1Deviation;
    int m_lowBit;
    int m_highBit;
    double m_rotation;         // cycles per sample of the mixer
    double m_mixPhase;         // cycles
    double m_lastPhase;        // radians
    BiquadLowpass m_lowpassI;
    BiquadLowpass m_lowpassQ;
    double m_bitsPerSample;
    double m_bitAdjust;
    double m_bitPos;           // bits
    int m_bit;                 // -1 if none
    bool m_change;
};

} // namespace modemnmt

#endif // MODEMNMT_FFSK_H_
