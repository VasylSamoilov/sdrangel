///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

#include "ampsbch.h"
#include "ampsword.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// A forward word of 28 message bits and a reverse word of 36, as tables (the layouts of the System
// Parameter Overhead word 1 and of a reverse word; the real tables are tested with their messages)
static const WordDef kForward = {"forward", false,
    {{"T1T2", 2}, {"DCC", 2}, {"SID1", 14}, {"EP", 1}, {"AUTH", 1}, {"PCI", 1}, {"NAWC", 4}, {"OHD", 3}}};
static const WordDef kReverse = {"reverse", true,
    {{"F", 1}, {"NAWC", 3}, {"T", 1}, {"S", 1}, {"E", 1}, {"ER", 1}, {"SCM", 4}, {"MIN1", 24}}};

static void testRoundTrip()
{
    CHECK(kForward.messageBits() == kForwardMessageBits);
    CHECK(kReverse.messageBits() == kReverseMessageBits);

    const WordValues in = {{"T1T2", 3}, {"DCC", 1}, {"SID1", 54}, {"EP", 0}, {"AUTH", 1}, {"PCI", 0}, {"NAWC", 5}, {"OHD", 6}};
    const uint64_t word = encodeWord(kForward, in);
    WordValues out;

    CHECK(word < (1ull << kForwardWordBits));
    CHECK(decodeWord(kForward, word, out) && out == in);
    CHECK(wordField(kForward, word, "SID1") == 54 && wordField(kForward, word, "OHD") == 6);
    CHECK(wordField(kForward, word, "nothing") == 0);
    // the message bits are the fields in order, the most significant first; then the parity
    CHECK((word >> kBchParityBits) == ((3ull << 26) | (1ull << 24) | (54ull << 10) | (1ull << 8) | (5ull << 3) | 6));

    const WordValues reverseIn = {{"F", 1}, {"NAWC", 2}, {"T", 1}, {"SCM", 0xa}, {"MIN1", 0xabcdef}};
    WordValues reverseOut;
    const uint64_t reverseWord = encodeWord(kReverse, reverseIn);
    CHECK(reverseWord < (1ull << kReverseWordBits));
    CHECK(decodeWord(kReverse, reverseWord, reverseOut));
    CHECK(reverseOut["MIN1"] == 0xabcdef && reverseOut["SCM"] == 0xa && reverseOut["S"] == 0);
}

static void testParityAndWidth()
{
    WordValues out;
    const uint64_t word = encodeWord(kForward, {{"SID1", 1234}});

    // a bit error is found (the values are read anyway)
    CHECK(!decodeWord(kForward, word ^ (1ull << 20), out));
    CHECK(!decodeWord(kForward, word ^ 1ull, out));
    // a value wider than its field is cut to it
    CHECK(wordField(kForward, encodeWord(kForward, {{"OHD", 0xf}}), "OHD") == 7);
    CHECK(wordField(kForward, encodeWord(kForward, {{"OHD", 0xf}}), "NAWC") == 0);
}

int main()
{
    testRoundTrip();
    testParityAndWidth();

    if (g_failures == 0) {
        std::printf("all word tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
