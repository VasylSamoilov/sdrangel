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

#ifndef INCLUDE_FEATURE_CELLULARCOLUMNS_H_
#define INCLUDE_FEATURE_CELLULARCOLUMNS_H_

#include <QList>
#include <QStringList>

class QTableWidget;

// Columns as SDRangel's tables have them: a menu on the header to show and hide them, moved and
// resized by the user, order and sizes kept in the settings (size 0: hidden, -1: auto, from the
// sample row).
namespace CellularColumns
{
    // `indexes` and `sizes` (the settings) follow what the user does to the columns
    void setup(QTableWidget *table, QList<int> *indexes, QList<int> *sizes, const QStringList& sample);
    void apply(QTableWidget *table, const QList<int>& indexes, const QList<int>& sizes);
}

#endif // INCLUDE_FEATURE_CELLULARCOLUMNS_H_
