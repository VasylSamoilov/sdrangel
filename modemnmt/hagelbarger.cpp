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

#include "hagelbarger.h"

namespace modemnmt
{

namespace
{

// 64 message bits and six bits to flush the delay.
constexpr int kInputBits = 70;
// The decoder runs ten bits longer than the message, to read the delayed information bits.
constexpr int kDecoderDelay = 10;

} // namespace

EncodedBits hagelbargerEncode(const MessageBytes& message)
{
    EncodedBits encoded;
    uint8_t reg = 0;    // newest message bit in bit 0, bit 6 is the message bit six positions back

    for (int i = 0; i < kInputBits; i++)
    {
        const uint8_t x = (i < kMessageBits) ? ((message[i / 8] >> (7 - (i & 7))) & 1) : 0;

        reg = static_cast<uint8_t>((reg << 1) | x);

        const uint8_t data = (reg >> 6) & 1;
        const uint8_t check = (reg + (reg >> 3) + 1) & 1;

        encoded[2 * i] = check;
        encoded[2 * i + 1] = data;
    }

    return encoded;
}

MessageBytes hagelbargerDecode(const EncodedBits& encoded)
{
    MessageBytes message{};
    uint16_t regData = 0x00;
    uint16_t regCheck = 0xff;
    int o = 0;

    for (int i = 0; i < kMessageBits + kDecoderDelay; i++)
    {
        // The encoded stream is followed by zero bits.
        const uint16_t check = (i < kInputBits) ? (encoded[2 * i] & 1) : 0;
        const uint16_t data = (i < kInputBits) ? (encoded[2 * i + 1] & 1) : 0;

        regCheck = static_cast<uint16_t>((regCheck << 1) | check);
        regData = static_cast<uint16_t>((regData << 1) | data);

        // Parity of the received bits against the parity bits calculated from them.
        const uint16_t rParity = (regData + (regData >> 3) + (regCheck >> 6) + 1) & 1;
        const uint16_t sParity = ((regData >> 3) + (regData >> 6) + (regCheck >> 9) + 1) & 1;

        // Both checks fail: the information bit that is three positions old is wrong.
        if (rParity && sParity) {
            regData ^= 0x0008;
        }

        if (i >= kDecoderDelay)
        {
            message[o / 8] = static_cast<uint8_t>(message[o / 8] | (((regData >> 4) & 1) << (7 - (o & 7))));
            o++;
        }
    }

    return message;
}

} // namespace modemnmt
