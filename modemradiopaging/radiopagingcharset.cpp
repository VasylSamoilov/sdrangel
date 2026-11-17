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

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "charsets/radiopagingcharsettables.h"
#include "radiopagingcharset.h"

using namespace RadioPagingCharsetTables;

const int RadioPagingCharset::m_maxGlyphs;

namespace {

std::string lower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return out;
}

int utf8Length(unsigned char c)
{
    if ((c & 0x80) == 0x00) {
        return 1;
    }
    if ((c & 0xe0) == 0xc0) {
        return 2;
    }
    if ((c & 0xf0) == 0xe0) {
        return 3;
    }
    if ((c & 0xf8) == 0xf0) {
        return 4;
    }
    return 1;
}

int hexValue(char c)
{
    return ((c >= '0') && (c <= '9')) ? c - '0' : ((c >= 'a') && (c <= 'f')) ? c - 'a' + 10
        : ((c >= 'A') && (c <= 'F')) ? c - 'A' + 10 : -1;
}

} // namespace

const char *RadioPagingCharset::Codepage::glyph(unsigned code) const
{
    return m_table->m_entries[code & (unsigned) (codes() - 1)][0];
}

std::vector<std::string> RadioPagingCharset::Codepage::glyphs(unsigned code) const
{
    std::vector<std::string> list;
    const Entry& entry = m_table->m_entries[code & (unsigned) (codes() - 1)];

    for (int i = 0; (i < m_maxGlyphs) && entry[i]; i++) {
        list.push_back(entry[i]);
    }

    return list;
}

