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

#ifndef MODEMRADIOPAGING_RADIOPAGINGCHARSET_H
#define MODEMRADIOPAGING_RADIOPAGINGCHARSET_H

#include <cstdint>
#include <string>
#include <vector>

#include "export.h"

// Codepages of the alphanumeric characters of the pagers.
//
// The codes of POCSAG and FLEX are 7 bits (128), those of GSC 6 bits (64). A pager shows a code with the glyph of
// its codepage, which depends on the pager: the transmission does not tell the codepage. A codepage is named
// protocol-vendor-model-charset; several of them (pager models) may share one table.
//
// An entry of a table is the glyph of the code (UTF-8, null: no glyph, a control or unused code), then the other
// characters taken for that code when text is encoded (lowercase letters of an uppercase pager, the Latin letters
// that look like a Cyrillic one, ...). Decoding gives the glyph of each code, <0xNN> for a code without one;
// encoding takes the glyph or an alias of each character (a glyph of several characters, the Arabic lam-alef,
// first), and <0xNN> for any code.
class MODEMRADIOPAGING_API RadioPagingCharset
{
public:
    static const int m_maxGlyphs = 8;           //!< Glyph and aliases of an entry
    typedef const char *Entry[m_maxGlyphs];

    struct Table
    {
        int m_codeBits;                         //!< 7 (128 entries) or 6 (64)
        const Entry *m_entries;
    };

    struct Codepage
    {
        const char *m_protocol;                 //!< pocsag, flex or gsc
        const char *m_vendor;
        const char *m_model;
        const char *m_charset;
        const char *m_label;                    //!< For the user
        const Table *m_table;

        std::string name() const;               //!< protocol-vendor-model-charset
        int codes() const { return 1 << m_table->m_codeBits; }
        //!< The glyph of a code, nullptr if it has none
        const char *glyph(unsigned code) const;
        //!< The glyph and the aliases of a code
        std::vector<std::string> glyphs(unsigned code) const;
    };

    //!< Every codepage, the default of each protocol first
    static const std::vector<Codepage>& codepages();
    //!< The codepages of a protocol
    static std::vector<const Codepage*> codepages(const std::string& protocol);
    //!< A codepage by its name (not case sensitive; golay is taken for gsc), nullptr if none
    static const Codepage *byName(const std::string& name);
    //!< The default codepage of a protocol (generic ASCII)
    static const Codepage *defaultFor(const std::string& protocol);

    //!< The text of codes: glyphs, <0xNN> for the codes without one
    static std::string decode(const Codepage& codepage, const std::vector<uint8_t>& codes);
    //!< The codes of a text: a glyph or an alias for each character, or <0xNN> (a code of the codepage). False and
    //!< an error naming the first character that has no code
    static bool encode(const Codepage& codepage, const std::string& text, std::vector<uint8_t>& codes, std::string *error = nullptr);
    //!< The code of the character at text[pos] (advanced past it), -1 if it has none
    static int encodeOne(const Codepage& codepage, const std::string& text, size_t& pos);
};

#endif // MODEMRADIOPAGING_RADIOPAGINGCHARSET_H
