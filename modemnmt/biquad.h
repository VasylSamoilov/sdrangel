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

#ifndef MODEMNMT_BIQUAD_H_
#define MODEMNMT_BIQUAD_H_

#include <vector>

#include "export.h"

namespace modemnmt
{

// Butterworth style low pass: a cascade of identical biquad sections (transposed direct form II),
// each with Q = 0.7071^(1/sections), so the cascade is 3 dB down at the cut-off frequency.
// Coefficients after Nigel Redmon (www.earlevel.com).
class MODEMNMT_API BiquadLowpass
{
public:
    BiquadLowpass(double cutoffHz, double sampleRate, int sections);

    // One sample in, one sample out.
    double process(double in);
    void reset();

private:
    double m_a0, m_a1, m_a2, m_b1, m_b2;
    std::vector<double> m_z1;
    std::vector<double> m_z2;
};

} // namespace modemnmt

#endif // MODEMNMT_BIQUAD_H_
