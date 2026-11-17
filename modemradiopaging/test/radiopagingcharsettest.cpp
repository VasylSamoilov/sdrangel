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
#include <set>
#include <string>
#include <vector>

#include "radiopagingcharset.h"

namespace {

int passes = 0;
int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        failures++;
    }
}

typedef RadioPagingCharset Cs;

std::vector<uint8_t> encode(const char *name, const std::string& text, bool *ok = nullptr)
{
    std::vector<uint8_t> codes;
    bool done = Cs::encode(*Cs::byName(name), text, codes);
    if (ok) {
        *ok = done;
    }
    return codes;
}

} // namespace

int main()
{
    // The codepages: 26 POCSAG, 14 FLEX, 1 GSC, names unique, each found by its name, generic ASCII the default
    std::set<std::string> names;
    bool found = true;

    for (const Cs::Codepage& c : Cs::codepages())
    {
        names.insert(c.name());
        found = found && (Cs::byName(c.name()) == &c) && c.m_table && c.m_label;
    }

    check((Cs::codepages("pocsag").size() == 26) && (Cs::codepages("flex").size() == 14) && (Cs::codepages("gsc").size() == 1)
        && (names.size() == Cs::codepages().size()) && found, "41 codepages, names unique, each found by its name");
    check((Cs::defaultFor("pocsag")->name() == "pocsag-generic-generic-ascii") && (Cs::defaultFor("flex")->name() == "flex-generic-generic-ascii")
        && (Cs::defaultFor("gsc")->name() == "gsc-generic-generic-ascii") && (Cs::defaultFor("ermes") == nullptr),
        "default codepage of each protocol: generic ASCII");
    check((Cs::byName("GOLAY-generic-generic-ascii") == Cs::defaultFor("gsc")) && (Cs::codepages("golay").size() == 1)
        && (Cs::byName("pocsag-none") == nullptr), "names not case sensitive, golay is gsc, an unknown name is none");
    check((Cs::byName("pocsag-partner-generic-cyrillic") != nullptr) && (Cs::byName("pocsag-multitone-generic-ascii") != nullptr)
        && (Cs::byName("pocsag-nec-maxima-compact-cyrillic") != nullptr), "the codepages missing from the osmocom registry are there");

    // Decode: glyphs, <0xNN> for the codes without one; the code masked to the codepage
    const Cs::Codepage& ascii = *Cs::defaultFor("pocsag");
    check(Cs::decode(ascii, {0x48, 0x49, 0x00, 0x03, 0x7F, 0x0A}) == "HI<0x00><0x03><0x7F>\n", "ASCII decode: glyphs, <0xNN> for the controls and DEL");
    const Cs::Codepage& german = *Cs::byName("pocsag-generic-generic-latin-german");
    check(Cs::decode(german, {0x5B, 0x7E}) == "\xC3\x84\xC3\x9F", "German decode: 0x5B A umlaut, 0x7E sharp s");
    const Cs::Codepage& gsc = *Cs::defaultFor("gsc");
    check((gsc.codes() == 64) && (Cs::decode(gsc, {0x21, 0x3C, 0x3E}) == "A\r<0x3E>"), "GSC: 64 codes, A, CR, the fill without a glyph");

    // Encode: the glyph or an alias, <0xNN> for any code, the first character without a code refused
    bool ok;
    check(encode("pocsag-generic-generic-ascii-uppercase", "ab") == std::vector<uint8_t>({0x41, 0x42}),
        "uppercase pager: lowercase encoded on the uppercase codes");
    check(encode("pocsag-motorola-advisor-linguist-latin-cyrillic", "\xD0\x9C\xD0\xB8\xD1\x80") == std::vector<uint8_t>({0x4D, 0x68, 0x50}),
        "Linguist: Cyrillic M on the Latin M, i on its own code, r on the Latin P");
    check(encode("pocsag-generic-generic-ascii", "A<0x04>B<0x7F>") == std::vector<uint8_t>({0x41, 0x04, 0x42, 0x7F}),
        "<0xNN> encodes any code");
    std::vector<uint8_t> refused = encode("pocsag-generic-generic-ascii", "A\xC3\x84", &ok);
    std::string error;
    std::vector<uint8_t> codes;
    Cs::encode(ascii, "A\xC3\x84", codes, &error);
    check(!ok && (error.find("\xC3\x84") != std::string::npos), "a character without a code is refused, named in the error");
    check(encode("gsc-generic-generic-ascii", "hi\n") == std::vector<uint8_t>({0x28, 0x29, 0x3C}), "GSC: lowercase taken as uppercase, LF as CR");
    check(encode("pocsag-nec-maxima-compact-cyrillic", "\xD0\x90\xD0\x91\xD0\x95\xD0\x81") == std::vector<uint8_t>({0x41, 0x61, 0x45, 0x65}),
        "NEC Maxima/Compact (the 21A layout): A and E on the Latin codes, Be on 0x61, Io on 0x65");
    check(encode("flex-motorola-linguist-arabic", "\xD9\x84\xD8\xA7\xD9\x84") == std::vector<uint8_t>({0x5B, 0x64}),
        "Arabic: lam-alef on its code 0x5B, a lam alone on 0x64");

    // Round trip of every code with a glyph of one character, for every codepage: the glyph encodes to a code that
    // has it as its glyph or an alias (a character on two codes takes the first: the backtick of an uppercase pager
    // is sent as its apostrophe)
    bool roundTrip = true;

    for (const Cs::Codepage& c : Cs::codepages())
    {
        for (int code = 0; code < c.codes(); code++)
        {
            const char *glyph = c.glyph(code);

            if (!glyph || (std::string(glyph).size() > 4) || (glyph[0] == 0)) {
                continue;
            }

            size_t pos = 0;
            int back = Cs::encodeOne(c, glyph, pos);
            bool listed = false;

            for (const std::string& g : (back >= 0) ? c.glyphs(back) : std::vector<std::string>()) {
                listed = listed || (g == glyph);
            }

            roundTrip = roundTrip && listed;
        }
    }

    check(roundTrip, "every glyph of every codepage encodes to a code that has it");

    std::printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
