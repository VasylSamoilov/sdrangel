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

#ifndef INCLUDE_FEATURE_CELLULARSMSTEXT_H_
#define INCLUDE_FEATURE_CELLULARSMSTEXT_H_

#include <cstdint>
#include <string>
#include <vector>

#include <QString>

#include "radiopagingcharset.h"

// The text of NMT short messages through the paging codepages of protocol "nmt" (Annex 24 4.2, 4.3):
// TP-DCS 0x00 the GSM default alphabet, 0x30 + n NMT Character Set n.
namespace CellularSmsText
{
    // The codepage of a key, the default (GSM) for an empty or unknown key
    const RadioPagingCharset::Codepage& codepage(const RadioPagingCharset::Key& key);
    // The codepage of a received TP-DCS: 0x30..0x34 the set, else the GSM default
    const RadioPagingCharset::Codepage& forDcs(uint8_t dcs);
    // The NMT Character Set (0..4) of a codepage, for the DCS of its 8-bit messages
    int set(const RadioPagingCharset::Codepage& codepage);
    // The codes of a UTF-8 text in a codepage as the SMS layer takes them (each <0xNN>); a character the
    // codepage has not, but the GSM extension table has, as its escape. False and the character if one has no
    // code.
    bool encode(const RadioPagingCharset::Codepage& codepage, const QString& text, std::string& codes, QString& bad);
    // The text of received codes with the codepage of their DCS; the GSM escape and its code as the character,
    // a code without a glyph as <0xNN>
    QString decode(uint8_t dcs, const std::vector<uint8_t>& codes);
    // The text of codes with a codepage (recoding: another codepage than the DCS says)
    QString decode(const RadioPagingCharset::Codepage& codepage, const std::vector<uint8_t>& codes);
    // The codes of a text of escapes <0xNN> as encode() makes it
    std::vector<uint8_t> codesOf(const std::string& escaped);
    // "vendor/model/codepage" of a key, and back (an empty text: the default)
    QString keyText(const RadioPagingCharset::Key& key);
    RadioPagingCharset::Key keyOf(const QString& text);
}

#endif // INCLUDE_FEATURE_CELLULARSMSTEXT_H_
