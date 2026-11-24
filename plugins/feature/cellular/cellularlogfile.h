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

#ifndef INCLUDE_FEATURE_CELLULARLOGFILE_H_
#define INCLUDE_FEATURE_CELLULARLOGFILE_H_

#include <QDateTime>
#include <QFile>
#include <QString>
#include <QTextStream>

// The log of the tester in a .csv file, as SDRangel's decoders log what they receive: appended to,
// the header written when the file is new. Columns: Date, Time, Network, Kind, Text.
class CellularLogFile
{
public:
    // closes the file that is open and opens this one (an empty name: none); false if it cannot
    bool open(const QString& filename);
    void close();
    bool isOpen() const { return m_file.isOpen(); }
    const QString& filename() const { return m_filename; }
    // `network`: its name, empty for the tester itself; `kind`: the badge of the log (FRAME, CALL ...)
    void write(const QDateTime& time, const QString& network, const QString& kind, const QString& text);

private:
    QString m_filename;
    QFile m_file;
    QTextStream m_stream;
};

#endif // INCLUDE_FEATURE_CELLULARLOGFILE_H_
