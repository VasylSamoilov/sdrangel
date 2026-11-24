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

#include "ampsbch.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static uint64_t bits(const std::string& s)
{
    uint64_t v = 0;
    for (char c : s) {
        v = (v << 1) | static_cast<uint64_t>(c == '1');
    }
    return v;
}

// osmocom-analog's encoder (amps/frame.c encode_bch), a shift register on characters, kept here
// to compare with.
static uint16_t referenceParity(uint64_t message, int length)
{
    static const char gp[12] = {0, 1, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1};
    char redun[12] = {0};

    for (int i = 0; i < length; i++)
    {
        const int feedback = static_cast<int>((message >> (length - 1 - i)) & 1u) ^ redun[0];
        for (int j = 11; j > 0; j--) {
            redun[11 - j] = static_cast<char>((feedback && gp[11 - j]) ? redun[12 - j] ^ 1 : redun[12 - j]);
        }
        redun[11] = static_cast<char>(feedback ? gp[11] : 0);
    }
    uint16_t p = 0;
    for (int i = 0; i < 12; i++) {
        p = static_cast<uint16_t>((p << 1) | redun[i]);
    }
    return p;
}

static void testVectors()
{
    // the two reverse channel words osmocom-analog's frame.c keeps as test vectors
    CHECK(bchCheck(bits("101011101000111001101110100111011111111110100000"), kReverseMessageBits));
    CHECK(bchCheck(bits("000100000011011010000000000110101101001110101011"), kReverseMessageBits));
    // the all-zero word is a code word
    CHECK(bchParity(0, kForwardMessageBits) == 0 && bchCheck(0, kReverseMessageBits));
}

static void testSameAsReference()
{
    uint64_t seed = 12345;
    for (int n = 0; n < 2000; n++)
    {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        const uint64_t f = (seed >> 20) & ((1ull << kForwardMessageBits) - 1);
        const uint64_t r = (seed >> 7) & ((1ull << kReverseMessageBits) - 1);
        CHECK(bchParity(f, kForwardMessageBits) == referenceParity(f, kForwardMessageBits));
        CHECK(bchParity(r, kReverseMessageBits) == referenceParity(r, kReverseMessageBits));
        CHECK(bchCheck(bchEncode(f, kForwardMessageBits), kForwardMessageBits));
        CHECK(bchCheck(bchEncode(r, kReverseMessageBits), kReverseMessageBits));
    }
}

// The code has the distance 5: every error of 1 and 2 bits in a word is seen.
static void testErrorsDetected()
{
    const uint64_t messages[] = {0x0, 0xabcdef1, 0x5555555, 0xfffffff};

    for (uint64_t m : messages)
    {
        const uint64_t word = bchEncode(m, kForwardMessageBits);
        for (int a = 0; a < kForwardWordBits; a++)
        {
            CHECK(!bchCheck(word ^ (1ull << a), kForwardMessageBits));
            for (int b = a + 1; b < kForwardWordBits; b++) {
                CHECK(!bchCheck(word ^ (1ull << a) ^ (1ull << b), kForwardMessageBits));
            }
        }
    }
}

// Linear: the sum of two code words is a code word.
static void testLinear()
{
    const uint64_t a = bchEncode(0x123456789ull, kReverseMessageBits);
    const uint64_t b = bchEncode(0xfedcba987ull, kReverseMessageBits);
    CHECK(bchCheck(a ^ b, kReverseMessageBits));
}

int main()
{
    testVectors();
    testSameAsReference();
    testErrorsDetected();
    testLinear();

    if (g_failures == 0) {
        std::printf("all BCH tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
