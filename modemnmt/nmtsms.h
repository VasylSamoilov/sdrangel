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

#ifndef MODEMNMT_NMTSMS_H_
#define MODEMNMT_NMTSMS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "export.h"

namespace modemnmt
{

// The short message service of NMT (NMT Doc 450-3 and 900-3 Annex 24): the relay layer (RP) and the
// transfer layer (TP) of GSM 03.40 / 04.11 in the octets that go over the data link. Pure functions,
// no state. Text is UTF-8 at the interface; messages are sent in the GSM default alphabet.
// The national 8-bit sets (DCS 0x30..0x3F) are received with their first 128 characters only.

// The time a message was received at the service centre.
struct SmsTime
{
    int year = 0;           // two digits
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int zoneQuarters = 0;   // time zone in quarters of an hour, east of UTC positive
};

// A message from the service centre to the phone (SMS-DELIVER).
struct SmsDeliver
{
    std::string originator;     // "+35849654321" or digits
    std::string text;           // UTF-8
    SmsTime time;
    bool moreWaiting = false;   // more messages wait for the phone in the service centre
};

// A message from the phone to the service centre (SMS-SUBMIT) with the relay layer around it.
struct SmsSubmit
{
    uint8_t rpRef = 0;
    std::string originator;
    uint8_t messageRef = 0;
    std::string destination;
    std::string text;           // UTF-8
    uint8_t dcs = 0;
    bool hasValidity = false;
    uint8_t validity = 0;       // relative validity period, if there is one
};

enum class SmsParse
{
    Ok,
    NeedMore,       // the octets so far are the start of a message
    Error           // not a message
};

enum class SmsRpKind
{
    Submit,             // RP-MO-DATA with an SMS-SUBMIT
    Ack,                // RP-ACK from the phone
    Error,              // RP-ERROR from the phone
    MemoryAvailable,    // RP-SM-MEMORY-AVAILABLE
    ReadyToReceive      // RP-SM-READY-TO-RECEIVE: the phone asks for waiting messages
};

// What the phone sends over the data link, one relay layer message.
struct SmsFromPhone
{
    SmsRpKind kind = SmsRpKind::Ack;
    uint8_t rpRef = 0;
    uint8_t cause = 0;          // Error: the RP cause (0x16 memory full, 0x6f protocol error)
    std::string address;        // MemoryAvailable, ReadyToReceive: the phone's number
    SmsSubmit submit;           // Submit
};

// RP cause values
enum SmsRpCause : uint8_t
{
    SmsCauseMemoryFull = 0x16,
    SmsCauseProtocolError = 0x6f
};

// TP failure cause values (SMS-DELIVER-REPORT, SMS-SUBMIT-REPORT)
enum SmsFailureCause : uint8_t
{
    SmsFcsScBusy = 0xc0,
    SmsFcsNoSubscription = 0xc1,
    SmsFcsSystemFailure = 0xc2,
    SmsFcsDestinationBarred = 0xc4,
    SmsFcsErrorInPhone = 0xd2,
    SmsFcsMemoryExceeded = 0xd3,
    SmsFcsUnspecified = 0xff
};

// The header that goes before every message to the phone: 01 18 'S' 'M' 'S' 'H' 18 'A' 'B' 'C' 02.
// With a destination the phone checks the address (01 41 length type digits).
MODEMNMT_API std::vector<uint8_t> smsHeader(const std::string& destination = "");

// RP-MT-DATA with an SMS-DELIVER: the octets after the header. False if the originator or the text
// cannot be coded (not a number, characters outside the alphabet are sent as '?', text over 160).
MODEMNMT_API bool smsBuildDeliver(const SmsDeliver& message, uint8_t rpRef, std::vector<uint8_t>& out);
// RP-ACK (answer to a submit), RP-ERROR with a cause, and "no messages waiting" (to the phone).
MODEMNMT_API std::vector<uint8_t> smsRpAckToPhone(uint8_t rpRef);
MODEMNMT_API std::vector<uint8_t> smsRpErrorToPhone(uint8_t rpRef, uint8_t cause);
MODEMNMT_API std::vector<uint8_t> smsRpNoMessages(uint8_t rpRef);

// One message from the phone at the start of the octets. consumed tells how many octets it took.
MODEMNMT_API SmsParse smsParseFromPhone(const uint8_t* data, size_t length, SmsFromPhone& out, size_t& consumed);

// The pieces, public for the tests.
// Address: length in semi-octets, type, digits (0 is 1010, * 1011, # 1100, + 1101, filler 1111).
MODEMNMT_API bool smsEncodeAddress(const std::string& number, std::vector<uint8_t>& out);
MODEMNMT_API SmsParse smsDecodeAddress(const uint8_t* data, size_t length, std::string& number, size_t& consumed);
MODEMNMT_API std::vector<uint8_t> smsEncodeTime(const SmsTime& time);
MODEMNMT_API SmsTime smsDecodeTime(const uint8_t* data);
// UTF-8 to the characters of the GSM default alphabet (septets, with the escape for the
// extension table); unknown characters become '?'.
MODEMNMT_API std::vector<uint8_t> smsGsmEncode(const std::string& utf8);
MODEMNMT_API std::string smsGsmDecode(const std::vector<uint8_t>& septets);
MODEMNMT_API std::vector<uint8_t> smsPackSeptets(const std::vector<uint8_t>& septets);
MODEMNMT_API std::vector<uint8_t> smsUnpackSeptets(const uint8_t* data, size_t length, size_t count);

} // namespace modemnmt

#endif // MODEMNMT_NMTSMS_H_
