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

#ifndef MODEMNMT_NMTNUMBERS_H_
#define MODEMNMT_NMTNUMBERS_H_

#include <cstdint>
#include <string>

#include "export.h"

namespace modemnmt
{

enum class System
{
    Nmt450 = 450,
    Nmt900 = 900
};

// Coding of the numbers in signalling frames (NMT Doc 450-1 para 4.3.3, NMT Doc 900-1 para 4.3.3).
// Channel numbers are 1..400 for NMT-450 and 1..1023 plus the interleaved channels 1024..2047
// for NMT-900. Power is the two power bits of the mobile station (0..3).

// Value of the N1N2N3 digits. NMT-450: bits 1 and 4 of N1 select the hundreds, N2N3 are the
// tens and units in BCD, channel 400 is '39A'. NMT-900: the 10-bit channel with bit 9 moved to
// bit 11. The power bits are at 0x600. Interleaved NMT-900 channels have the same N1N2N3 as
// the channel 1024 lower; they are marked in the traffic area (encodeTrafficArea).
MODEMNMT_API uint64_t encodeChannel(System system, int channel, int power);
// False if a digit of an NMT-450 channel is not a BCD digit.
MODEMNMT_API bool decodeChannel(System system, uint64_t value, int& channel, int& power);

// Value of the NaNbNc digits of a traffic channel. NMT-450 as above. NMT-900: 11 bit channel,
// the interleaved channels have bit 10 set, there are no power bits.
MODEMNMT_API uint64_t encodeTc(System system, int channel, int power);
// power is 0 for NMT-900.
MODEMNMT_API bool decodeTc(System system, uint64_t value, int& channel, int& power);

// Value of the Y1Y2 digits. NMT-450: the traffic area. NMT-900: seven bits of traffic area and
// the top bit set for an interleaved channel (channel >= 1024).
MODEMNMT_API uint64_t encodeTrafficArea(System system, int channel, uint8_t trafficArea);

// Digit characters of a value, most significant digit first, num digits. 10 is '0' and 0 is
// 'a' (not used), 11..15 are 'b'..'f'.
MODEMNMT_API std::string valueToDigits(uint64_t value, int num);
// Reverse of valueToDigits(); characters that are no digit count as 0.
MODEMNMT_API uint64_t digitsToValue(const std::string& digits);
// Keypad character of a digit signal: "D1234567890*#ABC"[value & 15].
MODEMNMT_API char valueToDigit(uint64_t value);
// Swaps the digit values 0 and 10 for multiples of 10, other values stay.
MODEMNMT_API uint8_t flipTen(uint8_t v);

// Area number 1..4 as H8H9H10, anything else as 000. NMT-450: 3F3..3F6 (NMT Doc 450-1 para
// 4.3.3.10.1). NMT-900: the fictitious channels 1011..1014 coded as a channel number, 9F3..9F6
// (NMT Doc 900-1 para 4.3.3.11.1).
MODEMNMT_API uint16_t encodeAreaNo(System system, uint8_t areaNo);

// NMT-900 channel band limit (NMT Doc 900-1 para 4.3.3.13.5): bits a9..a2 of the channel
// number, the band is given in steps of 100 kHz. Channel 1 gives 00, which means the start of
// the band.
MODEMNMT_API uint8_t encodeBandLimit(int channel);

// Line signals repeat the signal number in numChars hexadecimal digits (low nibbles first). The
// value that occurs at least minEqual times is accepted (NMT Doc 450-1 para 4.7.2.3: 2 of 3
// towards the mobile station, 3 of 5 towards the exchange). Returns -1 if there is none.
MODEMNMT_API int lineSignalMajority(uint64_t lineSignal, int numChars, int minEqual);

} // namespace modemnmt

#endif // MODEMNMT_NMTNUMBERS_H_
