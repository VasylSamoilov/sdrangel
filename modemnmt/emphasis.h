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

#ifndef MODEMNMT_EMPHASIS_H_
#define MODEMNMT_EMPHASIS_H_

#include "biquad.h"
#include "export.h"

namespace modemnmt
{

// The 6 dB per octave emphasis of the speech channel of analogue cellular radio, neutral at
// 1 kHz. Pre-emphasis towards the radio, with a low pass above 3.4 kHz (two sections: with the
// emphasis the slope is the -18 dB per octave the networks want); de-emphasis from the radio,
// after a high pass at 300 Hz that keeps the emphasis from lifting DC and hum.
class MODEMNMT_API Emphasis
{
public:
    static constexpr double kCutOff = 300.0;
    static constexpr double kHighpass = 300.0;
    static constexpr double kLowpass = 3400.0;

    explicit Emphasis(double sampleRate);

    void pre(float* samples, int count);
    // the high pass and the de-emphasis
    void de(float* samples, int count);

private:
    void preRaw(float* samples, int count);
    void deRaw(float* samples, int count);

    double m_factor;
    double m_preAmp;
    double m_deAmp;
    double m_xLast;
    double m_yLast;
    BiquadLowpass m_lowpass;
    BiquadHighpass m_highpass;
};

} // namespace modemnmt

#endif // MODEMNMT_EMPHASIS_H_
