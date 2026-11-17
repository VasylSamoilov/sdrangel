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

#include "radiopagingpage.h"
#include "radiopagingpocsag.h"
#include "radiopaginggsc.h"
#include "radiopagingkanji.h"
#include "radiopagingrecode.h"

namespace {

bool characters(const MessageRecord& record)
{
    return (record.m_msgType == "alphanumeric") || (record.m_msgType == "numeric");
}

// POCSAG and GSC: the interpretation, the text and the other candidate as the type asked for
template <typename Interpretation>
void setInterpretation(const Interpretation& interpretation, const QString& type, MessageRecord& out)
{
    const bool first = (type.toStdString() == interpretation.m_type);

    out.m_msgType = type;
    out.m_text = RadioPagingRecode::escapeText(first ? interpretation.m_text : interpretation.m_altText);
    out.m_protocolData.insert("score", first ? interpretation.m_score : interpretation.m_altScore);
    out.m_protocolData.insert("alt_type", QString::fromStdString(first ? interpretation.m_altType : interpretation.m_type));
    out.m_protocolData.insert("alt_text", RadioPagingRecode::escapeText(first ? interpretation.m_altText : interpretation.m_text));
    out.m_protocolData.insert("alt_score", first ? interpretation.m_altScore : interpretation.m_score);
}

} // namespace

QStringList RadioPagingRecode::types(const MessageRecord& record)
{
    if (record.m_direction != MessageRecord::DirectionRX) {
        return QStringList();
    }
    if (((record.m_protocol == "pocsag") || (record.m_protocol == "gsc")) && characters(record) && (record.m_rawBits > 0)) {
        return {"alphanumeric", "numeric"};
    }
    if ((record.m_protocol == "flex") && record.m_protocolData.contains("alpha_codes")) {
        return {"alphanumeric"};
    }
    if ((record.m_protocol == "flex") && (record.m_msgType == "binary") && record.m_protocolData.contains("hex")) {
        return {"binary"};
    }

    return QStringList();
}

bool RadioPagingRecode::redecode(const MessageRecord& record, const RadioPagingCharset::Key& charset, const QString& type,
    MessageRecord& out, QString& error)
{
    const QStringList can = types(record);
    const QString as = type.isEmpty() ? record.m_msgType : type;
    const std::string protocol = record.m_protocol.toStdString();
    const RadioPagingCharset::Key key = charset.empty() ? RadioPagingPage::charsetKey(record.m_protocolData.value("charset")) : charset;
    const RadioPagingCharset::Codepage *codepage = RadioPagingCharset::find(protocol, key);

    if (!can.contains(as))
    {
        error = can.isEmpty() ? QString("The record keeps nothing to decode again")
            : QString("A %1 record decodes as %2").arg(record.m_protocol).arg(can.join(" or "));
        return false;
    }
    if (!codepage)
    {
        if (!charset.empty())
        {
            error = RadioPagingPage::checkCharset(record.m_protocol, charset);
            return false;
        }

        codepage = RadioPagingCharset::defaultFor(protocol);
    }

    out = record;
    const std::vector<uint8_t> raw(record.m_rawData.begin(), record.m_rawData.end());

    if (record.m_protocol == "pocsag")
    {
        std::vector<uint8_t> fill;

        for (const QVariant& code : record.m_protocolData.value("fill_codes").toList()) {
            fill.push_back((uint8_t) code.toUInt());
        }

        setInterpretation(RadioPagingPocsag::interpret(raw, record.m_rawBits, codepage,
            record.m_protocolData.contains("fill_codes") ? fill : RadioPagingPocsag::fillMinimal()), as, out);
    }
    else if (record.m_protocol == "gsc")
    {
        setInterpretation(RadioPagingGsc::interpret(raw, record.m_rawBits, codepage), as, out);
    }
    else if (as == "binary")
    {
        // HEX/binary of 8 or 16-bit characters: Shift JIS text with the Kanji codepage (STD-43A 1.3.1), the hex
        // digits with any other
        const QString hex = record.m_protocolData.value("hex").toString();

        if (!(codepage->m_transcoder && codepage->m_transcoder->m_binary))
        {
            out.m_text = hex;
            out.m_protocolData.remove("charset");
            out.m_protocolData.insert("redecoded", true);
            return true;
        }

        out.m_text = escapeText(RadioPagingKanji::decode(hex.toStdString(), record.m_protocolData.value("blocking", 16).toInt(),
            record.m_protocolData.value("display_direction").toString() == "rtl"));
    }
    else
    {
        // The 7-bit characters of an alphanumeric message: not the Kanji codepage (HEX/binary messages)
        if (codepage->m_transcoder && codepage->m_transcoder->m_binary)
        {
            error = QString("Kanji is the codepage of HEX/binary messages");
            return false;
        }

        const QByteArray codes = QByteArray::fromHex(record.m_protocolData.value("alpha_codes").toString().toLatin1());
        out.m_text = escapeText(RadioPagingCharset::decode(*codepage, std::vector<uint8_t>(codes.begin(), codes.end())));
    }

    out.m_protocolData.insert("charset", RadioPagingPage::charsetData(codepage->key()));
    out.m_protocolData.insert("redecoded", true);
    return true;
}

QString RadioPagingRecode::escapeText(const std::string& text)
{
    std::string out;

    for (char c : text)
    {
        unsigned char u = (unsigned char) c;

        if ((u < 0x20) || (u == 0x7f))
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "<0x%02X>", u);
            out += buf;
        }
        else
        {
            out.push_back(c);
        }
    }

    return QString::fromUtf8(out.c_str(), (int) out.size());
}
