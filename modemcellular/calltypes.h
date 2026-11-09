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

#ifndef MODEMCELLULAR_CALLTYPES_H_
#define MODEMCELLULAR_CALLTYPES_H_

#include <cstdint>

namespace modemcellular
{

// Identifies one call leg between a mobile and the call router. Zero means "no call".
using CallRef = uint32_t;

// Audio exchanged with a mobile: mono float samples at 8 kHz, normally in 20 ms
// blocks of 160 samples. 1.0 is full scale speech level.
constexpr int kAudioSampleRate = 8000;
constexpr int kAudioFrameSamples = 160;

enum class Cause
{
    Normal,
    Busy,
    NoChannel,
    OutOfOrder,
    InvalidNumber,
    TempFail
};

enum class NumberType
{
    NotAvailable,
    Anonymous,
    Unknown,
    Subscriber,
    National,
    International
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CALLTYPES_H_
