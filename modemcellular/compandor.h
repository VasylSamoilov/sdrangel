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

#ifndef MODEMCELLULAR_COMPANDOR_H_
#define MODEMCELLULAR_COMPANDOR_H_

#include "export.h"

namespace modemcellular
{

// Syllabic 2:1 compandor of the speech path (ITU-T G.162; attack 3 ms, recovery 13.5 ms). The
// compressor (towards the radio) divides by the square root of the envelope, which halves the
// range in dB; the expander (from the radio) multiplies by the envelope, which doubles it again.
// A level of 1.0 passes unchanged. The envelope is held between
// -30 dB and +20 dB; the expander does not go above 1.0, it only restores what the compressor
// lifted. Compressor and expander have their own state. Not thread safe.
class MODEMCELLULAR_API Compandor
{
public:
    explicit Compandor(double sampleRate = 8000.0, double attackMs = 3.0, double recoveryMs = 13.5);

    void compress(float* samples, int count);
    void expand(float* samples, int count);
    // Both back to a level of 1.0.
    void reset();

private:
    struct Tracker
    {
        double peak;
        double envelope;
    };

    void track(Tracker& t, double value, double maxEnvelope) const;

    double m_stepUp;
    double m_stepDown;
    Tracker m_compressor;
    Tracker m_expander;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_COMPANDOR_H_
