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

#include "nmtsms.h"

#include <algorithm>
#include <cstdlib>

namespace modemnmt
{

namespace
{

const size_t kMaxChars = 160;
const size_t kMaxAddressDigits = 20;                // 10 octets of digits are ample (the standard allows 12)

// The GSM default alphabet (GSM 03.38) as Unicode code points, 0x1b is the escape.
const uint32_t kGsm[128] = {
    0x40, 0xa3, 0x24, 0xa5, 0xe8, 0xe9, 0xf9, 0xec, 0xf2, 0xc7, 0x0a, 0xd8, 0xf8, 0x0d, 0xc5, 0xe5,
    0x394, 0x5f, 0x3a6, 0x393, 0x39b, 0x3a9, 0x3a0, 0x3a8, 0x3a3, 0x398, 0x39e, 0x1b, 0xc6, 0xe6, 0xdf, 0xc9,
    0x20, 0x21, 0x22, 0x23, 0xa4, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0xa1, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0xc4, 0xd6, 0xd1, 0xdc, 0xa7,
    0xbf, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0xe4, 0xf6, 0xf1, 0xfc, 0xe0
};

// after the escape
struct Extension { uint8_t code; uint32_t character; };
const Extension kExtension[] = {
    {0x0a, 0x0c}, {0x14, 0x5e}, {0x28, 0x7b}, {0x29, 0x7d}, {0x2f, 0x5c}, {0x3c, 0x5b},
    {0x3d, 0x7e}, {0x3e, 0x5d}, {0x40, 0x7c}, {0x65, 0x20ac}
};

std::vector<uint32_t> utf8Decode(const std::string& text)
{
    std::vector<uint32_t> out;

    for (size_t i = 0; i < text.size();)
    {
        const uint8_t c = static_cast<uint8_t>(text[i]);
        uint32_t code = 0x3f;
        size_t n = 1;

        if (c < 0x80) {
            code = c;
        } else if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) {
            code = ((c & 0x1fu) << 6) | (static_cast<uint8_t>(text[i + 1]) & 0x3f);
            n = 2;
        } else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) {
            code = ((c & 0x0fu) << 12) | ((static_cast<uint8_t>(text[i + 1]) & 0x3fu) << 6) | (static_cast<uint8_t>(text[i + 2]) & 0x3f);
            n = 3;
        } else if ((c & 0xf8) == 0xf0 && i + 3 < text.size()) {
            n = 4;                                  // no character of the alphabet is that high
        }
        out.push_back(code);
        i += n;
    }

    return out;
}

