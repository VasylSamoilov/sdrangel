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
#include "ampssysinfo.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static uint64_t field(const WordDef& def, uint64_t word, const char *name)
{
    return wordField(def, word, name);
}

// osmocom amps -S sid=54, the rest its defaults: word 1, word 2, registration ID, registration
// increment (300, not 450), location area (power up and down registration), access type (BIS 0 and
// PCI): 6 words
static void testDefaultTrain()
{
    SystemInfo si;
    si.sid = 54;
    const std::vector<uint64_t> t = si.overheadTrain(0x12345678);

    CHECK(t.size() == 6);
    if (t.size() != 6) {
        return;
    }
    for (uint64_t w : t) {
        CHECK(bchCheck(w, kForwardMessageBits) && field(words::kSystemParameter1, w, "T1T2") == kT1T2Overhead);
    }
    CHECK(field(words::kSystemParameter1, t[0], "OHD") == kOhdSystemParameter1);
    CHECK(field(words::kSystemParameter1, t[0], "SID1") == 27 && field(words::kSystemParameter1, t[0], "NAWC") == 5);
    CHECK(field(words::kSystemParameter2, t[1], "OHD") == kOhdSystemParameter2);
    CHECK(field(words::kSystemParameter2, t[1], "S") == 1 && field(words::kSystemParameter2, t[1], "E") == 1);
    CHECK(field(words::kSystemParameter2, t[1], "RCF") == 1 && field(words::kSystemParameter2, t[1], "CPA") == 1);
    CHECK(field(words::kSystemParameter2, t[1], "N-1") == 20 && field(words::kSystemParameter2, t[1], "CMAX-1") == 20);
    CHECK(field(words::kSystemParameter2, t[1], "END") == 0);
    CHECK(field(words::kRegistrationId, t[2], "OHD") == kOhdRegistrationId);
    CHECK(field(words::kRegistrationId, t[2], "REGID") == 0x45678);
    CHECK(field(words::kRegistrationIncrement, t[3], "ACT") == kActRegistrationIncrement);
    CHECK(field(words::kRegistrationIncrement, t[3], "REGINCR") == 300);
    CHECK(field(words::kLocationArea, t[4], "ACT") == kActLocationArea);
    CHECK(field(words::kLocationArea, t[4], "PUREG") == 1 && field(words::kLocationArea, t[4], "LREG") == 0);
    CHECK(field(words::kAccessTypeParameters, t[5], "ACT") == kActAccessTypeParameters);
    CHECK(field(words::kAccessTypeParameters, t[5], "BIS") == 0 && field(words::kAccessTypeParameters, t[5], "BSCAP") == 1);
    CHECK(field(words::kAccessTypeParameters, t[5], "PCI_HOME") == 1 && field(words::kAccessTypeParameters, t[5], "END") == 1);
    for (int i = 2; i < 5; i++) {
        CHECK(field(words::kRegistrationId, t[i], "END") == 0);
    }
}

// The global actions at their defaults are left out; a changed one comes in, the END moves
static void testDefaultsLeftOut()
{
    SystemInfo si;
    si.regincr = 450;
    si.pureg = 0;
    si.pdreg = 0;
    si.bis = 1;
    si.pciHome = 0;
    si.pciRoam = 0;
    si.bscap = 0;
    std::vector<uint64_t> t = si.overheadTrain(1);

    CHECK(t.size() == 3);
    CHECK(field(words::kSystemParameter1, t[0], "NAWC") == 2 && field(words::kRegistrationId, t[2], "END") == 1);
    // busy-idle on: RCF 0
    CHECK(field(words::kSystemParameter2, t[1], "RCF") == 0);

    si.olc = 0xfffe;
    t = si.overheadTrain(1);
    CHECK(t.size() == 4 && field(words::kOverloadControl, t[3], "ACT") == kActOverloadControl);
    CHECK(field(words::kOverloadControl, t[3], "OLC 0") == 0 && field(words::kOverloadControl, t[3], "OLC 1") == 1);
    CHECK(field(words::kOverloadControl, t[3], "END") == 1 && field(words::kRegistrationId, t[2], "END") == 0);
}

static void testFiller()
{
    SystemInfo si;
    si.dcc = 2;
    const uint64_t f = si.controlFiller();

    CHECK(bchCheck(f, kForwardMessageBits));
    CHECK(field(words::kControlFiller, f, "T1T2") == 3 && field(words::kControlFiller, f, "DCC") == 2);
    CHECK(field(words::kControlFiller, f, "010111") == 0x17 && field(words::kControlFiller, f, "11") == 3);
    CHECK(field(words::kControlFiller, f, "1") == 1 && field(words::kControlFiller, f, "1111") == 0xf);
    CHECK(field(words::kControlFiller, f, "CMAC") == 4 && field(words::kControlFiller, f, "WFOM") == 1);
    CHECK(field(words::kControlFiller, f, "OHD") == kOhdControlFiller);
}

int main()
{
    testDefaultTrain();
    testDefaultsLeftOut();
    testFiller();

    if (g_failures == 0) {
        std::printf("all system information tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
