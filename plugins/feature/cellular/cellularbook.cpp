///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov                                             //
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

#include <QDataStream>
#include <QIODevice>
#include <QString>

#include "cellularbook.h"

namespace
{

const qint32 kVersion = 1;

void writeList(QDataStream& out, const std::vector<modemnmt::NmtPhoneInfo::Seen>& list)
{
    out << static_cast<qint32>(list.size());
    for (const modemnmt::NmtPhoneInfo::Seen& s : list) {
        out << QString::fromStdString(s.number) << static_cast<qint32>(s.y1) << static_cast<qint32>(s.area) << static_cast<qint32>(s.count);
    }
}

bool readList(QDataStream& in, std::vector<modemnmt::NmtPhoneInfo::Seen>& list)
{
    qint32 n = 0;
    in >> n;
    if (n < 0 || n > 100) {
        return false;
    }
    for (qint32 i = 0; i < n; i++)
    {
        QString number;
        qint32 y1 = 0, area = 0, count = 0;
        in >> number >> y1 >> area >> count;
        list.push_back({number.toStdString(), y1, area, count});
    }
    return in.status() == QDataStream::Ok;
}

} // namespace

namespace CellularBook
{

QByteArray toBytes(const modemnmt::NmtPhoneBook& book)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    const std::vector<const modemnmt::NmtPhoneRecord*> records = book.all();

    out.setVersion(QDataStream::Qt_5_0);
    out << kVersion << static_cast<qint32>(records.size());
    for (const modemnmt::NmtPhoneRecord *r : records)
    {
        out << static_cast<qint32>(r->system) << QString::fromStdString(r->number);
        out << static_cast<qint32>(modemnmt::NmtPhoneRecord::FieldCount);
        for (const modemnmt::NmtBookField& f : r->fields) {
            out << QString::fromStdString(f.value) << f.typed;
        }
        writeList(out, r->dialled);
        writeList(out, r->smsCentresUsed);
        out << static_cast<qint32>(r->areas.size());
        for (const std::pair<int, int>& a : r->areas) {
            out << static_cast<qint32>(a.first) << static_cast<qint32>(a.second);
        }
    }
    return bytes;
}

bool fromBytes(const QByteArray& bytes, modemnmt::NmtPhoneBook& book)
{
    QDataStream in(bytes);
    qint32 version = 0, count = 0;

    in.setVersion(QDataStream::Qt_5_0);
    in >> version >> count;
    if (version != kVersion || count < 0 || count > 100000) {
        return false;
    }
    for (qint32 i = 0; i < count; i++)
    {
        modemnmt::NmtPhoneRecord r;
        qint32 system = 0, fields = 0, areas = 0;
        QString number;

        in >> system >> number >> fields;
        if (fields < 0 || fields > 64) {
            return false;
        }
        r.system = system == static_cast<qint32>(modemnmt::System::Nmt900) ? modemnmt::System::Nmt900 : modemnmt::System::Nmt450;
        r.number = number.toStdString();
        for (qint32 f = 0; f < fields; f++)
        {
            QString value;
            bool typed = false;
            in >> value >> typed;
            if (f < modemnmt::NmtPhoneRecord::FieldCount) {     // fields of a newer version are skipped
                r.fields[f] = {value.toStdString(), typed};
            }
        }
        if (!readList(in, r.dialled) || !readList(in, r.smsCentresUsed)) {
            return false;
        }
        in >> areas;
        if (areas < 0 || areas > 100) {
            return false;
        }
        for (qint32 a = 0; a < areas; a++)
        {
            qint32 y1 = 0, area = 0;
            in >> y1 >> area;
            r.areas.emplace_back(y1, area);
        }
        if (in.status() != QDataStream::Ok || r.number.empty()) {
            return false;
        }
        book.merge(r);
    }
    return true;
}

} // namespace CellularBook
