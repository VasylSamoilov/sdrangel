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

#include <cstdio>
#include <string>

#include "nmtnumbers.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// NMT Doc 450-1 para 4.3.3.1 notes 1 and 3.
static void testChannel450()
{
    // N1 bits 1 and 4 select the hundreds, N2N3 are BCD
    CHECK(encodeChannel(System::Nmt450, 1, 0) == 0x001);
    CHECK(encodeChannel(System::Nmt450, 99, 0) == 0x099);
    CHECK(encodeChannel(System::Nmt450, 100, 0) == 0x100);
    CHECK(encodeChannel(System::Nmt450, 135, 0) == 0x135);       // the worked example of the standard
    CHECK(encodeChannel(System::Nmt450, 199, 0) == 0x199);
    CHECK(encodeChannel(System::Nmt450, 200, 0) == 0x800);
    CHECK(encodeChannel(System::Nmt450, 299, 0) == 0x899);
    CHECK(encodeChannel(System::Nmt450, 300, 0) == 0x900);
    CHECK(encodeChannel(System::Nmt450, 399, 0) == 0x999);
    // channel 400 is '39A', here with power bits 00 and 01
    CHECK(encodeChannel(System::Nmt450, 400, 0) == 0x99a);
    CHECK(encodeChannel(System::Nmt450, 400, 1) == 0xb9a);
    // power bits 0x600
    CHECK(encodeChannel(System::Nmt450, 135, 3) == (0x135 | 0x600));

    for (int channel = 1; channel <= 400; channel++)
    {
        for (int power = 0; power < 4; power++)
        {
            int c = 0;
            int p = 0;
            const uint64_t v = encodeChannel(System::Nmt450, channel, power);

            CHECK(decodeChannel(System::Nmt450, v, c, p));
            CHECK(c == channel);
            CHECK(p == power);

            CHECK(decodeTc(System::Nmt450, encodeTc(System::Nmt450, channel, power), c, p));
            CHECK(c == channel);
            CHECK(p == power);
        }
    }

    int c = 0;
    int p = 0;
    CHECK(!decodeChannel(System::Nmt450, 0x00a, c, p));      // units are no BCD digit
    CHECK(!decodeChannel(System::Nmt450, 0x0a0, c, p));      // tens are no BCD digit
    CHECK(!decodeTc(System::Nmt450, 0x00b, c, p));
}

// NMT Doc 900-1 para 4.3.3.1 note 4: N1N2N3 = a9 x x a8 | a7..a4 | a3..a0 (xx are the power
// bits), Y1 = a10 y y y, NaNbNc = 0 a10 a9 a8 | a7..a4 | a3..a0.
static void testChannel900()
{
    CHECK(encodeChannel(System::Nmt900, 1, 0) == 0x001);
    CHECK(encodeChannel(System::Nmt900, 256, 0) == 0x100);       // a8
    CHECK(encodeChannel(System::Nmt900, 512, 0) == 0x800);       // a9 is at bit 11
    CHECK(encodeChannel(System::Nmt900, 1023, 0) == 0x9ff);
    CHECK(encodeChannel(System::Nmt900, 1023, 3) == (0x9ff | 0x600));
    // the interleaved channel has the N1N2N3 of the channel 1024 lower
    CHECK(encodeChannel(System::Nmt900, 1030, 0) == encodeChannel(System::Nmt900, 6, 0));

    CHECK(encodeTc(System::Nmt900, 1, 0) == 0x001);
    CHECK(encodeTc(System::Nmt900, 512, 0) == 0x200);            // a9 is at bit 9
    CHECK(encodeTc(System::Nmt900, 1023, 0) == 0x3ff);
    CHECK(encodeTc(System::Nmt900, 1024, 0) == 0x400);           // a10 is at bit 10
    CHECK(encodeTc(System::Nmt900, 2047, 0) == 0x7ff);

    for (int channel = 1; channel <= 1023; channel++)
    {
        for (int power = 0; power < 4; power++)
        {
            int c = 0;
            int p = 0;
            CHECK(decodeChannel(System::Nmt900, encodeChannel(System::Nmt900, channel, power), c, p));
            CHECK(c == channel);
            CHECK(p == power);
        }
    }

    // traffic channels include the interleaved ones, up to a10
    for (int channel = 1; channel <= 2047; channel++)
    {
        int c = 0;
        int p = 9;
        CHECK(decodeTc(System::Nmt900, encodeTc(System::Nmt900, channel, 0), c, p));
        CHECK(c == channel);
        CHECK(p == 0);
    }
}

static void testTrafficArea()
{
    CHECK(encodeTrafficArea(System::Nmt450, 135, 0x64) == 0x64);
    CHECK(encodeTrafficArea(System::Nmt450, 135, 0xff) == 0xff);
    // NMT-900: seven bits and the interleaved marker in the top bit
    CHECK(encodeTrafficArea(System::Nmt900, 6, 0x15) == 0x15);
    CHECK(encodeTrafficArea(System::Nmt900, 1023, 0x15) == 0x15);
    CHECK(encodeTrafficArea(System::Nmt900, 1024, 0x15) == 0x95);
    CHECK(encodeTrafficArea(System::Nmt900, 1030, 0x7f) == 0xff);
    CHECK(encodeTrafficArea(System::Nmt900, 6, 0xff) == 0x7f);
}

