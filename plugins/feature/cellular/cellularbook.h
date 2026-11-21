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

#ifndef INCLUDE_FEATURE_CELLULARBOOK_H_
#define INCLUDE_FEATURE_CELLULARBOOK_H_

#include <QByteArray>

#include "nmtphonebook.h"

// The book of phones in the settings of the feature (a blob of its own).
namespace CellularBook
{
QByteArray toBytes(const modemnmt::NmtPhoneBook& book);
// the records of the bytes merge into the book; false if the bytes cannot be read
bool fromBytes(const QByteArray& bytes, modemnmt::NmtPhoneBook& book);
}

#endif // INCLUDE_FEATURE_CELLULARBOOK_H_
