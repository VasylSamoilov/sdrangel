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

#ifndef INCLUDE_RADIOPAGINGDEMODOUTPUT_H
#define INCLUDE_RADIOPAGINGDEMODOUTPUT_H

#include <QByteArray>

#include "util/messagerecord.h"

// The messages received, as the Pager demodulator outputs them
namespace RadioPagingDemodOutput {

//!< The datagram of a message in the format of the Pager demodulator: date, time, address, function bits,
//!< alphanumeric, numeric, protocol and type, each ended by a NUL
QByteArray pagerDatagram(const MessageRecord& record);
//!< An update that only completes an earlier record (the end of a GSC voice message, without text)
bool isCompletion(const MessageRecord& record);

} // namespace RadioPagingDemodOutput

#endif // INCLUDE_RADIOPAGINGDEMODOUTPUT_H
