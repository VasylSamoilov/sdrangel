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

// A cascade of identical biquad sections (transposed direct form II). Coefficients after Nigel
// Redmon (www.earlevel.com).
class MODEMNMT_API BiquadCascade
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
class MODEMNMT_API BiquadLowpass : public BiquadCascade
{
public:
    BiquadLowpass(double cutoffHz, double sampleRate, int sections);
};

// Butterworth style high pass, same Q as the low pass.
class MODEMNMT_API BiquadHighpass : public BiquadCascade
{
public:
    BiquadHighpass(double cutoffHz, double sampleRate, int sections);
};

// Flat Butterworth filter of an even order, as biquad sections with the Q of each pole pair.
// (A cascade of identical sections, as above, is 3 dB down at the cut-off but rings in the
// pass band; this one is flat, which the speech path needs.)
class MODEMNMT_API ButterworthFilter
{
public:
    enum class Type
    {
        Lowpass,
        Highpass
    };

    // order is rounded up to an even number, at least 2
    ButterworthFilter(Type type, double cutoffHz, double sampleRate, int order);

    double process(double in);
    void reset();

private:
    struct Section
    {
        double a0, a1, a2, b1, b2;
        double z1, z2;
    };

    std::vector<Section> m_sections;
};

// Notch with the given Q in every section.
class MODEMNMT_API BiquadNotch : public BiquadCascade
{
public:
    BiquadNotch(double frequencyHz, double sampleRate, int sections, double q);
};

} // namespace modemnmt

#endif // MODEMNMT_BIQUAD_H_
