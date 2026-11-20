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

#include "nmtphoneinfo.h"
#include "nmttracefold.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// The password is taken when two frames in a row agree; a single false decode does not stick.
static void testPassword()
{
    NmtPhoneInfo info;

    CHECK(!info.hasPassword());
    CHECK(info.password().empty());
    info.notePassword(0x943);
    CHECK(!info.hasPassword());
    info.notePassword(0x943);
    CHECK(info.hasPassword());
    CHECK(info.password() == "943");
    // one stray value does not replace it, two do
    info.notePassword(0x1a2);
    CHECK(info.password() == "943");
    info.notePassword(0x943);
    info.notePassword(0x0b4);
    info.notePassword(0x0b4);
    CHECK(info.password() == "0B4");
}

// Dialled numbers: the newest first, a repeat moves to the front with its count, at most five;
// the same number on a network with another traffic area is another entry.
static void testDialled()
{
    NmtPhoneInfo info;

    info.noteDialled("000501", 8, 3);
    info.noteDialled("112233", 8, 3);
    info.noteDialled("000501", 8, 3);
    CHECK(info.dialled().size() == 2);
    CHECK(info.dialled()[0].number == "000501" && info.dialled()[0].count == 2);
    CHECK(info.dialled()[1].number == "112233" && info.dialled()[1].count == 1);
    info.noteDialled("000501", 6, 1);
    CHECK(info.dialled().size() == 3);
    CHECK(info.dialled()[0].y1 == 6);
    for (int i = 0; i < 10; i++) {
        info.noteDialled(std::to_string(1000 + i), 8, 3);
    }
    CHECK(info.dialled().size() == NmtPhoneInfo::kMaxDialled);
    CHECK(info.dialled()[0].number == "1009");
    info.noteDialled("", 8, 3);
    CHECK(info.dialled()[0].number == "1009");
    CHECK(NmtPhoneInfo::text(info.dialled(), false).find("1009, 1008") == 0);
}

// The SMS centre comes with the traffic area it was used on.
static void testSmsCentre()
{
    NmtPhoneInfo info;

    CHECK(NmtPhoneInfo::text(info.smsCentres(), true).empty());
    info.noteSmsCentre("000501", 8, 3);
    CHECK(NmtPhoneInfo::text(info.smsCentres(), true) == "000501 (Y1 8, area 3)");
    info.noteSmsCentre("767", 5, 1);
    CHECK(NmtPhoneInfo::text(info.smsCentres(), true) == "767 (Y1 5, area 1), 000501 (Y1 8, area 3)");
}

// A run of the same frame: the first is shown, the repeats are counted and shown when the run ends,
// by another frame or by the channel going quiet.
static void testTraceFold()
{
    NmtTraceFold fold;

    CHECK(!fold.repeat("15 idle"));                 // shown
    CHECK(fold.flush() == 0);
    CHECK(fold.repeat("15 idle"));
    CHECK(fold.repeat("15 idle"));
    CHECK(fold.running());
    CHECK(!fold.repeat("14a digit"));               // a new run: the old one had 2 repeats
    CHECK(fold.flush() == 2);
    CHECK(fold.flush() == 0);                       // told once
    CHECK(!fold.repeat("14b digit"));
    CHECK(fold.flush() == 0);                       // a run of one frame has nothing to tell
    CHECK(fold.repeat("14b digit"));
    CHECK(fold.end() == 1);                         // the channel went quiet
    CHECK(fold.end() == 0);
    CHECK(!fold.repeat("14b digit"));               // after the end the same frame starts a new run
}

int main()
{
    testTraceFold();
    testPassword();
    testDialled();
    testSmsCentre();

    if (g_failures)
    {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("phone info tests passed\n");
    return 0;
}
