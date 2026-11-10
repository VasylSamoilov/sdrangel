///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Andreas Eversberg <jolly@eversberg.eu>                     //
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

#ifndef MODEMNMT_NMTMWI_H_
#define MODEMNMT_NMTMWI_H_

#include <cstdint>
#include <string>

#include "export.h"

namespace modemnmt
{

// Message waiting indicators (NMT Doc 450-1 para 4.3.3.11, frame 5c). The bits are the indicators
// as a base station sets them; the frame has its own order, see mwiWireInfo().
enum MwiFlag : uint8_t
{
    MwiSms = 0x01,
    MwiVoice = 0x02,
    MwiFax = 0x04,
    MwiEmail = 0x08,
    MwiData = 0x10,
    MwiAll = 0x1f
};

// How a message waiting delivery ended.
enum class NmtServiceEnd
{
    Ok,             // the phone acknowledged the clearing that carried the indicators
    NoAck,          // the indicators were sent, the phone did not acknowledge
    Undelivered,    // they were not sent: the call ended before, or the phone did not answer
    Unreachable,    // the phone did not answer the page
    NoChannel,      // no traffic channel became free
    Rejected        // a short message: the phone refused it (RP-ERROR, with a cause)
};

// The 12 bits F1F2F3 of frame 5c: i1..i6 (SMS, voice mail, fax, e-mail, data, spare) followed by
// the same six bits again. Only set indicators are sent; a 0 does not reset an indicator.
// SMS only is 0x820, voice mail 0x410.
MODEMNMT_API uint16_t mwiWireInfo(uint8_t flags);
// The indicators of a frame 5c, false if the two copies of the six bits differ (the phone
// does not accept the frame then).
MODEMNMT_API bool mwiFlagsFromWire(uint16_t info, uint8_t& flags);
// "sms,voice" or "sms voice fax email data" or a number 1..31. False if there is nothing valid.
MODEMNMT_API bool mwiParseFlags(const std::string& text, uint8_t& flags);
// "sms,voice"
MODEMNMT_API std::string mwiFlagsText(uint8_t flags);

} // namespace modemnmt

#endif // MODEMNMT_NMTMWI_H_
