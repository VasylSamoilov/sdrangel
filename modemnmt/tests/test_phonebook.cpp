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

#include "nmtphonebook.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const System k450 = System::Nmt450;
static const std::string kPhone = "3726136";

// Learned values fill the record; a typed value wins and learning leaves it; clearing a typed
// value gives the field back to learning.
static void testTypedWins()
{
    NmtPhoneBook book;

    CHECK(book.learnPassword(k450, kPhone, "943"));
    CHECK(!book.learnPassword(k450, kPhone, "943"));                    // nothing new
    CHECK(book.find(k450, kPhone)->value(NmtPhoneRecord::Password) == "943");
    CHECK(book.edit(k450, kPhone, NmtPhoneRecord::Password, "111"));
    CHECK(!book.learnPassword(k450, kPhone, "943"));                    // typed wins
    CHECK(book.find(k450, kPhone)->value(NmtPhoneRecord::Password) == "111");
    CHECK(book.find(k450, kPhone)->fields[NmtPhoneRecord::Password].typed);
    CHECK(book.edit(k450, kPhone, NmtPhoneRecord::Password, ""));       // back to learning
    CHECK(book.learnPassword(k450, kPhone, "943"));
    CHECK(book.find(k450, kPhone)->value(NmtPhoneRecord::Password) == "943");
    CHECK(!book.find(k450, kPhone)->fields[NmtPhoneRecord::Password].typed);
}

// The SMS centre the phone really used becomes its centre unless one is typed; the list of
// centres used keeps all of them with the traffic area.
static void testSmsCentre()
{
    NmtPhoneBook book;

    CHECK(book.learnSmsCentre(k450, kPhone, "000501", 8, 3));
    CHECK(book.find(k450, kPhone)->value(NmtPhoneRecord::SmsCentre) == "000501");
    book.edit(k450, kPhone, NmtPhoneRecord::SmsCentre, "767");
    CHECK(book.learnSmsCentre(k450, kPhone, "000999", 5, 1));
    CHECK(book.find(k450, kPhone)->value(NmtPhoneRecord::SmsCentre) == "767");
    CHECK(book.find(k450, kPhone)->smsCentresUsed.size() == 2);
    CHECK(book.find(k450, kPhone)->smsCentresUsed[0].number == "000999");
}

// A record from settings merges: typed values win, learned values only fill gaps, lists do not
// shrink (a worker that started again knows less than the book).
static void testMerge()
{
    NmtPhoneBook book;

    book.learnDialled(k450, kPhone, "000501", 8, 3);
    book.learnDialled(k450, kPhone, "112233", 8, 3);
    book.learnPassword(k450, kPhone, "943");

    NmtPhoneRecord saved;
    saved.system = k450;
    saved.number = kPhone;
    saved.fields[NmtPhoneRecord::Name] = {"Nokia", true};
    saved.fields[NmtPhoneRecord::Password] = {"000", false};          // learned elsewhere: does not replace
    saved.dialled.push_back({"1234", 8, 3, 1});
    book.merge(saved);

    const NmtPhoneRecord *r = book.find(k450, kPhone);
    CHECK(r->value(NmtPhoneRecord::Name) == "Nokia" && r->fields[NmtPhoneRecord::Name].typed);
    CHECK(r->value(NmtPhoneRecord::Password) == "943");
    CHECK(r->dialled.size() == 3);

    NmtPhoneRecord shorter;
    shorter.system = k450;
    shorter.number = kPhone;
    book.merge(shorter);
    CHECK(book.find(k450, kPhone)->dialled.size() == 3);
}

// Records are by system and number; areas the phone was heard on, the newest first.
static void testKeysAndAreas()
{
    NmtPhoneBook book;

    book.record(k450, kPhone);
    book.record(System::Nmt900, kPhone);
    CHECK(book.size() == 2);
    CHECK(book.learnArea(k450, kPhone, 8, 3));
    CHECK(!book.learnArea(k450, kPhone, 8, 3));
    CHECK(book.learnArea(k450, kPhone, 5, 1));
    CHECK(book.learnArea(k450, kPhone, 8, 3));
    CHECK(book.find(k450, kPhone)->areas.size() == 2);
    CHECK(book.find(k450, kPhone)->areas.front() == std::make_pair(8, 3));
    CHECK(book.forget(System::Nmt900, kPhone));
    CHECK(book.size() == 1);
    CHECK(!book.find(System::Nmt900, kPhone));
}

int main()
{
    testTypedWins();
    testSmsCentre();
    testMerge();
    testKeysAndAreas();

    if (g_failures)
    {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("phone book tests passed\n");
    return 0;
}
