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

#include "cellularsmstext.h"

#include <cstdio>

#include <QStringList>

#include "nmtsms.h"

namespace CellularSmsText
{

const RadioPagingCharset::Codepage& codepage(const RadioPagingCharset::Key& key)
{
    const RadioPagingCharset::Codepage *c = key.empty() ? nullptr : RadioPagingCharset::find("nmt", key);
    return c ? *c : *RadioPagingCharset::defaultFor("nmt");
}

const RadioPagingCharset::Codepage& forDcs(uint8_t dcs)
{
    static const char *const kSets[] = {"cyrillic", "baltic", "central", "western", "thai"};

    if ((dcs & 0xf0) == 0x30 && (dcs & 0x0f) < 5) {
        return codepage(RadioPagingCharset::Key{"generic", "generic", kSets[dcs & 0x0f]});
    }
    return *RadioPagingCharset::defaultFor("nmt");
}

int set(const RadioPagingCharset::Codepage& codepage)
{
    static const char *const kSets[] = {"cyrillic", "baltic", "central", "western", "thai"};

    for (int i = 0; i < 5; i++)
    {
        if (std::string(codepage.m_codepage) == kSets[i]) {
            return i;
        }
    }
    return 0;
}

bool encode(const RadioPagingCharset::Codepage& codepage, const QString& text, std::string& codes, QString& bad)
{
    const std::string utf8 = text.toStdString();
    size_t pos = 0;
    char hex[8];

    codes.clear();
    while (pos < utf8.size())
    {
        const size_t start = pos;
        const int code = RadioPagingCharset::encodeOne(codepage, utf8, pos);
        std::vector<uint8_t> gsm;

        if (code >= 0) {
            gsm.push_back(static_cast<uint8_t>(code));
        }
        else
        {
            // the character: a UTF-8 sequence; the GSM extension table (escape and code) has some ASCII
            pos = start + 1;
            while (pos < utf8.size() && (static_cast<uint8_t>(utf8[pos]) & 0xc0) == 0x80) {
                pos++;
            }
            const std::string character = utf8.substr(start, pos - start);
            gsm = modemnmt::smsGsmEncode(character);
            if (gsm.size() != 2 || gsm[0] != 0x1b)
            {
                bad = QString::fromStdString(character);
                return false;
            }
        }
        for (uint8_t c : gsm)
        {
            std::snprintf(hex, sizeof(hex), "<0x%02X>", c);
            codes += hex;
        }
    }
    return true;
}

QString decode(const RadioPagingCharset::Codepage& codepage, const std::vector<uint8_t>& codes)
{
    QString text;

    for (size_t i = 0; i < codes.size(); i++)
    {
        const uint8_t c = codes[i];
        const char *glyph = (c < codepage.codes()) ? codepage.glyph(c) : nullptr;

        if (c == 0x1b && i + 1 < codes.size())
        {
            const std::string extension = modemnmt::smsGsmDecode(std::vector<uint8_t>{c, codes[i + 1]});
            if (!extension.empty() && extension != "?")
            {
                text += QString::fromStdString(extension);
                i++;
                continue;
            }
        }
        if (glyph) {
            text += QString::fromUtf8(glyph);
        } else {
            text += QString::asprintf("<0x%02X>", c);
        }
    }
    return text;
}

QString decode(uint8_t dcs, const std::vector<uint8_t>& codes)
{
    return decode(forDcs(dcs), codes);
}

std::vector<uint8_t> codesOf(const std::string& escaped)
{
    std::vector<uint8_t> codes;

    for (size_t i = 0; i + 6 <= escaped.size(); i += 6)
    {
        if (escaped.compare(i, 3, "<0x") != 0 || escaped[i + 5] != '>') {
            break;
        }
        codes.push_back(static_cast<uint8_t>(std::stoi(escaped.substr(i + 3, 2), nullptr, 16)));
    }
    return codes;
}

QString keyText(const RadioPagingCharset::Key& key)
{
    return key.empty() ? QString() : QString("%1/%2/%3").arg(QString::fromStdString(key.m_vendor))
        .arg(QString::fromStdString(key.m_model)).arg(QString::fromStdString(key.m_codepage));
}

RadioPagingCharset::Key keyOf(const QString& text)
{
    const QStringList parts = text.split('/');
    if (parts.size() != 3) {
        return RadioPagingCharset::Key();
    }
    return RadioPagingCharset::Key{parts[0].toStdString(), parts[1].toStdString(), parts[2].toStdString()};
}

} // namespace CellularSmsText
