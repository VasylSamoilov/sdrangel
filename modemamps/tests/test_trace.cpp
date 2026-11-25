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

#include "ampsfvc.h"
#include "ampsmessages.h"
#include "ampstrace.h"
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

#define CHECK_TEXT(text, expected) \
    do { \
        const std::string t_ = (text); \
        if (t_ != (expected)) { \
            std::fprintf(stderr, "%s:%d: got \"%s\"\n    expected \"%s\"\n", __FILE__, __LINE__, t_.c_str(), (expected)); \
            g_failures++; \
        } \
    } while (0)

static const uint32_t kMin1 = ReccAssembler::min1Of("2177526");
static const uint16_t kMin2 = ReccAssembler::min2Of("216");

static void testOrderNames()
{
    CHECK_TEXT(ampsOrderName(kOrderAlert, 0, 0), "Alert");
    CHECK_TEXT(ampsOrderName(kOrderChangePower, 4, 0), "Change Power to Power Level 4");
    CHECK_TEXT(ampsOrderName(kOrderRegistration, 3, 0), "Autonomous Registration - Make whereabouts known");
    CHECK_TEXT(ampsOrderName(kOrderRegistration, 3, 1), "Autonomous Registration - Power Down");
    CHECK_TEXT(ampsOrderName(kOrderMessageWaiting, 0, 2), "Voice Message Waiting (2)");
    CHECK_TEXT(ampsOrderName(0x1c, 2, 1), "ORDER 28 ORDQ 2 MSG_TYPE 1");
}

static void testFocc()
{
    CHECK_TEXT(describeFocc(FoccMessage::order(0, kMin1, kMin2, kOrderPage, 0, 0)), "Page to 2162177526");
    CHECK_TEXT(describeFocc(FoccMessage::order(0, kMin1, kMin2, kOrderRegistration, 3, 0)),
        "Autonomous Registration - Make whereabouts known to 2162177526");
    CHECK_TEXT(describeFocc(FoccMessage::voiceChannel(0, kMin1, kMin2, 1, 4, 356)),
        "Initial Voice Channel Designation to 2162177526: CHAN 356, SCC 1, VMAC 4");
}

static void testRecc()
{
    ReccMessage m;
    m.t = 1;
    m.min1 = kMin1;
    m.min2 = kMin2;
    m.scm = 0x0a;
    m.hasEsn = true;
    m.esn = 0x8212345f;
    m.dialled = "555";
    CHECK_TEXT(describeRecc(m), "Origination from 2162177526 to 555, ESN 0x8212345f, SCM 0x0a");

    m.t = 0;
    m.dialled.clear();
    m.hasEsn = false;
    CHECK_TEXT(describeRecc(m), "Page Response from 2162177526, SCM 0x0a");

    m.t = 1;
    m.order = kOrderRegistration;
    m.ordq = 3;
    m.hasPci = true;
    m.mspc = 1;
    m.mscap = 0;
    CHECK_TEXT(describeRecc(m), "Autonomous Registration - Make whereabouts known from 2162177526, SCM 0x0a, MSPC 1, MSCAP 0");
}

static void testRvc()
{
    const uint64_t confirm = encodeWord(words::kRvcOrderConfirmation, {{"F", 1}, {"NAWC", 0}, {"T", 1}, {"ORDER", kOrderRelease}});
    CHECK_TEXT(describeRvc(confirm), "Order Confirmation: Release");
    const uint64_t digits = encodeWord(words::kRvcDigits, {{"F", 1}, {"T", 0}, {"DIGIT 1", 5}, {"DIGIT 2", 10}, {"DIGIT 3", 11}});
    CHECK_TEXT(describeRvc(digits), "Called Address: 50*");
}

static void testFvc()
{
    FvcTrace trace;

    // a one-word order: its line at once
    std::vector<std::string> lines = trace.add(fvc::order(0, kOrderAlert, 0, 0));
    CHECK(lines.size() == 1);
    if (lines.size() == 1) {
        CHECK_TEXT(lines[0], "Alert");
    }

    // the alert with info of osmocom (the same words): one line after the fifth word
    const std::vector<uint64_t> words = fvc::alertWithInfo(0, "1000001");
    CHECK(words.size() == 5);
    for (size_t i = 0; i + 1 < words.size(); i++) {
        CHECK(trace.add(words[i]).empty());
    }
    lines = trace.add(words.back());
    CHECK(lines.size() == 1);
    if (lines.size() == 1) {
        CHECK_TEXT(lines[0], "Alert With Info: 1000001 (PI presentation allowed, SI network provided, SIGNAL 1)");
    }

    // cut short by the next order: both shown
    trace.add(words[0]);
    trace.add(words[1]);
    lines = trace.add(fvc::order(0, kOrderRelease, 0, 0));
    CHECK(lines.size() == 2);
    if (lines.size() == 2)
    {
        CHECK_TEXT(lines[0], "Alert With Info: no number (PI presentation allowed, SI network provided, SIGNAL 1) (cut short after 2 words)");
        CHECK_TEXT(lines[1], "Release");
    }

    // no caller id: PI number not available
    const std::vector<uint64_t> empty = fvc::alertWithInfo(0, "");
    lines.clear();
    for (uint64_t w : empty)
    {
        for (const std::string& l : trace.add(w)) {
            lines.push_back(l);
        }
    }
    CHECK(lines.size() == 1);
    if (lines.size() == 1) {
        CHECK_TEXT(lines[0], "Alert With Info: no number (PI number not available, SI network provided, SIGNAL 1)");
    }

    // the flash with info: its own word 2
    lines.clear();
    for (uint64_t w : fvc::flashWithInfo(0, "Call waiting"))
    {
        for (const std::string& l : trace.add(w)) {
            lines.push_back(l);
        }
    }
    CHECK(lines.size() == 1);
    if (lines.size() == 1) {
        CHECK_TEXT(lines[0], "Flash With Info: Call waiting (PI presentation allowed, SI network provided)");
    }

    // a next word without its first: nothing
    trace.reset();
    CHECK(trace.add(words[2]).empty());
}

int main()
{
    testOrderNames();
    testFocc();
    testRecc();
    testRvc();
    testFvc();

    if (g_failures == 0) {
        std::printf("all trace tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