static void testDigits()
{
    // 10 is the digit '0', 0 is not used and shown as 'a', 11..15 are 'b'..'f'
    CHECK(valueToDigits(0x1a0b, 4) == "10ab");
    CHECK(valueToDigits(0x123456789abcdef0ULL, 16) == "1234567890bcdefa");
    CHECK(valueToDigits(0x5, 1) == "5");
    CHECK(valueToDigits(0x5, 3) == "aa5");

    CHECK(digitsToValue("10ab") == 0x1a0b);             // 'a' counts as 0
    CHECK(digitsToValue("1234567890") == 0x123456789aULL);
    CHECK(digitsToValue("bcdef") == 0xbcdef);
    CHECK(digitsToValue("BCDEF") == 0xbcdef);
    CHECK(digitsToValue("x") == 0);                     // no digit counts as 0
    CHECK(digitsToValue("") == 0);

    // a number survives the conversion
    CHECK(valueToDigits(digitsToValue("0601323890"), 10) == "0601323890");

    CHECK(valueToDigit(0) == 'D');
    CHECK(valueToDigit(1) == '1');
    CHECK(valueToDigit(9) == '9');
    CHECK(valueToDigit(10) == '0');
    CHECK(valueToDigit(11) == '*');
    CHECK(valueToDigit(12) == '#');
    CHECK(valueToDigit(13) == 'A');
    CHECK(valueToDigit(14) == 'B');
    CHECK(valueToDigit(15) == 'C');
    CHECK(valueToDigit(0x15) == '5');                   // only the low nibble counts

    CHECK(flipTen(0) == 10);
    CHECK(flipTen(10) == 0);
    CHECK(flipTen(1) == 1);
    CHECK(flipTen(9) == 9);
    CHECK(flipTen(11) == 11);
    CHECK(flipTen(15) == 15);
}

static void testAreaNumber()
{
    CHECK(encodeAreaNo(System::Nmt450, 1) == 0x3f3);
    CHECK(encodeAreaNo(System::Nmt450, 2) == 0x3f4);
    CHECK(encodeAreaNo(System::Nmt450, 3) == 0x3f5);
    CHECK(encodeAreaNo(System::Nmt450, 4) == 0x3f6);
    CHECK(encodeAreaNo(System::Nmt450, 0) == 0x000);
    CHECK(encodeAreaNo(System::Nmt450, 5) == 0x000);

    // NMT-900: channels 1011..1014 as N1N2N3
    for (int area = 1; area <= 4; area++)
    {
        CHECK(encodeAreaNo(System::Nmt900, static_cast<uint8_t>(area)) == 0x9f2 + area);
        CHECK(encodeAreaNo(System::Nmt900, static_cast<uint8_t>(area)) == encodeChannel(System::Nmt900, 1010 + area, 0));
    }
    CHECK(encodeAreaNo(System::Nmt900, 0) == 0x000);
    CHECK(encodeAreaNo(System::Nmt900, 5) == 0x000);

    CHECK(encodeBandLimit(1) == 0x00);
    CHECK(encodeBandLimit(4) == 0x01);
    CHECK(encodeBandLimit(124) == 31);
    CHECK(encodeBandLimit(1023) == 0xff);
    CHECK(encodeBandLimit(1024 + 8) == 2);                  // a10 is dropped
}

// NMT Doc 450-1 para 4.7.2.3: 2 of 3 equal towards the mobile station, 3 of 5 towards the exchange.
static void testLineSignalMajority()
{
    // three characters, the lowest nibble is character 0
    CHECK(lineSignalMajority(0x999, 3, 2) == 9);
    CHECK(lineSignalMajority(0x9a9, 3, 2) == 9);
    CHECK(lineSignalMajority(0x99a, 3, 2) == 9);
    CHECK(lineSignalMajority(0xa99, 3, 2) == 9);
    CHECK(lineSignalMajority(0x123, 3, 2) == -1);

    // five characters
    CHECK(lineSignalMajority(0xeeeee, 5, 3) == 14);
    CHECK(lineSignalMajority(0xee1e2, 5, 3) == 14);
    CHECK(lineSignalMajority(0x1e2e3, 5, 3) == -1);
    CHECK(lineSignalMajority(0x1ee2e, 5, 3) == 14);
    CHECK(lineSignalMajority(0x11223, 5, 3) == -1);
    CHECK(lineSignalMajority(0x00000, 5, 3) == 0);          // 0 is a value like any other
}

int main()
{
    testChannel450();
    testChannel900();
    testTrafficArea();
    testDigits();
    testAreaNumber();
    testLineSignalMajority();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all number tests passed\n");
    return 0;
}
