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
#include <vector>

#include "ampsbch.h"
#include "ampsfocc.h"
#include "ampsmessages.h"
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

static uint64_t ohd(uint64_t word)
{
    return wordField(words::kSystemParameter1, word, "OHD");
}

static uint64_t t1t2(uint64_t word)
{
    return wordField(words::kAbbreviatedAddress, word, "T1T2");
}

static bool isFiller(uint64_t word)
{
    return t1t2(word) == kT1T2Overhead && ohd(word) == kOhdControlFiller;
}

static FoccTransmitter transmitter()
{
    FoccTransmitter tx;
    SystemInfo si;

    si.sid = 54;
    tx.setSystemInfo(si);
    tx.setRegistrationId([]() { return 1000u; });
    return tx;
}

// Idle: the train of 6 words (osmocom amps -S sid=54) every 17 frames, the filler in between
static void testIdle()
{
    FoccTransmitter tx = transmitter();

    for (int train = 0; train < 3; train++)
    {
        const uint64_t w1 = tx.nextWord();
        CHECK(ohd(w1) == kOhdSystemParameter1 && wordField(words::kSystemParameter1, w1, "NAWC") == 5);
        for (int i = 1; i < 6; i++) {
            CHECK(!isFiller(tx.nextWord()));
        }
        for (int i = 6; i < FoccTransmitter::kOverheadRepeat; i++) {
            CHECK(isFiller(tx.nextWord()));
        }
    }
}

// A voice channel designation: word 1 (T1T2 01) and word 2 (T1T2 10) three times after the train
static void testMessage()
{
    FoccTransmitter tx = transmitter();
    const FoccMessage m = FoccMessage::voiceChannel(0, 0x123456, 0x2ab, 1, 4, 356);

    CHECK(t1t2(m.word1) == kT1T2First && t1t2(m.word2) == kT1T2Next);
    CHECK(wordField(words::kExtendedAddressChannel, m.word2, "CHAN") == 356);
    CHECK(wordField(words::kExtendedAddressChannel, m.word2, "SCC") == 1);
    CHECK(bchCheck(m.word1, kForwardMessageBits) && bchCheck(m.word2, kForwardMessageBits));

    tx.queue(m);
    CHECK(tx.queued() == 1);
    for (int i = 0; i < 6; i++) {
        CHECK(ohd(tx.nextWord()) != kOhdControlFiller);           // the train first
    }
    for (int r = 0; r < 3; r++)
    {
        CHECK(tx.nextWord() == m.word1);
        CHECK(tx.nextWord() == m.word2);
    }
    CHECK(tx.queued() == 0);
    CHECK(isFiller(tx.nextWord()));

    const FoccMessage page = FoccMessage::order(0, 0x123456, 0x2ab, kOrderPage, 0, 0);
    CHECK(wordField(words::kExtendedAddressOrder, page.word2, "SCC") == 3);
    CHECK(wordField(words::kExtendedAddressOrder, page.word2, "ORDER") == kOrderPage);
}

// A message over the end of the 17 frames: the next train waits until it is done (osmocom)
static void testMessageOverTrain()
{
    FoccTransmitter tx = transmitter();
    const FoccMessage m = FoccMessage::order(0, 0x111111, 0x111, kOrderAlert, 0, 0);

    for (int i = 0; i < 14; i++) {
        tx.nextWord();                                          // train and filler up to frame 14
    }
    tx.queue(m);
    for (int r = 0; r < 3; r++)
    {
        CHECK(tx.nextWord() == m.word1);                        // frames 14 .. 19
        CHECK(tx.nextWord() == m.word2);
    }
    CHECK(ohd(tx.nextWord()) == kOhdSystemParameter1);          // then the train
}

// The message source is asked when nothing is being sent, at frames that are not the train: a
// message it hands over goes out three times before it is asked again (osmocom's scheduling)
static void testSource()
{
    FoccTransmitter tx = transmitter();
    const FoccMessage m = FoccMessage::order(0, 0x222222, 0x222, kOrderRegistration, 3, 0);
    int asked = 0;

    tx.setMessageSource([&](FoccMessage& out) {
        asked++;
        if (asked == 1)
        {
            out = m;
            return true;
        }
        return false;
    });
    for (int i = 0; i < 6; i++) {
        tx.nextWord();                                          // the train
    }
    CHECK(asked == 0);
    for (int r = 0; r < 3; r++)
    {
        CHECK(tx.nextWord() == m.word1);
        CHECK(tx.nextWord() == m.word2);
    }
    CHECK(asked == 1);
    CHECK(isFiller(tx.nextWord()) && asked == 2);
    CHECK(isFiller(tx.nextWord()) && asked == 3);
}

// The bits: frames of 463, the word in both streams, the busy-idle bits idle (1)
static void testBits()
{
    FoccTransmitter tx = transmitter();
    std::vector<uint8_t> stream;

    for (int i = 0; i < 2 * kFoccTrainBits; i++) {
        stream.push_back(static_cast<uint8_t>(tx.nextBit()));
    }
    FoccTransmitter ref = transmitter();
    const uint64_t w = ref.nextWord();
    const std::vector<uint8_t> first = foccTrain(w, w, false);
    CHECK(first.size() == static_cast<size_t>(kFoccTrainBits));
    CHECK(std::vector<uint8_t>(stream.begin(), stream.begin() + kFoccTrainBits) == first);
    for (int i = 0; i < kFoccTrainBits; i++) {
        if (foccBusyIdleBit(i)) {
            CHECK(stream[i] == 1);
        }
    }
}

int main()
{
    testIdle();
    testMessage();
    testMessageOverTrain();
    testSource();
    testBits();

    if (g_failures == 0) {
        std::printf("all FOCC tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
