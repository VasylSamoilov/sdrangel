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

#ifndef MODEMAMPS_AMPSVOICE_H_
#define MODEMAMPS_AMPSVOICE_H_

#include <array>
#include <functional>
#include <utility>
#include <vector>

#include "dsp/goertzel.h"
#include "dsp/nco.h"
#include "export.h"

// The tones of an AMPS voice channel (TIA/EIA-553-A 3.4.1, 2.4.1): the supervisory audio tone (SAT,
// 5970, 6000 or 6030 Hz, 2 kHz deviation) the base station sends and the phone transponds, and the
// signalling tone (ST, 10 kHz, 8 kHz deviation) of the phone. As osmocom-analog's amps (dsp.c
// sat_encode, sat_decode); the audio side is the deviation in Hz.
namespace modemamps
{

constexpr double kSatDeviation = 2000.0;           // Hz
constexpr double kSignallingToneHz = 10000.0;

// The SAT towards the phone, SDRangel's oscillator.
class MODEMAMPS_API SatTone
{
public:
    // scc 0..2; another value switches the tone off
    SatTone(double sampleRate, int scc);

    bool enabled() const { return m_enabled; }
    // adds the tone to the samples
    void add(float* samples, int length);

private:
    bool m_enabled;
    NCO m_nco;
};

// Detects the SAT the phone transponds and its signalling tone, with SDRangel's Goertzel filters
// over windows of 1/15 s (30 Hz wide: the SAT frequencies are apart, the noise at 5790 Hz is
// measured next to them) with osmocom's Hamming window. A window has a SAT when one of the three is
// 6 dB above the other two, clearly above the noise (quality over 0.85) and at half its deviation
// (0.35 once it is there); a change counts after 5 windows (333 ms) of the new state. The signalling
// tone counts at quality 0.8 and a fifth of the FSK deviation, on after 6 windows, off after 4.
class MODEMAMPS_API SatDetector
{
public:
    // the SAT of the phone changed: its colour code 0..2, or -1 for lost; the quality of the window
    using SatSink = std::function<void(int scc, double quality)>;
    // every window while the SAT is there: its quality (osmocom: amps_rx_sat_level, for power control)
    using QualitySink = std::function<void(double quality)>;
    using ToneSink = std::function<void(bool on, double quality)>;

    explicit SatDetector(double sampleRate);

    void setSatSink(SatSink sink) { m_satSink = std::move(sink); }
    void setQualitySink(QualitySink sink) { m_qualitySink = std::move(sink); }
    void setToneSink(ToneSink sink) { m_toneSink = std::move(sink); }
    // the deviation in Hz, after the DC filter
    void process(const float* samples, int length);
    // forget the state, so that a tone that is there is detected again
    void reset();
    int sat() const { return m_state; }
    bool tone() const { return m_tone; }

private:
    void decode();
    double amplitude(Goertzel& goertzel);

    SatSink m_satSink;
    QualitySink m_qualitySink;
    ToneSink m_toneSink;
    std::vector<float> m_window;
    size_t m_filled;
    std::vector<double> m_hamming;
    std::array<Goertzel, 5> m_goertzel;     // 5970, 6000, 6030, the noise at 5790, the ST
    int m_state;                            // -1 no SAT, else its colour code
    int m_pending;
    int m_pendingCount;
    bool m_tone;
    int m_toneCount;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSVOICE_H_
