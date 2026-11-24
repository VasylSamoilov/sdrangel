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

#ifndef MODEMAMPS_AMPSBCH_H_
#define MODEMAMPS_AMPSBCH_H_

#include <cstdint>

#include "export.h"

namespace modemamps
{

// The BCH code of the AMPS data words (TIA/EIA-553-A 3.7.1 and 2.7.1): 12 parity bits after the
// message bits, BCH(40,28) on the forward channels and BCH(48,36) on the reverse channels, both
// shortened from the BCH(63,51) code with the generator polynomial
//   g(x) = x^12 + x^10 + x^8 + x^5 + x^4 + x^3 + 1.
// Words are values with the first bit sent as the most significant bit.
constexpr uint32_t kBchGenerator = 0x1539;      // the bits of g(x), x^12 .. x^0
constexpr int kBchParityBits = 12;
constexpr int kForwardMessageBits = 28;
constexpr int kReverseMessageBits = 36;
constexpr int kForwardWordBits = kForwardMessageBits + kBchParityBits;     // 40
constexpr int kReverseWordBits = kReverseMessageBits + kBchParityBits;     // 48

// The 12 parity bits of messageBits bits of message (at most 51).
MODEMAMPS_API uint16_t bchParity(uint64_t message, int messageBits);
// The word: the message followed by its parity.
MODEMAMPS_API uint64_t bchEncode(uint64_t message, int messageBits);
// True if the word of messageBits + 12 bits has the parity of its message.
MODEMAMPS_API bool bchCheck(uint64_t word, int messageBits);

} // namespace modemamps

#endif // MODEMAMPS_AMPSBCH_H_
