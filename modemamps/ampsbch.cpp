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

#include "ampsbch.h"

namespace modemamps
{

// The remainder of message(x) * x^12 divided by g(x), the message shifted in from its first bit.
uint16_t bchParity(uint64_t message, int messageBits)
{
    uint32_t remainder = 0;

    for (int i = messageBits - 1; i >= 0; i--)
    {
        const uint32_t in = static_cast<uint32_t>((message >> i) & 1u);
        const uint32_t feedback = ((remainder >> (kBchParityBits - 1)) & 1u) ^ in;

        remainder = (remainder << 1) & ((1u << kBchParityBits) - 1u);
        if (feedback) {
            remainder ^= kBchGenerator & ((1u << kBchParityBits) - 1u);
        }
    }

    return static_cast<uint16_t>(remainder);
}

uint64_t bchEncode(uint64_t message, int messageBits)
{
    const uint64_t mask = (messageBits >= 64) ? ~0ull : ((1ull << messageBits) - 1ull);

    return ((message & mask) << kBchParityBits) | bchParity(message & mask, messageBits);
}

bool bchCheck(uint64_t word, int messageBits)
{
    return bchParity(word >> kBchParityBits, messageBits) == static_cast<uint16_t>(word & ((1u << kBchParityBits) - 1u));
}

} // namespace modemamps
