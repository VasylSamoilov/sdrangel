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

#include "nmtnumbers.h"

namespace modemnmt
{

namespace
{

// NMT Doc 450-1 para 4.3.3.1 notes 1 and 3: bit 1 (0x800) and bit 4 (0x100) of N1 select the
// hundreds (0xx0: 1..99, 0xx1: 100..199, 1xx0: 200..299, 1xx1: 300..400). N2N3 are the tens
// and units in BCD. Channel 400 is the top of the 300 base and is coded '39A'.
uint64_t encode450(int channel, int power)
{
    uint64_t value = 0;

    if (channel >= 300)
    {
        value |= 0x900;
        channel -= 300;
    }
    else if (channel >= 200)
    {
        value |= 0x800;
        channel -= 200;
    }
    else if (channel >= 100)
    {
        value |= 0x100;
        channel -= 100;
    }

    if (channel == 100)
    {
        value |= 0x9a;      // channel 400
    }
    else
    {
        value |= channel % 10;
        value |= static_cast<uint64_t>(channel / 10) << 4;
    }

    value |= static_cast<uint64_t>(power & 3) << 9;

    return value;
}

bool decode450(uint64_t value, int& channel, int& power)
{
    const int base = ((((value & 0x800) >> 11) << 1) | ((value & 0x100) >> 8)) * 100;

    if ((base == 300) && ((value & 0x0ff) == 0x9a))
    {
        channel = 400;
    }
    else
    {
        if ((value & 0x00f) > 0x009) {
            return false;
        }

        if ((value & 0x0f0) > 0x090) {
            return false;
        }

        channel = base + static_cast<int>(value & 0x00f) + static_cast<int>((value & 0x0f0) >> 4) * 10;
    }

    power = static_cast<int>((value & 0x600) >> 9);

    return true;
}

} // namespace

uint64_t encodeChannel(System system, int channel, int power)
{
    if (system == System::Nmt450) {
        return encode450(channel, power);
    }

    // Interleaved channels are indicated in the traffic area.
    if (channel >= 1024) {
        channel -= 1024;
    }

    uint64_t value = static_cast<uint64_t>(channel);

    // Bit 9 of the channel is at bit 11, the power bits take its place.
    if (value & 0x200) {
        value = value - 0x200 + 0x800;
    }

    value |= static_cast<uint64_t>(power & 3) << 9;

    return value;
}

bool decodeChannel(System system, uint64_t value, int& channel, int& power)
{
    if (system == System::Nmt450) {
        return decode450(value, channel, power);
    }

    channel = static_cast<int>((value & 0x1ff) + ((value & 0x800) >> 2));
    power = static_cast<int>((value & 0x600) >> 9);

    return true;
}

uint64_t encodeTc(System system, int channel, int power)
{
    if (system == System::Nmt450) {
        return encode450(channel, power);
    }

    return static_cast<uint64_t>(channel) & 0x7ff;
}

bool decodeTc(System system, uint64_t value, int& channel, int& power)
{
    if (system == System::Nmt450) {
        return decode450(value, channel, power);
    }

    channel = static_cast<int>(value & 0x7ff);
    power = 0;

    return true;
}

uint64_t encodeTrafficArea(System system, int channel, uint8_t trafficArea)
{
    if (system == System::Nmt450) {
        return trafficArea;
    }

    uint64_t value = trafficArea & 0x7f;

    if (channel >= 1024) {
        value |= 0x80;
    }

    return value;
}

std::string valueToDigits(uint64_t value, int num)
{
    std::string digits;

    for (int i = 0; i < num; i++)
    {
        const int digit = static_cast<int>((value >> ((num - 1 - i) << 2)) & 0xf);

        if (digit == 10) {
            digits += '0';
        } else if (digit == 0) {
            digits += 'a';
        } else if (digit > 10) {
            digits += static_cast<char>(digit - 10 + 'a');
        } else {
            digits += static_cast<char>(digit + '0');
        }
    }

    return digits;
}

uint64_t digitsToValue(const std::string& digits)
{
    uint64_t value = 0;

    for (char c : digits)
    {
        value <<= 4;

        if (c >= '1' && c <= '9') {
            value |= static_cast<uint64_t>(c - '0');
        } else if (c == '0') {
            value |= 10;
        } else if (c >= 'b' && c <= 'f') {
            value |= static_cast<uint64_t>(c - 'a' + 10);
        } else if (c >= 'B' && c <= 'F') {
            value |= static_cast<uint64_t>(c - 'A' + 10);
        }
    }

    return value;
}

char valueToDigit(uint64_t value)
{
    return "D1234567890*#ABC"[value & 0xf];
}

uint8_t flipTen(uint8_t v)
{
    if (!(v % 10)) {
        v ^= 10;
    }

    return v;
}

uint16_t encodeAreaNo(System system, uint8_t areaNo)
{
    if (areaNo < 1 || areaNo > 4) {
        return 0x000;
    }

    // NMT-900 sends the area as the fictitious channels 1011..1014, which are coded like a
    // channel number: bit 9 of the channel moves to bit 11 (NMT Doc 900-1 para 4.3.3.11.1)
    return static_cast<uint16_t>((system == System::Nmt450 ? 0x3f2 : 0x9f2) + areaNo);
}

uint8_t encodeBandLimit(int channel)
{
    return static_cast<uint8_t>((channel >> 2) & 0xff);
}

int lineSignalMajority(uint64_t lineSignal, int numChars, int minEqual)
{
    int count[16] = {0};
    int best = -1;
    int bestCount = 0;

    for (int i = 0; i < numChars; i++)
    {
        const int v = static_cast<int>((lineSignal >> (i * 4)) & 0xf);

        if (++count[v] > bestCount)
        {
            bestCount = count[v];
            best = v;
        }
    }

    if (bestCount >= minEqual) {
        return best;
    }

    return -1;
}

} // namespace modemnmt
