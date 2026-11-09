///////////////////////////////////////////////////////////////////////////////////
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

#ifndef MODEMCELLULAR_TONEGENERATOR_H_
#define MODEMCELLULAR_TONEGENERATOR_H_

#include <cstdint>

#include "export.h"

namespace modemcellular
{

// Call progress tones the network plays to a mobile.
enum class Tone
{
    Off,
    Ringback,
    Busy,
    Congestion
};

// Tone region. It follows the standard of the base station: NMT uses Denmark, AMPS uses
// American, TACS uses Uk, JTACS uses Japan.
//
// Frequencies and cadences are the published values of ITU-T E.180, "Various tones used
// in national networks" (ITU Operational Bulletin No. 781, position on 1 February 2003).
//
//   region    ringback                         busy                  congestion
//   American  440+480 Hz, 2 s on, 4 s off      480+620 Hz, 0.5/0.5   480+620 Hz, 0.3 on, 0.2 off
//   Denmark   425 Hz, 1 s on, 4 s off          425 Hz, 0.25/0.25     425 Hz, 0.25/0.25
//   Uk        400+450 Hz, 0.4 on, 0.2 off,     400 Hz, 0.375/0.375   400 Hz, 0.4 on, 0.35 off,
//             0.4 on, 2 s off                                        0.225 on, 0.525 off
//   Japan     400 Hz modulated at 16 Hz,       400 Hz, 0.5/0.5       as busy (the list has none)
//             1 s on, 2 s off
enum class ToneSet
{
    American,
    Denmark,
    Uk,
    Japan
};

// Generates one tone at a time as mono float at 8 kHz, peak level 0.25. Edges of each
// burst have a 5 ms ramp. Not thread safe.
class MODEMCELLULAR_API ToneGenerator
{
public:
    explicit ToneGenerator(ToneSet toneSet);

    // Starts the tone from the beginning of its cadence. Tone::Off gives silence.
    void setTone(Tone tone);
    Tone tone() const { return m_tone; }
    // Writes count samples; silence if no tone is set.
    void read(float* samples, int count);

private:
    ToneSet m_toneSet;
    Tone m_tone;
    int64_t m_pos;          // position in the cadence cycle, in samples
    int64_t m_total;        // samples since the tone was set, drives the modulation
    double m_phase1;        // radians
    double m_phase2;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_TONEGENERATOR_H_
