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

#ifndef MODEMCELLULAR_BIQUAD_H_
#define MODEMCELLULAR_BIQUAD_H_

#include <vector>

#include "export.h"

namespace modemcellular
{

// A cascade of identical biquad sections (transposed direct form II). Coefficients after Nigel
// Redmon (www.earlevel.com).
class MODEMCELLULAR_API BiquadCascade
{
public:
    // One sample in, one sample out.
    double process(double in);
    void reset();

protected:
    explicit BiquadCascade(int sections);

    double m_a0, m_a1, m_a2, m_b1, m_b2;

private:
    std::vector<double> m_z1;
    std::vector<double> m_z2;
};

// Butterworth style low pass: each section has Q = 0.7071^(1/sections), so the cascade is 3 dB
// down at the cut-off frequency.
class MODEMCELLULAR_API BiquadLowpass : public BiquadCascade
{
public:
    BiquadLowpass(double cutoffHz, double sampleRate, int sections);
};

// Butterworth style high pass, same Q as the low pass.
// A notch at the frequency, q its quality (osmocom-analog's iir_notch_init), each section the same.
class MODEMCELLULAR_API BiquadNotch : public BiquadCascade
{
public:
    BiquadNotch(double frequencyHz, double sampleRate, int sections, double q);
};

class MODEMCELLULAR_API BiquadHighpass : public BiquadCascade
{
public:
    BiquadHighpass(double cutoffHz, double sampleRate, int sections);
};

} // namespace modemcellular

#endif // MODEMCELLULAR_BIQUAD_H_
