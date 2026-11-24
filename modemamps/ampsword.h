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

#ifndef MODEMAMPS_AMPSWORD_H_
#define MODEMAMPS_AMPSWORD_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "export.h"

// The words of the AMPS data channels (EIA/TIA-553 3.7, 4.7) as tables of fields, as osmocom-analog's
// frame.c: a word is its fields in order, the most significant first, then the 12 BCH parity bits P.
// Forward words (FOCC, FVC) have 28 message bits, reverse words (RECC, RVC) 36. A word is made from
// the values of its fields (a field not given is 0) and read back into them; the parity is computed
// when made and checked when read.
namespace modemamps
{

struct WordField
{
    const char *name;
    int bits;
};

struct WordDef
{
    const char *name;
    bool reverse;                       // RECC/RVC: 36 message bits, else 28
    std::vector<WordField> fields;      // the message fields, without P

    int messageBits() const;            // the sum of the fields: 28 or 36 when the table is right
};

using WordValues = std::map<std::string, uint64_t>;

// The word of the values: the message bits and its parity (40 or 48 bits). A value wider than its
// field is cut to it.
MODEMAMPS_API uint64_t encodeWord(const WordDef& def, const WordValues& values);
// The values of a word's fields; false if its parity is wrong (the values are read anyway).
MODEMAMPS_API bool decodeWord(const WordDef& def, uint64_t word, WordValues& values);
// One field of a word, without checking the parity; 0 for a name the word has not.
MODEMAMPS_API uint64_t wordField(const WordDef& def, uint64_t word, const std::string& name);

} // namespace modemamps

#endif // MODEMAMPS_AMPSWORD_H_
