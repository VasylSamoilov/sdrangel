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

#ifndef MODEMRADIOPAGING_RADIOPAGINGRECODE_H
#define MODEMRADIOPAGING_RADIOPAGINGRECODE_H

#include <QString>
#include <QStringList>

#include "export.h"
#include "util/messagerecord.h"
#include "radiopagingcharset.h"

// A received message decoded again from what its record keeps: the message bits of POCSAG and GSC (with another
// codepage, or as the other type, alphanumeric or numeric, when the content guess was wrong), the 7-bit characters
// of FLEX alphanumeric messages (alpha_codes, with another codepage; the type of FLEX is in the message).
class MODEMRADIOPAGING_API RadioPagingRecode
{
public:
    //!< The types a record can be decoded as (alphanumeric, numeric; FLEX alphanumeric only), none if it cannot be
    static QStringList types(const MessageRecord& record);
    //!< The record decoded with the codepage (none: the one of the record) as the type (empty: the one of the record).
    //!< The text, the type, the codepage and the other candidate; redecoded is set. False and an error if it cannot be
    static bool redecode(const MessageRecord& record, const RadioPagingCharset::Key& charset, const QString& type,
        MessageRecord& out, QString& error);
    //!< The text of a record: the codes below 0x20 and 0x7f as <0xNN>, other bytes kept (UTF-8 passes)
    static QString escapeText(const std::string& text);
};

#endif // MODEMRADIOPAGING_RADIOPAGINGRECODE_H
