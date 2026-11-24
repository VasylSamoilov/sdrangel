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
#include "ampsmessages.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// Every table fills its word: 28 message bits forward, 36 reverse (TIA/EIA-553-A: 40 and 48 with P)
static void testSizes()
{
    const WordDef *forward[] = {&words::kAbbreviatedAddress, &words::kExtendedAddressOrder, &words::kExtendedAddressChannel,
        &words::kSystemParameter1, &words::kSystemParameter2, &words::kRegistrationIncrement, &words::kRegistrationId,
        &words::kRescan, &words::kLocationArea, &words::kNewAccessChannelSet, &words::kOverloadControl,
        &words::kAccessTypeParameters, &words::kAccessAttemptParameters,
        &words::kControlFiller, &words::kFvcOrder, &words::kFvcHandoff};
    const WordDef *reverse[] = {&words::kReccA, &words::kReccB, &words::kReccSerial, &words::kReccPci, &words::kReccDigits,
        &words::kRvcOrderConfirmation, &words::kRvcDigits};

    for (const WordDef *d : forward) {
        CHECK(!d->reverse && d->messageBits() == kForwardMessageBits);
    }
    for (const WordDef *d : reverse) {
        CHECK(d->reverse && d->messageBits() == kReverseMessageBits);
    }
}

// A page with a voice channel designation, as the base station sends it, read back as the phone does
static void testChannelDesignation()
{
    const uint64_t w1 = encodeWord(words::kAbbreviatedAddress, {{"T1T2", kT1T2First}, {"DCC", 0}, {"MIN1", 0x123456}});
    const uint64_t w2 = encodeWord(words::kExtendedAddressChannel,
        {{"T1T2", kT1T2Next}, {"SCC", 1}, {"MIN2", 0x2ab}, {"VMAC", 2}, {"CHAN", 356}});
    WordValues v;

    CHECK(decodeWord(words::kAbbreviatedAddress, w1, v) && v["T1T2"] == 1 && v["MIN1"] == 0x123456);
    CHECK(decodeWord(words::kExtendedAddressChannel, w2, v) && v["T1T2"] == 2 && v["SCC"] == 1 && v["CHAN"] == 356);
    // the same word read as the order form (SCC = 11 tells them apart): its SCC is not 11
    CHECK(wordField(words::kExtendedAddressOrder, w2, "SCC") != 3);
    // the overhead word 1 of the system of the phone (SID 54) has OHD 110 at its end
    const uint64_t spom = encodeWord(words::kSystemParameter1,
        {{"T1T2", kT1T2Overhead}, {"SID1", 54}, {"NAWC", 1}, {"OHD", kOhdSystemParameter1}});
    CHECK(((spom >> kBchParityBits) & 7) == kOhdSystemParameter1 && wordField(words::kSystemParameter1, spom, "SID1") == 54);
}

// A registration of a phone on the RECC: word A then B, read back
static void testRegistration()
{
    const uint64_t a = encodeWord(words::kReccA, {{"F", 1}, {"NAWC", 1}, {"T", 1}, {"E", 1}, {"SCM", 0xa}, {"MIN1", 0x654321}});
    const uint64_t b = encodeWord(words::kReccB,
        {{"NAWC", 0}, {"ORDQ", 3}, {"ORDER", kOrderRegistration}, {"MIN2", 0x155}});
    WordValues v;

    CHECK(decodeWord(words::kReccA, a, v) && v["F"] == 1 && v["T"] == 1 && v["MIN1"] == 0x654321);
    CHECK(decodeWord(words::kReccB, b, v) && v["ORDER"] == kOrderRegistration && v["ORDQ"] == 3 && v["MIN2"] == 0x155);
}

static void testCodes()
{
    CHECK(satFrequency(0) == 5970.0 && satFrequency(1) == 6000.0 && satFrequency(2) == 6030.0 && satFrequency(3) == 0.0);
    CHECK(digitCode('1') == 1 && digitCode('9') == 9 && digitCode('0') == 10 && digitCode('*') == 11 && digitCode('#') == 12);
    CHECK(digitCode('A') == -1);
    for (char c : {'0', '1', '5', '9', '*', '#'}) {
        CHECK(codeDigit(digitCode(c)) == c);
    }
    CHECK(codeDigit(0) == 0 && codeDigit(13) == 0);
}

int main()
{
    testSizes();
    testChannelDesignation();
    testRegistration();
    testCodes();

    if (g_failures == 0) {
        std::printf("all message tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