const std::vector<RadioPagingCharset::Codepage>& RadioPagingCharset::codepages()
{
    static const std::vector<Codepage> list = {
        // POCSAG
        {"pocsag", "generic", "generic", "ascii", "Generic - ASCII", &pocsagAscii},
        {"pocsag", "motorola", "advisor", "ascii", "Motorola Advisor - ASCII", &pocsagAscii},
        {"pocsag", "generic", "generic", "ascii-uppercase", "Generic - ASCII (uppercase)", &pocsagAsciiUppercase},
        {"pocsag", "motorola", "bravo-alpha", "ascii-uppercase", "Motorola Bravo Alpha - ASCII (uppercase)", &pocsagAsciiUppercase},
        {"pocsag", "generic", "generic", "latin-british", "Generic - British", &pocsagLatinBritish},
        {"pocsag", "generic", "generic", "latin-danish-norwegian", "Generic - Danish/Norwegian", &pocsagLatinDanishNorwegian},
        {"pocsag", "generic", "generic", "latin-french", "Generic - French", &pocsagLatinFrench},
        {"pocsag", "generic", "generic", "latin-german", "Generic - German", &pocsagLatinGerman},
        {"pocsag", "generic", "generic", "latin-italian", "Generic - Italian", &pocsagLatinItalian},
        {"pocsag", "generic", "generic", "latin-portuguese", "Generic - Portuguese", &pocsagLatinPortuguese},
        {"pocsag", "generic", "generic", "latin-spanish", "Generic - Spanish", &pocsagLatinSpanish},
        {"pocsag", "generic", "generic", "latin-swedish", "Generic - Swedish", &pocsagLatinSwedish},
        {"pocsag", "generic", "generic", "latin-swiss", "Generic - Swiss", &pocsagLatinSwiss},
        {"pocsag", "motorola", "advisor", "cyrillic", "Motorola Advisor - Cyrillic", &pocsagMotorolaAdvisorCyrillic},
        {"pocsag", "motorola", "advisor-linguist", "latin-cyrillic", "Motorola Advisor Linguist - Latin/Cyrillic (uppercase)",
            &pocsagMotorolaAdvisorLinguistLatinCyrillic},
        {"pocsag", "motorola", "scriptor-lx2-linguist", "latin-cyrillic", "Motorola Scriptor LX2 Linguist - Latin/Cyrillic (uppercase)",
            &pocsagMotorolaAdvisorLinguistLatinCyrillic},
        {"pocsag", "nec", "maxima", "latin-cyrillic", "NEC Maxima - Latin/Cyrillic (uppercase)", &pocsagMotorolaAdvisorLinguistLatinCyrillic},
        {"pocsag", "nec", "optima", "latin-cyrillic", "NEC Optima - Latin/Cyrillic (uppercase)", &pocsagMotorolaAdvisorLinguistLatinCyrillic},
        {"pocsag", "truly", "supervisor", "latin-cyrillic", "Truly Supervisor - Latin/Cyrillic (uppercase)",
            &pocsagMotorolaAdvisorLinguistLatinCyrillic},
        {"pocsag", "multitone", "generic", "ascii", "Multitone - ASCII (uppercase)", &pocsagMultitoneAscii},
        {"pocsag", "nec", "21a", "latin-cyrillic", "NEC 21A - Latin/Cyrillic (uppercase)", &pocsagNec21aLatinCyrillic},
        {"pocsag", "nec", "maxima-compact", "cyrillic", "NEC Maxima/Compact - Cyrillic", &pocsagNecMaximaCompactCyrillic},
        {"pocsag", "philips", "prg2220", "latin-cyrillic", "Philips PRG2220 - Latin/Cyrillic", &pocsagPhilipsPrg2220LatinCyrillic},
        {"pocsag", "swissphone", "de506", "ascii", "Swissphone DE506 - ASCII", &pocsagAscii},
        {"pocsag", "swissphone", "de506", "cyrillic", "Swissphone DE506 - Cyrillic", &pocsagSwissphoneDe506Cyrillic},
        {"pocsag", "partner", "generic", "cyrillic", "Partner - Cyrillic", &pocsagSwissphoneDe506Cyrillic},
        {"pocsag", "generic", "generic", "ascii-rot13", "Generic - ASCII, ROT-13", &pocsagAscii, &rot13},
        // FLEX
        {"flex", "generic", "generic", "ascii", "Generic - ASCII", &pocsagAscii},
        {"flex", "motorola", "linguist", "cyrillic", "Motorola Linguist - Cyrillic", &flexLinguistCyrillic},
        {"flex", "motorola", "linguist", "icelandic", "Motorola Linguist - Icelandic", &flexLinguistIcelandic},
        {"flex", "motorola", "linguist", "polish", "Motorola Linguist - Polish", &flexLinguistPolish},
        {"flex", "motorola", "linguist", "vietnamese", "Motorola Linguist - Vietnamese", &flexLinguistVietnamese},
        {"flex", "motorola", "linguist", "arabic", "Motorola Linguist - Arabic", &flexLinguistArabic},
        {"flex", "motorola", "linguist", "arabic-english", "Motorola Linguist - Arabic/English", &flexLinguistArabicEnglish},
        {"flex", "motorola", "linguist", "turkish", "Motorola Linguist - Turkish", &flexLinguistTurkish},
        {"flex", "motorola", "linguist", "hebrew", "Motorola Linguist - Hebrew", &flexLinguistHebrew},
        {"flex", "motorola", "linguist", "greek", "Motorola Linguist - Greek", &flexLinguistGreek},
        {"flex", "motorola", "linguist", "latin1", "Motorola Linguist - Latin-1 (ISO 8859-1)", &flexLinguistLatin1},
        {"flex", "motorola", "linguist", "latin2", "Motorola Linguist - Latin-2 (ISO 8859-2)", &flexLinguistLatin2},
        {"flex", "motorola", "linguist", "latin3", "Motorola Linguist - Latin-3 (ISO 8859-3)", &flexLinguistLatin3},
        {"flex", "motorola", "linguist", "latin4", "Motorola Linguist - Latin-4 (ISO 8859-4)", &flexLinguistLatin4},
        {"flex", "timex", "beepwear-pro", "alarm", "Timex BeepwearPRO - Alarm command (key=value)", &pocsagAscii, &beepwearProAlarm},
        {"flex", "generic", "generic", "ascii-rot13", "Generic - ASCII, ROT-13", &pocsagAscii, &rot13},
        {"flex", "generic", "generic", "kanji", "Generic - Kanji (Shift-JIS, 16-bit characters)", &pocsagAscii, &kanji},
        // GSC
        {"gsc", "generic", "generic", "ascii", "Generic - ASCII (Table VII)", &gscAscii},
    };

    return list;
}

std::vector<const RadioPagingCharset::Codepage*> RadioPagingCharset::codepages(const std::string& protocol)
{
    std::vector<const Codepage*> list;
    const std::string p = (lower(protocol) == "golay") ? std::string("gsc") : lower(protocol);

    for (const Codepage& codepage : codepages())
    {
        if (p == codepage.m_protocol) {
            list.push_back(&codepage);
        }
    }

    return list;
}

