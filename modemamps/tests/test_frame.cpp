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

#include <cstdint>
#include <cstdio>
#include <vector>

#include "ampsbch.h"
#include "ampsframe.h"
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

static const double kRate = 48000.0;

// The bits as a phone sends them (silence before and after), into the receiver
static void send(ReverseReceiver& rx, const std::vector<uint8_t>& bits, bool invert)
{
    ManchesterModulator mod(kRate, kFskBitRate, kFskDeviation);
    size_t next = 0;
    std::vector<float> block(480, 0.0f);

    rx.process(block.data(), static_cast<int>(block.size()));
    mod.setBitSource([&]() { return next < bits.size() ? static_cast<int>(bits[next++]) : -1; });
    for (;;)
    {
        const int n = mod.render(block.data(), static_cast<int>(block.size()), false);
        for (int i = 0; i < n && invert; i++) {
            block[i] = -block[i];
        }
        rx.process(block.data(), n);
        if (n < static_cast<int>(block.size())) {
            break;
        }
    }
    std::vector<float> zero(480, 0.0f);
    rx.process(zero.data(), static_cast<int>(zero.size()));
}

// A registration: word A (NAWC 1) and word B (NAWC 0). The handler wants the next word while the
// NAWC says so, as the channel will.
static std::vector<uint64_t> registration()
{
    return {encodeWord(words::kReccA, {{"F", 1}, {"NAWC", 1}, {"T", 1}, {"E", 1}, {"SCM", 0xa}, {"MIN1", 0x654321}}),
        encodeWord(words::kReccB, {{"NAWC", 0}, {"ORDQ", 3}, {"ORDER", kOrderRegistration}, {"MIN2", 0x155}})};
}

static std::vector<ReverseBurst> receive(ReverseReceiver& rx, const std::vector<uint8_t>& bits, bool invert)
{
    std::vector<ReverseBurst> bursts;

    rx.setBurstHandler([&](const ReverseBurst& b) {
        bursts.push_back(b);
        return b.okCount > 0 && wordField(words::kReccA, b.word(), "NAWC") > 0;
    });
    send(rx, bits, invert);
    return bursts;
}

static void testRecc()
{
    const std::vector<uint64_t> w = registration();

    for (bool invert : {false, true})
    {
        ReverseReceiver rx(kRate, false);
        const std::vector<ReverseBurst> b = receive(rx, reccMessage(2, w), invert);

        CHECK(b.size() == 2);
        if (b.size() != 2) {
            continue;
        }
        CHECK(b[0].first && b[0].dcc == 2 && b[0].okCount == 5 && b[0].word() == w[0]);
        CHECK(!b[1].first && b[1].dcc == -1 && b[1].okCount == 5 && b[1].word() == w[1]);
        CHECK(b[0].negative == invert && b[1].negative == invert);
        CHECK(b[0].level > 0.8 && b[0].quality > 0.7);
    }
}

// On the RVC the first, third and fifth repeat are read, the others are dotting and sync
static void testRvc()
{
    const uint64_t w = encodeWord(words::kRvcOrderConfirmation, {{"F", 1}, {"T", 1}, {"ORDER", kOrderAlert}});
    ReverseReceiver rx(kRate, false);
    std::vector<ReverseBurst> b;

    rx.setChannel(ReverseReceiver::ChannelRvc);
    rx.setBurstHandler([&](const ReverseBurst& burst) {
        b.push_back(burst);
        return false;
    });
    send(rx, rvcMessage({w}), false);
    CHECK(!b.empty());
    if (!b.empty())
    {
        CHECK(b[0].dcc == -1 && b[0].word() == w);
        CHECK(b[0].ok[0] && !b[0].ok[1] && b[0].ok[2] && !b[0].ok[3] && b[0].ok[4] && b[0].okCount == 3);
    }
}

// A sync with one bit wrong only when tolerant
static void testTolerant()
{
    std::vector<uint8_t> bits = reccMessage(1, registration());
    bits[kReccDottingBits + 3] ^= 1;

    ReverseReceiver strict(kRate, false);
    CHECK(receive(strict, bits, false).empty());
    ReverseReceiver tolerant(kRate, true);
    const std::vector<ReverseBurst> b = receive(tolerant, bits, false);
    CHECK(b.size() == 2 && b[0].dcc == 1);
}

static void testDcc()
{
    for (int dcc = 0; dcc < 4; dcc++)
    {
        CHECK(ReverseReceiver::decodeDcc(codedDcc(dcc)) == dcc);
        for (int j = 0; j < 7; j++) {
            CHECK(ReverseReceiver::decodeDcc(static_cast<uint8_t>(codedDcc(dcc) ^ (1 << j))) == dcc);
        }
    }
    // two bits wrong: none (0000011 is 2 bits from 0000000 and from 1100011)
    CHECK(ReverseReceiver::decodeDcc(0x03) == -1);
}

int main()
{
    testRecc();
    testRvc();
    testTolerant();
    testDcc();

    if (g_failures == 0) {
        std::printf("all frame tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
