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
#include "ampsfvc.h"
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

static void testWords()
{
    const uint64_t alert = fvc::order(1, kOrderAlert, 0, 0);

    CHECK(bchCheck(alert, kForwardMessageBits));
    CHECK(wordField(words::kFvcOrder, alert, "T1T2") == kT1T2Next && wordField(words::kFvcOrder, alert, "SCC") == 3);
    CHECK(wordField(words::kFvcOrder, alert, "PSCC") == 1 && wordField(words::kFvcOrder, alert, "ORDER") == kOrderAlert);

    const uint64_t handoff = fvc::handoff(2, 1, 0, 4, 359);
    CHECK(bchCheck(handoff, kForwardMessageBits));
    CHECK(wordField(words::kFvcHandoff, handoff, "SCC") == 2 && wordField(words::kFvcHandoff, handoff, "PSCC") == 1);
    CHECK(wordField(words::kFvcHandoff, handoff, "VMAC") == 4 && wordField(words::kFvcHandoff, handoff, "CHAN") == 359);
}

// Each queued word is its 11 repeats, the words one after the other, then -1 (back to audio)
static void testStream()
{
    FvcTransmitter tx;
    const uint64_t alert = fvc::order(0, kOrderAlert, 0, 0);
    const uint64_t release = fvc::order(0, kOrderRelease, 0, 0);
    std::vector<uint8_t> bits;
    int bit;

    CHECK(!tx.busy() && tx.nextBit() == -1);
    tx.queue(alert);
    tx.queue(release);
    CHECK(tx.busy());
    while ((bit = tx.nextBit()) >= 0) {
        bits.push_back(static_cast<uint8_t>(bit));
    }
    std::vector<uint8_t> expected = fvcWord(alert);
    const std::vector<uint8_t> second = fvcWord(release);
    expected.insert(expected.end(), second.begin(), second.end());
    CHECK(bits.size() == 2u * kFvcWordBits && bits == expected);
    CHECK(!tx.busy());

    tx.queue(alert);
    tx.nextBit();
    tx.reset();
    CHECK(!tx.busy() && tx.nextBit() == -1);
}

int main()
{
    testWords();
    testStream();

    if (g_failures == 0) {
        std::printf("all FVC tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