const RadioPagingCharset::Codepage *RadioPagingCharset::find(const std::string& protocol, const Key& key)
{
    const std::string vendor = lower(key.m_vendor);
    const std::string model = lower(key.m_model);
    const std::string page = lower(key.m_codepage);

    for (const Codepage *codepage : codepages(protocol))
    {
        if ((vendor == codepage->m_vendor) && (model == codepage->m_model) && (page == codepage->m_codepage)) {
            return codepage;
        }
    }

    return nullptr;
}

const RadioPagingCharset::Codepage *RadioPagingCharset::defaultFor(const std::string& protocol)
{
    std::vector<const Codepage*> list = codepages(protocol);
    return list.empty() ? nullptr : list.front();
}

std::string RadioPagingCharset::decode(const Codepage& codepage, const std::vector<uint8_t>& codes)
{
    std::string text;

    if (codepage.m_transcoder && codepage.m_transcoder->m_bytes) {
        return codepage.m_transcoder->m_decode(std::string(codes.begin(), codes.end()));
    }

    for (uint8_t code : codes)
    {
        const char *glyph = codepage.glyph(code);

        if (glyph)
        {
            text += glyph;
        }
        else
        {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "<0x%02X>", code & (codepage.codes() - 1));
            text += hex;
        }
    }

    return codepage.m_transcoder ? codepage.m_transcoder->m_decode(text) : text;
}

int RadioPagingCharset::encodeOne(const Codepage& codepage, const std::string& text, size_t& pos)
{
    // A code given as <0xNN>
    if ((pos + 6 <= text.size()) && (text[pos] == '<') && (text[pos + 1] == '0') && ((text[pos + 2] == 'x') || (text[pos + 2] == 'X'))
        && (hexValue(text[pos + 3]) >= 0) && (hexValue(text[pos + 4]) >= 0) && (text[pos + 5] == '>'))
    {
        int code = hexValue(text[pos + 3]) * 16 + hexValue(text[pos + 4]);

        if (code < codepage.codes())
        {
            pos += 6;
            return code;
        }
    }

    size_t length = std::min((size_t) utf8Length((unsigned char) text[pos]), text.size() - pos);

    // A glyph of several characters (Arabic lam-alef) first, the longest one
    int best = -1;
    size_t bestLength = length;

    for (int code = 0; code < codepage.codes(); code++)
    {
        const Entry& entry = codepage.m_table->m_entries[code];

        for (int i = 0; (i < m_maxGlyphs) && entry[i]; i++)
        {
            size_t n = std::strlen(entry[i]);

            if ((n > bestLength) && (text.compare(pos, n, entry[i]) == 0))
            {
                best = code;
                bestLength = n;
            }
        }
    }

    if (best >= 0)
    {
        pos += bestLength;
        return best;
    }

    for (int code = 0; code < codepage.codes(); code++)
    {
        const Entry& entry = codepage.m_table->m_entries[code];

        for (int i = 0; (i < m_maxGlyphs) && entry[i]; i++)
        {
            if ((std::strlen(entry[i]) == length) && (text.compare(pos, length, entry[i]) == 0))
            {
                pos += length;
                return code;
            }
        }
    }

    pos += length;
    return -1;
}

bool RadioPagingCharset::encode(const Codepage& codepage, const std::string& userText, std::vector<uint8_t>& codes, std::string *error,
    std::string *warning)
{
    std::string text = userText;

    if (codepage.m_transcoder)
    {
        std::string transcoderError, transcoderWarning;

        if (!codepage.m_transcoder->m_encode(userText, text, transcoderError, transcoderWarning))
        {
            if (error) {
                *error = transcoderError;
            }
            return false;
        }
        if (warning) {
            *warning = transcoderWarning;
        }
    }

    size_t pos = 0;

    while (pos < text.size())
    {
        size_t start = pos;
        int code = encodeOne(codepage, text, pos);

        if (code < 0)
        {
            if (error) {
                *error = "The character '" + text.substr(start, pos - start) + "' has no code in the codepage " + codepage.m_label;
            }
            return false;
        }

        codes.push_back((uint8_t) code);
    }

    return true;
}
