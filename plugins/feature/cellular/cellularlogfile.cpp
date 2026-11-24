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

#include "util/csv.h"

#include "cellularlogfile.h"

bool CellularLogFile::open(const QString& filename)
{
    close();
    m_filename = filename;
    if (filename.isEmpty()) {
        return false;
    }
    m_file.setFileName(filename);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    m_stream.setDevice(&m_file);
    if (m_file.size() == 0)
    {
        m_stream << "Date,Time,Network,Kind,Text\n";
        m_stream.flush();
    }
    return true;
}

void CellularLogFile::close()
{
    if (m_file.isOpen())
    {
        m_stream.flush();
        m_stream.setDevice(nullptr);
        m_file.close();
    }
}

void CellularLogFile::write(const QDateTime& time, const QString& network, const QString& kind, const QString& text)
{
    if (!m_file.isOpen()) {
        return;
    }
    // the texts may carry commas, quotes and line breaks (what a phone sent)
    m_stream << time.date().toString(Qt::ISODate) << ","
        << time.time().toString("HH:mm:ss.zzz") << ","
        << CSV::escape(network) << ","
        << kind << ","
        << CSV::escape(text) << "\n";
    m_stream.flush();
}
