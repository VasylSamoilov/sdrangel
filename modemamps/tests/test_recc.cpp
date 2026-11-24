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
#include <string>
#include <vector>

#include "ampsmessages.h"
#include "ampsrecc.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const uint32_t kMin1 = ReccAssembler::min1Of("5551234");
static const uint16_t kMin2 = ReccAssembler::min2Of("312");

struct Received
{
    std::vector<ReccMessage> messages;
    std::vector<std::string> drops;
};

static ReccAssembler assembler(Received& r)
{
    ReccAssembler a;
    a.setSystemInfo(SystemInfo());              // S 1, PCI home and roam 1, as osmocom's defaults
    a.setMessageSink([&r](const ReccMessage& m) { r.messages.push_back(m); });
    a.setDropSink([&r](const char* why) { r.drops.push_back(why); });
    return a;
}

static uint64_t wordA(int nawc, int t)
{
    return encodeWord(words::kReccA, {{"F", 1}, {"NAWC", nawc}, {"T", t}, {"S", 1}, {"E", 1}, {"SCM", 0xa}, {"MIN1", kMin1}});
}

static uint64_t wordB(int nawc, int order, int ordq)
{
    return encodeWord(words::kReccB, {{"NAWC", nawc}, {"ORDQ", ordq}, {"ORDER", order}, {"SCM(4)", 1}, {"MIN2", kMin2}});
}

static uint64_t serial(int nawc)
{
    return encodeWord(words::kReccSerial, {{"NAWC", nawc}, {"SERIAL", 0x89abcdef}});
}

static uint64_t digits(int nawc, const std::string& d)
{
    WordValues v = {{"NAWC", nawc}};
    for (size_t i = 0; i < d.size() && i < 8; i++) {
        v["DIGIT " + std::to_string(i + 1)] = digitCode(d[i]);
    }
    return encodeWord(words::kReccDigits, v);
}

// sends the words as the receiver does: the first after a sync, the next ones while it wants more
static void send(ReccAssembler& a, const std::vector<uint64_t>& w)
{
    for (size_t i = 0; i < w.size(); i++)
    {
        const bool more = a.word(w[i], i == 0);
        CHECK(more == (i + 1 < w.size()));
    }
}

// A registration: A, B (ORDER 13), the serial number and the capability word
static void testRegistration()
{
    Received r;
    ReccAssembler a = assembler(r);

    send(a, {wordA(3, 1), wordB(2, kOrderRegistration, 3), serial(1),
        encodeWord(words::kReccPci, {{"NAWC", 0}, {"MSPC", 2}, {"MSCAP", 1}})});
    CHECK(r.messages.size() == 1 && r.drops.empty());
    if (r.messages.empty()) {
        return;
    }
    const ReccMessage& m = r.messages[0];
    CHECK(m.t == 1 && m.order == kOrderRegistration && m.ordq == 3 && m.words == 4);
    CHECK(m.min1 == kMin1 && m.min2 == kMin2 && m.scm == 0x1a);
    CHECK(m.hasEsn && m.esn == 0x89abcdef && m.hasPci && m.mspc == 2 && m.mscap == 1);
    CHECK(m.dialled.empty());
}

// An origination: A, B (ORDER 0), the serial number, no capability word, two words of digits
static void testOrigination()
{
    Received r;
    ReccAssembler a = assembler(r);

    send(a, {wordA(4, 1), wordB(3, kOrderPage, 0), serial(2), digits(1, "12345678"), digits(0, "901")});
    CHECK(r.messages.size() == 1 && r.drops.empty());
    if (!r.messages.empty()) {
        CHECK(r.messages[0].dialled == "12345678901" && r.messages[0].hasEsn && !r.messages[0].hasPci);
    }
}

// A page response: A (T 0), B (ORDER 0), the serial number
static void testPageResponse()
{
    Received r;
    ReccAssembler a = assembler(r);

    send(a, {wordA(2, 0), wordB(1, kOrderPage, 0), serial(0)});
    CHECK(r.messages.size() == 1 && !r.messages.empty() && r.messages[0].t == 0 && r.messages[0].dialled.empty());
}

// A word count that does not follow, a lost word, a burst without word A: no message
static void testBroken()
{
    Received r;
    ReccAssembler a = assembler(r);

    CHECK(a.word(wordA(2, 1), true));
    CHECK(!a.word(wordB(0, kOrderRegistration, 3), false));      // NAWC 0 after 2
    CHECK(r.messages.empty() && r.drops.size() == 1);

    CHECK(a.word(wordA(2, 1), true));
    a.lost();
    CHECK(r.messages.empty() && r.drops.size() == 2);

    CHECK(!a.word(wordB(1, kOrderRegistration, 3), true));       // not word A after a sync
    CHECK(r.messages.empty() && r.drops.size() == 3);
}

// The MIN of a number and back (TIA/EIA-553-A 2.3.1: 0 counts as 10)
static void testMin()
{
    CHECK(ReccAssembler::min1Digits(kMin1) == "5551234" && ReccAssembler::min2Digits(kMin2) == "312");
    CHECK(ReccAssembler::min1Digits(ReccAssembler::min1Of("0000000")) == "0000000");
    CHECK(ReccAssembler::min1Of("1111111") == ((0u << 14) | (1u << 10) | 0u));
    CHECK(ReccAssembler::min2Of("000") == 999 && ReccAssembler::min2Of("111") == 0);
    CHECK(ReccAssembler::min1Of("123") == 0);
}

int main()
{
    testRegistration();
    testOrigination();
    testPageResponse();
    testBroken();
    testMin();

    if (g_failures == 0) {
        std::printf("all RECC tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
