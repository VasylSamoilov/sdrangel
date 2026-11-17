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

// The national variants of 7-bit ASCII of the POCSAG pagers: ASCII with the national characters on some of the
// codes 0x23, 0x40, 0x5B-0x60 and 0x7B-0x7E. Each table is the ASCII table with its replaced codes; glyphs are
// UTF-8 escapes with their code point

#include <array>
#include <initializer_list>
#include <utility>

#include "radiopagingcharsettables.h"

namespace {

typedef std::array<RadioPagingCharset::Entry, 128> Entries;

Entries national(std::initializer_list<std::pair<int, const char*>> replaced)
{
    Entries entries;

    for (int code = 0; code < 128; code++)
    {
        for (int i = 0; i < RadioPagingCharset::m_maxGlyphs; i++) {
            entries[code][i] = RadioPagingCharsetTables::pocsagAscii.m_entries[code][i];
        }
    }

    for (const auto& r : replaced)
    {
        for (int i = 0; i < RadioPagingCharset::m_maxGlyphs; i++) {
            entries[r.first][i] = nullptr;
        }
        entries[r.first][0] = r.second;
    }

    return entries;
}

const Entries british = national({
    {0x23, "\xC2\xA3"},     // U+00A3 pound
    {0x7E, "-"},
});

const Entries danishNorwegian = national({
    {0x40, "\xC2\xA7"},     // U+00A7 section
    {0x5B, "\xC3\x86"},     // U+00C6 AE
    {0x5C, "\xC3\x98"},     // U+00D8 O stroke
    {0x5D, "\xC3\x85"},     // U+00C5 A ring
    {0x7B, "\xC5\x93"},     // U+0153 oe
    {0x7C, "\xC3\xB8"},     // U+00F8 o stroke
    {0x7D, "\xC3\xA5"},     // U+00E5 a ring
    {0x7E, "-"},
});

const Entries french = national({
    {0x23, "\xC2\xA3"},     // U+00A3 pound
    {0x40, "\xC3\xA0"},     // U+00E0 a grave
    {0x5B, "\xC2\xB0"},     // U+00B0 degree
    {0x5C, "\xC3\xA7"},     // U+00E7 c cedilla
    {0x5D, "\xC2\xA7"},     // U+00A7 section
    {0x60, "\xC3\xA9"},     // U+00E9 e acute
    {0x7B, "\xE2\x80\x99"}, // U+2019 right single quotation mark
    {0x7C, "\xC3\xB9"},     // U+00F9 u grave
    {0x7D, "\xC3\xA8"},     // U+00E8 e grave
    {0x7E, "-"},
});

const Entries german = national({
    {0x40, "\xC2\xA7"},     // U+00A7 section
    {0x5B, "\xC3\x84"},     // U+00C4 A umlaut
    {0x5C, "\xC3\x96"},     // U+00D6 O umlaut
    {0x5D, "\xC3\x9C"},     // U+00DC U umlaut
    {0x7B, "\xC3\xA4"},     // U+00E4 a umlaut
    {0x7C, "\xC3\xB6"},     // U+00F6 o umlaut
    {0x7D, "\xC3\xBC"},     // U+00FC u umlaut
    {0x7E, "\xC3\x9F"},     // U+00DF sharp s
});

const Entries italian = national({
    {0x23, "\xC2\xA3"},     // U+00A3 pound
    {0x40, "\xC2\xA7"},     // U+00A7 section
    {0x5B, "\xC2\xB0"},     // U+00B0 degree
    {0x5C, "\xC3\xA7"},     // U+00E7 c cedilla
    {0x5D, "\xC3\xA9"},     // U+00E9 e acute
    {0x60, "\xC3\xB9"},     // U+00F9 u grave
    {0x7B, "\xC3\xA0"},     // U+00E0 a grave
    {0x7C, "\xC3\xB2"},     // U+00F2 o grave
    {0x7D, "\xC3\xA8"},     // U+00E8 e grave
    {0x7E, "\xC3\x8C"},     // U+00CC I grave
});

const Entries portuguese = national({
    {0x23, "\xC2\xA3"},     // U+00A3 pound
    {0x7E, "-"},
});

const Entries spanish = national({
    {0x23, "\xC2\xA1"},     // U+00A1 inverted exclamation mark
    {0x5B, "\xC3\xA7"},     // U+00E7 c cedilla
    {0x5C, "\xC3\x91"},     // U+00D1 N tilde
    {0x5D, "\xC2\xBA"},     // U+00BA masculine ordinal
    {0x5E, "\xC2\xAA"},     // U+00AA feminine ordinal
    {0x60, "\xC2\xBF"},     // U+00BF inverted question mark
    {0x7B, "\xC3\xA7"},     // U+00E7 c cedilla
    {0x7C, "\xC3\xA5"},     // U+00E5 a ring
    {0x7D, "\xC3\x9C"},     // U+00DC U umlaut
    {0x7E, "\xC3\xBC"},     // U+00FC u umlaut
});

const Entries swedish = national({
    {0x23, "\xC2\xA3"},     // U+00A3 pound
    {0x40, "\xC3\x89"},     // U+00C9 E acute
    {0x5B, "\xC3\x84"},     // U+00C4 A umlaut
    {0x5C, "\xC3\x96"},     // U+00D6 O umlaut
    {0x5D, "\xC3\x85"},     // U+00C5 A ring
    {0x5E, "\xC3\x9C"},     // U+00DC U umlaut
    {0x60, "\xC3\xA9"},     // U+00E9 e acute
    {0x7B, "\xC3\xA4"},     // U+00E4 a umlaut
    {0x7C, "\xC3\xB6"},     // U+00F6 o umlaut
    {0x7D, "\xC3\xA5"},     // U+00E5 a ring
    {0x7E, "\xC3\xBC"},     // U+00FC u umlaut
});

const Entries swiss = national({
    {0x23, "\xC3\xB9"},     // U+00F9 u grave
    {0x40, "\xC3\xA0"},     // U+00E0 a grave
    {0x5B, "\xC3\xA9"},     // U+00E9 e acute
    {0x5C, "\xC3\xA7"},     // U+00E7 c cedilla
    {0x5D, "\xC3\xAA"},     // U+00EA e circumflex
    {0x5E, "\xC3\xAE"},     // U+00EE i circumflex
    {0x5F, "\xC3\xA8"},     // U+00E8 e grave
    {0x60, "\xC3\xB2"},     // U+00F2 o grave
    {0x7B, "\xC3\xA0"},     // U+00E0 a grave
    {0x7C, "\xC3\xB2"},     // U+00F2 o grave
    {0x7D, "\xC3\xA8"},     // U+00E8 e grave
    {0x7E, "\xC3\xBB"},     // U+00FB u circumflex
});

} // namespace

const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinBritish = {7, british.data()};
const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinDanishNorwegian = {7, danishNorwegian.data()};
const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinFrench = {7, french.data()};
const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinGerman = {7, german.data()};
const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinItalian = {7, italian.data()};
const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinPortuguese = {7, portuguese.data()};
const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinSpanish = {7, spanish.data()};
const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinSwedish = {7, swedish.data()};
const RadioPagingCharset::Table RadioPagingCharsetTables::pocsagLatinSwiss = {7, swiss.data()};
