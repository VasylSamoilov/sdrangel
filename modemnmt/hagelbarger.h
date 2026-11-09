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

#ifndef MODEMNMT_HAGELBARGER_H_
#define MODEMNMT_HAGELBARGER_H_

#include <array>
#include <cstdint>

#include "export.h"

namespace modemnmt
{

// NMT signalling uses a convolutional burst-error-correcting code (NMT Doc 450-1, para 4.6.2):
// 64 message bits X1..X64, followed by six zero bits, give 140 encoded bits Y1..Y140.
//   Y(2i-1) = not (X(i) xor X(i-3))   parity check bit, X(j) = 0 outside 1..64
//   Y(2i)   = X(i-6)                  delayed information bit, 0 for i = 1..6
// It corrects a burst of up to 6 bits if at least 19 error-free bits follow before the next
// burst. There is no error detection: the receiver has to check the information elements.

constexpr int kMessageBits = 64;
constexpr int kEncodedBits = 140;

// 16 hexadecimal digits, first digit in the high nibble of the first byte.
using MessageBytes = std::array<uint8_t, 8>;
// One bit per element (0 or 1) in transmission order.
using EncodedBits = std::array<uint8_t, kEncodedBits>;

MODEMNMT_API EncodedBits hagelbargerEncode(const MessageBytes& message);
// Elements are used as 0 or 1 by their lowest bit.
MODEMNMT_API MessageBytes hagelbargerDecode(const EncodedBits& encoded);

} // namespace modemnmt

#endif // MODEMNMT_HAGELBARGER_H_