void utf8Append(std::string& out, uint32_t code)
{
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800)
    {
        out.push_back(static_cast<char>(0xc0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
    else
    {
        out.push_back(static_cast<char>(0xe0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

// the semi-octet of a character of an address, -1 if there is none
int semiOctet(char c)
{
    if (c == '0') {
        return 0xa;
    }
    if (c >= '1' && c <= '9') {
        return c - '0';
    }
    if (c == '*') {
        return 0xb;
    }
    if (c == '#') {
        return 0xc;
    }
    if (c == '+') {
        return 0xd;
    }

    return -1;
}

char semiOctetChar(int nibble)
{
    if (nibble == 0xa || nibble == 0) {
        return '0';
    }
    if (nibble >= 1 && nibble <= 9) {
        return static_cast<char>('0' + nibble);
    }
    if (nibble == 0xb) {
        return '*';
    }
    if (nibble == 0xc) {
        return '#';
    }
    if (nibble == 0xd) {
        return '+';
    }

    return '?';
}

// two decimal digits as a swapped semi-octet pair; a zero is 1010
uint8_t twoDigits(int value)
{
    const int tens = (value / 10) % 10;
    const int units = value % 10;

    return static_cast<uint8_t>(((units ? units : 0xa) << 4) | (tens ? tens : 0xa));
}

int twoDigitsValue(uint8_t byte)
{
    const int tens = (byte & 0xf) == 0xa ? 0 : (byte & 0xf);
    const int units = (byte >> 4) == 0xa ? 0 : (byte >> 4);

    return tens * 10 + units;
}

// The text as it is sent: septets of the GSM alphabet. False if it does not fit.
bool textSeptets(const std::string& text, std::vector<uint8_t>& septets)
{
    septets = smsGsmEncode(text);
    return septets.size() <= kMaxChars;
}

} // namespace

std::vector<uint8_t> smsGsmEncode(const std::string& utf8)
{
    std::vector<uint8_t> out;

    for (uint32_t code : utf8Decode(utf8))
    {
        bool found = false;

        for (uint32_t i = 0; i < 128 && !found; i++)
        {
            if (i != 0x1b && kGsm[i] == code)
            {
                out.push_back(static_cast<uint8_t>(i));
                found = true;
            }
        }
        for (const Extension& e : kExtension)
        {
            if (!found && e.character == code)
            {
                out.push_back(0x1b);
                out.push_back(e.code);
                found = true;
            }
        }
        if (!found) {
            out.push_back(0x3f);                    // '?'
        }
    }

    return out;
}

std::string smsGsmDecode(const std::vector<uint8_t>& septets)
{
    std::string out;

    for (size_t i = 0; i < septets.size(); i++)
    {
        const uint8_t c = septets[i] & 0x7f;

        if (c == 0x1b && i + 1 < septets.size())
        {
            const uint8_t next = septets[++i] & 0x7f;
            uint32_t code = 0x20;                   // an unknown escape is a blank

            for (const Extension& e : kExtension)
            {
                if (e.code == next) {
                    code = e.character;
                }
            }
            utf8Append(out, code);
            continue;
        }
        utf8Append(out, kGsm[c]);
    }

    return out;
}

std::vector<uint8_t> smsPackSeptets(const std::vector<uint8_t>& septets)
{
    std::vector<uint8_t> out((septets.size() * 7 + 7) / 8, 0);

    for (size_t i = 0; i < septets.size(); i++)
    {
        const size_t bit = i * 7;
        const unsigned value = septets[i] & 0x7fu;

        out[bit / 8] = static_cast<uint8_t>(out[bit / 8] | (value << (bit % 8)));
        if (bit % 8 > 1) {
            out[bit / 8 + 1] = static_cast<uint8_t>(out[bit / 8 + 1] | (value >> (8 - bit % 8)));
        }
    }

    return out;
}

std::vector<uint8_t> smsUnpackSeptets(const uint8_t* data, size_t length, size_t count)
{
    std::vector<uint8_t> out;

    for (size_t i = 0; i < count; i++)
    {
        const size_t bit = i * 7;
        unsigned value = 0;

        if (bit / 8 < length) {
            value = data[bit / 8] >> (bit % 8);
        }
        if (bit % 8 > 1 && bit / 8 + 1 < length) {
            value |= static_cast<unsigned>(data[bit / 8 + 1]) << (8 - bit % 8);
        }
        out.push_back(static_cast<uint8_t>(value & 0x7f));
    }

    return out;
}

bool smsEncodeAddress(const std::string& number, std::vector<uint8_t>& out)
{
    if (number.empty() || number.size() > kMaxAddressDigits) {
        return false;
    }
    for (size_t i = 0; i < number.size(); i++)
    {
        // a plus sign only at the start
        if (semiOctet(number[i]) < 0 || (number[i] == '+' && i != 0)) {
            return false;
        }
    }

    // international with the numbering plan of the telephone network, else unknown
    out.push_back(static_cast<uint8_t>(number.size()));
    out.push_back(number[0] == '+' ? 0x91 : 0x81);
    for (size_t i = 0; i < number.size(); i += 2)
    {
        const int low = semiOctet(number[i]);
        const int high = (i + 1 < number.size()) ? semiOctet(number[i + 1]) : 0xf;

        out.push_back(static_cast<uint8_t>((high << 4) | low));
    }

    return true;
}

SmsParse smsDecodeAddress(const uint8_t* data, size_t length, std::string& number, size_t& consumed)
{
    number.clear();
    if (length < 2) {
        return SmsParse::NeedMore;
    }

    const size_t digits = data[0];
    const size_t octets = (digits + 1) / 2;

    if (digits > 24 || !(data[1] & 0x80)) {
        return SmsParse::Error;
    }
    if (length < 2 + octets) {
        return SmsParse::NeedMore;
    }
    for (size_t i = 0; i < digits; i++)
    {
        const uint8_t byte = data[2 + i / 2];

        number.push_back(semiOctetChar((i & 1) ? (byte >> 4) : (byte & 0xf)));
    }
    // a number in international format is shown with its plus sign if the digits lack it
    if (((data[1] >> 4) & 7) == 1 && !number.empty() && number[0] != '+') {
        number.insert(number.begin(), '+');
    }
    consumed = 2 + octets;

    return SmsParse::Ok;
}

std::vector<uint8_t> smsEncodeTime(const SmsTime& time)
{
    std::vector<uint8_t> out;
    const int zone = std::abs(time.zoneQuarters) % 100;

    out.push_back(twoDigits(time.year));
    out.push_back(twoDigits(time.month));
    out.push_back(twoDigits(time.day));
    out.push_back(twoDigits(time.hour));
    out.push_back(twoDigits(time.minute));
    out.push_back(twoDigits(time.second));
    // the zone: plain digits, the sign in bit 3 of the first semi-octet
    out.push_back(static_cast<uint8_t>(((zone % 10) << 4) | ((zone / 10) & 7) | (time.zoneQuarters < 0 ? 8 : 0)));

    return out;
}

SmsTime smsDecodeTime(const uint8_t* data)
{
    SmsTime time;

    time.year = twoDigitsValue(data[0]);
    time.month = twoDigitsValue(data[1]);
    time.day = twoDigitsValue(data[2]);
    time.hour = twoDigitsValue(data[3]);
    time.minute = twoDigitsValue(data[4]);
    time.second = twoDigitsValue(data[5]);
    time.zoneQuarters = ((data[6] & 7) * 10) + (data[6] >> 4);
    if (data[6] & 8) {
        time.zoneQuarters = -time.zoneQuarters;
    }

    return time;
}

std::vector<uint8_t> smsHeader(const std::string& destination)
{
    std::vector<uint8_t> out = {0x01, 0x18, 'S', 'M', 'S', 'H', 0x18, 'A', 'B', 'C'};
    std::vector<uint8_t> address;

    if (!destination.empty() && smsEncodeAddress(destination, address))
    {
        out.push_back(0x01);
        out.push_back(0x41);
        out.insert(out.end(), address.begin(), address.end());
    }
    out.push_back(0x02);

    return out;
}

bool smsBuildDeliver(const SmsDeliver& message, uint8_t rpRef, std::vector<uint8_t>& out)
{
    std::vector<uint8_t> tpdu;
    std::vector<uint8_t> septets;

    if (!textSeptets(message.text, septets)) {
        return false;
    }
    // first octet: SMS-DELIVER, bit 2 is 0 when more messages wait (the other way round than GSM)
    tpdu.push_back(message.moreWaiting ? 0x00 : 0x04);
    if (!smsEncodeAddress(message.originator, tpdu)) {
        return false;
    }
    tpdu.push_back(0x00);                           // protocol identifier
    tpdu.push_back(0x00);                           // data coding scheme: the GSM alphabet
    const std::vector<uint8_t> time = smsEncodeTime(message.time);
    tpdu.insert(tpdu.end(), time.begin(), time.end());
    tpdu.push_back(static_cast<uint8_t>(septets.size()));
    const std::vector<uint8_t> packed = smsPackSeptets(septets);
    tpdu.insert(tpdu.end(), packed.begin(), packed.end());

    if (tpdu.size() > 255) {
        return false;
    }
    out.clear();
    out.push_back(0x01);                            // RP-MT-DATA
    out.push_back(rpRef);
    out.push_back(0x41);                            // user data
    out.push_back(static_cast<uint8_t>(tpdu.size()));
    out.insert(out.end(), tpdu.begin(), tpdu.end());

    return true;
}

std::vector<uint8_t> smsRpAckToPhone(uint8_t rpRef)
{
    return {0x03, rpRef};
}

std::vector<uint8_t> smsRpErrorToPhone(uint8_t rpRef, uint8_t cause)
{
    return {0x05, rpRef, 0x42, 0x01, static_cast<uint8_t>(cause & 0x7f)};
}

std::vector<uint8_t> smsRpNoMessages(uint8_t rpRef)
{
    return {0x07, rpRef};
}

namespace
{

// The transfer layer of a submit: starts at the first octet.
SmsParse parseSubmitTpdu(const uint8_t* data, size_t length, SmsSubmit& out, size_t& consumed)
{
    size_t pos = 0;

    if (length < 2) {
        return SmsParse::NeedMore;
    }
    const uint8_t first = data[pos++];

    if ((first & 3) != 1) {
        return SmsParse::Error;                     // not an SMS-SUBMIT
    }
    out.messageRef = data[pos++];

    size_t used = 0;
    SmsParse result = smsDecodeAddress(data + pos, length - pos, out.destination, used);

    if (result != SmsParse::Ok) {
        return result;
    }
    pos += used;

    if (length < pos + 2) {
        return SmsParse::NeedMore;
    }
    const uint8_t pid = data[pos++];

    (void) pid;
    out.dcs = data[pos++];

    // validity period: none (00), one octet (10) or seven octets (11)
    const unsigned vpf = (first >> 3) & 3;
    const size_t vpOctets = (vpf == 2) ? 1 : (vpf == 3 ? 7 : 0);

    if (vpf == 1) {
        return SmsParse::Error;
    }
    if (length < pos + vpOctets + 1) {
        return SmsParse::NeedMore;
    }
    out.hasValidity = (vpf == 2);
    if (vpf == 2) {
        out.validity = data[pos];
    }
    pos += vpOctets;

    const size_t udl = data[pos++];
    size_t udOctets;

    if (out.dcs == 0x00) {
        udOctets = (udl * 7 + 7) / 8;
    } else if ((out.dcs & 0xf0) == 0x30) {
        udOctets = udl;
    } else {
        return SmsParse::Error;                     // an alphabet that is not supported
    }
    if (udl > 255 || (out.dcs == 0x00 && udl > kMaxChars)) {
        return SmsParse::Error;
    }
    if (length < pos + udOctets) {
        return SmsParse::NeedMore;
    }
    if (out.dcs == 0x00) {
        out.text = smsGsmDecode(smsUnpackSeptets(data + pos, udOctets, udl));
    }
    else
    {
        // 8-bit set: the first 128 characters are those of the GSM alphabet
        std::string text;

        for (size_t i = 0; i < udl; i++) {
            utf8Append(text, (data[pos + i] & 0x80) ? 0x3f : kGsm[data[pos + i]]);
        }
        out.text = text;
    }
    pos += udOctets;
    consumed = pos;

    return SmsParse::Ok;
}

} // namespace

SmsParse smsParseFromPhone(const uint8_t* data, size_t length, SmsFromPhone& out, size_t& consumed)
{
    out = SmsFromPhone();
    consumed = 0;
    if (length < 2) {
        return SmsParse::NeedMore;
    }

    const unsigned mti = data[0] & 7;

    out.rpRef = data[1];
    if (data[0] & 0xf8) {
        return SmsParse::Error;
    }

    switch (mti)
    {
    case 2:                                         // RP-ACK
        out.kind = SmsRpKind::Ack;
        consumed = 2;
        return SmsParse::Ok;

    case 4:                                         // RP-ERROR with a cause
        out.kind = SmsRpKind::Error;
        if (length < 3) {
            return SmsParse::NeedMore;
        }
        if (data[2] != 0x42)
        {
            consumed = 2;                           // no cause
            return SmsParse::Ok;
        }
        if (length < 5) {
            return SmsParse::NeedMore;
        }
        out.cause = data[4] & 0x7f;
        consumed = 5;
        return SmsParse::Ok;

    case 6:                                         // memory available
    case 7:                                         // ready to receive
    {
        size_t used = 0;
        const SmsParse result = smsDecodeAddress(data + 2, length - 2, out.address, used);

        out.kind = (mti == 6) ? SmsRpKind::MemoryAvailable : SmsRpKind::ReadyToReceive;
        if (result != SmsParse::Ok) {
            return result;
        }
        consumed = 2 + used;
        return SmsParse::Ok;
    }

    case 0:                                         // RP-MO-DATA
    {
        size_t pos = 2;
        size_t used = 0;

        out.kind = SmsRpKind::Submit;
        out.submit.rpRef = data[1];
        SmsParse result = smsDecodeAddress(data + pos, length - pos, out.submit.originator, used);

        if (result != SmsParse::Ok) {
            return result;
        }
        pos += used;
        if (length < pos + 2) {
            return SmsParse::NeedMore;
        }
        if (data[pos] != 0x41) {
            return SmsParse::Error;
        }
        // The length byte is not trusted (it is 255 for a long one); the fields of the transfer layer say where it ends.
        pos += 2;
        result = parseSubmitTpdu(data + pos, length - pos, out.submit, used);
        if (result != SmsParse::Ok) {
            return result;
        }
        consumed = pos + used;
        return SmsParse::Ok;
    }

    default:
        return SmsParse::Error;
    }
}

} // namespace modemnmt
