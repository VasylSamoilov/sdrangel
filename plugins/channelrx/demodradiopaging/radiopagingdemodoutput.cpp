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

#include <QDateTime>
#include <QVariantMap>

#include "radiopagingdemodoutput.h"

namespace RadioPagingDemodOutput {

bool isCompletion(const MessageRecord& record)
{
    return record.m_protocolData.contains("voice_end") && record.m_text.isEmpty();
}

QByteArray pagerDatagram(const MessageRecord& record)
{
    const QVariantMap& d = record.m_protocolData;
    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(record.m_timestamp);
    QString address = record.m_address;
    QString functionBits;

    // The address and function bits as the Pager demodulator gives them: 7 digits for POCSAG, 9 for FLEX
    if ((record.m_protocol == "pocsag") && d.contains("ric"))
    {
        address = QString("%1").arg(d.value("ric").toULongLong(), 7, 10, QChar('0'));
        functionBits = QString::number(d.value("function").toInt());
    }
    else if ((record.m_protocol == "flex") && d.contains("capcode"))
    {
        address = QString("%1").arg(d.value("capcode").toULongLong(), 9, 10, QChar('0'));
    }
    else if (d.contains("function"))
    {
        functionBits = QString::number(d.value("function").toInt());
    }

    // The text in its field, and the other interpretation (numeric or alphanumeric) in the other
    QString alpha, numeric;
    bool isNumeric = record.m_msgType.contains("numeric") && !record.m_msgType.contains("alpha");

    (isNumeric ? numeric : alpha) = record.m_text;

    QString altType = d.value("alt_type").toString();
    QString altText = d.value("alt_text").toString();

    if (!altText.isEmpty())
    {
        if (isNumeric && altType.contains("alpha")) {
            alpha = altText;
        } else if (!isNumeric && altType.contains("numeric") && !altType.contains("alpha")) {
            numeric = altText;
        }
    }

    QByteArray datagram;
    auto field = [&datagram](const QString& text) {
        datagram.append(text.toLatin1());
        datagram.append('\0');
    };

    field(dateTime.date().toString());
    field(dateTime.time().toString());
    field(address);
    field(functionBits);
    field(alpha);
    field(numeric);
    field(record.m_protocol.toUpper());
    field(record.m_msgType);

    return datagram;
}

} // namespace RadioPagingDemodOutput
