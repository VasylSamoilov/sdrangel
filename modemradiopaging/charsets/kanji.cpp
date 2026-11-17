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

// FLEX Kanji: the alphanumeric message words carry a 16-bit character each (bits 0-15), a Shift-JIS code. The
// transmission does not tell it: the pager is a Kanji one. The decoder gives the codes as byte pairs, high byte
// first; this gives their text

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#else
#include <QTextCodec>
#endif

#include "radiopagingcharsettables.h"

namespace {

bool encode(const std::string&, std::string&, std::string& error, std::string&)
{
    error = "Kanji (16-bit Shift-JIS characters) is decoded only, not sent";
    return false;
}

std::string decode(const std::string& bytes)
{
    const QByteArray data(bytes.data(), (int) bytes.size());
    QString text;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QStringDecoder decoder("Shift_JIS");

    if (!decoder.isValid()) {
        return bytes;
    }

    text = decoder.decode(data);
#else
    QTextCodec *codec = QTextCodec::codecForName("Shift_JIS");

    if (!codec) {
        return bytes;
    }

    text = codec->toUnicode(data);
#endif

    return text.toStdString();
}

} // namespace

const RadioPagingCharset::Transcoder RadioPagingCharsetTables::kanji = {
    "Kanji: 16-bit Shift-JIS characters, a message word each (decoded only)",
    encode,
    decode,
    true
};
