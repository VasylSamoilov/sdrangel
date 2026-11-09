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
#include <random>
#include <vector>

#include "hagelbarger.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// The encoder written down from the formulas of NMT Doc 450-1 para 4.6.2 (page 84), case by
// case as printed, X1..X64 are x[0..63], Y1..Y140 are y[0..139].
static EncodedBits specEncode(const MessageBytes& message)
{
    int x[65];

    for (int i = 1; i <= 64; i++) {
        x[i] = (message[(i - 1) / 8] >> (7 - ((i - 1) & 7))) & 1;
    }

    EncodedBits y;

    for (int i = 1; i <= 70; i++)
    {
        int parity;

        if (i <= 3) {
            parity = !x[i];
        } else if (i <= 64) {
            parity = !(x[i] ^ x[i - 3]);
        } else if (i <= 67) {
            parity = !x[i - 3];
        } else {
            parity = 1;
        }

        const int information = (i <= 6) ? 0 : x[i - 6];

        y[2 * i - 2] = static_cast<uint8_t>(parity);        // Y(2i-1)
        y[2 * i - 1] = static_cast<uint8_t>(information);   // Y(2i)
    }

    return y;
}

static MessageBytes randomMessage(std::mt19937& rng)
{
    MessageBytes m;

    for (auto& b : m) {
        b = static_cast<uint8_t>(rng());
    }

    return m;
}

// The worked example of the standard (NMT Doc 450-1 pages 85 and 86): frame 1a, channel
// 135, P(12), traffic area 64, no additional information. The printed X and the printed
// values of the first and last encoded bits.
static void testStandardExample()
{
    // X = 0001 0011 0101 1100 0110 0100 0000 ...
    MessageBytes message{};
    message[0] = 0x13;
    message[1] = 0x5c;
    message[2] = 0x64;

    const EncodedBits y = hagelbargerEncode(message);

    const int first20[20] = {1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1};
    const int last8[8] = {1, 0, 1, 0, 1, 0, 1, 0};      // Y133..Y140

    for (int i = 0; i < 20; i++) {
        CHECK(y[i] == first20[i]);
    }

    for (int i = 0; i < 8; i++) {
        CHECK(y[132 + i] == last8[i]);
    }

    CHECK(y == specEncode(message));
}

static void testEncoderMatchesTheFormulas()
{
    std::mt19937 rng(1);
    MessageBytes zero{};
    MessageBytes ones;
    ones.fill(0xff);

    CHECK(hagelbargerEncode(zero) == specEncode(zero));
    CHECK(hagelbargerEncode(ones) == specEncode(ones));

    for (int n = 0; n < 5000; n++)
    {
        const MessageBytes m = randomMessage(rng);
        CHECK(hagelbargerEncode(m) == specEncode(m));
    }
}

static void testRoundTripWithoutErrors()
{
    std::mt19937 rng(2);
    MessageBytes zero{};
    MessageBytes ones;
    ones.fill(0xff);

    CHECK(hagelbargerDecode(hagelbargerEncode(zero)) == zero);
    CHECK(hagelbargerDecode(hagelbargerEncode(ones)) == ones);

    for (int n = 0; n < 5000; n++)
    {
        const MessageBytes m = randomMessage(rng);
        CHECK(hagelbargerDecode(hagelbargerEncode(m)) == m);
    }
}

// Flips the bits of a burst: a span of `length` bits, first and last flipped, the ones in
// between flipped at random.
static void addBurst(EncodedBits& bits, int start, int length, std::mt19937& rng)
{
    for (int i = 0; i < length; i++)
    {
        const bool edge = (i == 0) || (i == length - 1);

        if (edge || (rng() & 1)) {
            bits[start + i] ^= 1;
        }
    }
}

// NMT Doc 450-1 para 4.6.2: the code corrects a burst of up to 6 bits. Every position and
// length of a single burst.
static void testSingleBurstsAreCorrected()
{
    std::mt19937 rng(3);
    int failures = 0;
    int cases = 0;
    std::vector<int> failedStarts;

    for (int length = 1; length <= 6; length++)
    {
        for (int start = 0; start + length <= kEncodedBits; start++)
        {
            for (int n = 0; n < 8; n++)
            {
                const MessageBytes m = randomMessage(rng);
                EncodedBits bits = hagelbargerEncode(m);
                addBurst(bits, start, length, rng);
                cases++;

                if (hagelbargerDecode(bits) != m)
                {
                    failures++;
                    failedStarts.push_back(start);
                }
            }
        }
    }

    if (failures)
    {
        std::fprintf(stderr, "single bursts: %d of %d not corrected, first at start %d\n",
                     failures, cases, failedStarts.front());
    }

    CHECK(failures == 0);
}

// Two bursts with at least 19 error-free bits between them are corrected.
static void testTwoBurstsWithAGapAreCorrected()
{
    std::mt19937 rng(4);
    int failures = 0;
    int cases = 0;

    for (int n = 0; n < 20000; n++)
    {
        const int length1 = 1 + static_cast<int>(rng() % 6);
        const int length2 = 1 + static_cast<int>(rng() % 6);
        const int gap = 19 + static_cast<int>(rng() % 30);
        const int total = length1 + gap + length2;

        if (total > kEncodedBits) {
            continue;
        }

        const int start = static_cast<int>(rng() % (kEncodedBits - total + 1));
        const MessageBytes m = randomMessage(rng);
        EncodedBits bits = hagelbargerEncode(m);

        addBurst(bits, start, length1, rng);
        addBurst(bits, start + length1 + gap, length2, rng);
        cases++;

        if (hagelbargerDecode(bits) != m) {
            failures++;
        }
    }

    if (failures) {
        std::fprintf(stderr, "two bursts: %d of %d not corrected\n", failures, cases);
    }

    CHECK(cases > 1000);
    CHECK(failures == 0);
}

static void testElementsAreUsedByTheirLowestBit()
{
    std::mt19937 rng(5);
    const MessageBytes m = randomMessage(rng);
    EncodedBits bits = hagelbargerEncode(m);

    for (auto& b : bits) {
        b = static_cast<uint8_t>(b | 0xfe);
    }

    CHECK(hagelbargerDecode(bits) == m);
}

int main()
{
    testStandardExample();
    testEncoderMatchesTheFormulas();
    testRoundTripWithoutErrors();
    testSingleBurstsAreCorrected();
    testTwoBurstsWithAGapAreCorrected();
    testElementsAreUsedByTheirLowestBit();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all hagelbarger tests passed\n");
    return 0;
}
