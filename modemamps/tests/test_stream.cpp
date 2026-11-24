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
#include <cstring>
#include <string>

#include "ampsbch.h"
#include "ampsstream.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static uint64_t read(const std::vector<uint8_t>& bits, size_t at, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++) {
        v = (v << 1) | bits[at + static_cast<size_t>(i)];
    }
    return v;
}

static bool isDotting(const std::vector<uint8_t>& bits, size_t at, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (bits[at + static_cast<size_t>(i)] != ((i & 1) == 0 ? 1 : 0)) {
            return false;
        }
    }
    return true;
}

// osmocom-analog's amps_encode_focc_bits (amps/frame.c), 'i' for the busy-idle bits
static std::string referenceFocc(uint64_t wordA, uint64_t wordB)
{
    std::string bits = "1010101010i11100010010i";
    for (int i = 0; i < 5; i++)
    {
        for (uint64_t word : {wordA, wordB})
        {
            for (int j = 39; j >= 0; j--)
            {
                bits += static_cast<char>('0' + ((word >> j) & 1));
                if ((j % 10) == 0) {
                    bits += 'i';
                }
            }
        }
    }
    return bits;
}

// 3.7.1: dotting, sync, A1 B1 .. A5 B5 with the busy-idle bits.
static void testFocc()
{
    const uint64_t a = bchEncode(0xabcdef1, kForwardMessageBits);
    const uint64_t b = bchEncode(0x1234567, kForwardMessageBits);

    for (bool busy : {false, true})
    {
        const std::vector<uint8_t> bits = foccTrain(a, b, busy);
        CHECK(bits.size() == static_cast<size_t>(kFoccTrainBits));
        CHECK(isDotting(bits, 0, 10));
        CHECK(read(bits, 11, 11) == kWordSync);

        int busyIdle = 0;
        std::vector<uint8_t> message;
        for (int i = 0; i < kFoccTrainBits; i++)
        {
            if (foccBusyIdleBit(i))
            {
                busyIdle++;
                CHECK(bits[static_cast<size_t>(i)] == (busy ? 0 : 1));
            }
            else if (i >= 23) {
                message.push_back(bits[static_cast<size_t>(i)]);
            }
        }
        CHECK(busyIdle == 42);
        CHECK(message.size() == 400);
        for (int r = 0; r < 5; r++)
        {
            CHECK(read(message, static_cast<size_t>(r * 80), 40) == a);
            CHECK(read(message, static_cast<size_t>(r * 80 + 40), 40) == b);
        }

        // the same as osmocom-analog's
        const std::string reference = referenceFocc(a, b);
        CHECK(reference.size() == bits.size());
        for (size_t i = 0; i < bits.size(); i++)
        {
            if (reference[i] == 'i') {
                CHECK(foccBusyIdleBit(static_cast<int>(i)));
            } else {
                CHECK(bits[i] == reference[i] - '0' && !foccBusyIdleBit(static_cast<int>(i)));
            }
        }
    }
}

// 3.7.2: 101 dotting, sync, word; then 10 times 37 dotting, sync, word.
static void testFvc()
{
    const uint64_t w = bchEncode(0x5a5a5a5, kForwardMessageBits);
    const std::vector<uint8_t> bits = fvcWord(w);

    CHECK(bits.size() == static_cast<size_t>(kFvcWordBits));
    size_t at = 0;
    for (int r = 0; r < kFvcRepeats; r++)
    {
        const int dot = r == 0 ? 101 : 37;
        CHECK(isDotting(bits, at, dot));
        at += static_cast<size_t>(dot);
        CHECK(read(bits, at, 11) == kWordSync);
        at += 11;
        CHECK(read(bits, at, 40) == w);
        at += 40;
    }
    CHECK(at == bits.size());
}

// 2.7.1: 30 dotting, sync, coded DCC (table 2.7.1-1), each word 5 times.
static void testRecc()
{
    CHECK(codedDcc(0) == 0x00 && codedDcc(1) == 0x1f && codedDcc(2) == 0x63 && codedDcc(3) == 0x7c);

    const uint64_t w1 = bchEncode(0x123456789ull, kReverseMessageBits);
    const uint64_t w2 = bchEncode(0x987654321ull, kReverseMessageBits);
    const std::vector<uint8_t> bits = reccMessage(2, {w1, w2});
    CHECK(bits.size() == static_cast<size_t>(kReccPrecursorBits + 2 * 5 * 48));
    CHECK(isDotting(bits, 0, 30));
    CHECK(read(bits, 30, 11) == kWordSync);
    CHECK(read(bits, 41, 7) == 0x63);
    for (int r = 0; r < 5; r++)
    {
        CHECK(read(bits, static_cast<size_t>(48 + r * 48), 48) == w1);
        CHECK(read(bits, static_cast<size_t>(48 + 240 + r * 48), 48) == w2);
    }
}

// 2.7.2: 101 dotting only before the first repeat of the first word; word 2 starts with 37.
static void testRvc()
{
    const uint64_t w1 = bchEncode(0x111111111ull, kReverseMessageBits);
    const uint64_t w2 = bchEncode(0x222222222ull, kReverseMessageBits);
    const std::vector<uint8_t> bits = rvcMessage({w1, w2});

    CHECK(bits.size() == static_cast<size_t>(101 + 9 * 37 + 10 * (11 + 48)));
    size_t at = 0;
    for (int n = 0; n < 10; n++)
    {
        const int dot = n == 0 ? 101 : 37;
        CHECK(isDotting(bits, at, dot));
        at += static_cast<size_t>(dot);
        CHECK(read(bits, at, 11) == kWordSync);
        at += 11;
        CHECK(read(bits, at, 48) == (n < 5 ? w1 : w2));
        at += 48;
    }
}

int main()
{
    testFocc();
    testFvc();
    testRecc();
    testRvc();

    if (g_failures == 0) {
        std::printf("all stream tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
