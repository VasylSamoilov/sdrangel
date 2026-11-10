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
// an address field is at most 12 octets with its length and type octets (Annex 24 2.1.2.5)
const size_t kMaxAddressDigits = 20;

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
        size_t need = 0;
        uint32_t minimum = 0;

        if (c < 0x80) {
            code = c;
        } else if ((c & 0xe0) == 0xc0) {
            need = 1;
            minimum = 0x80;
            code = c & 0x1fu;
        } else if ((c & 0xf0) == 0xe0) {
            need = 2;
            minimum = 0x800;
            code = c & 0x0fu;
        } else if ((c & 0xf8) == 0xf0) {
            need = 3;
            minimum = 0x10000;
            code = c & 0x07u;
        }
        if (need)
        {
            bool good = true;

            for (size_t k = 1; k <= need && good; k++)
            {
                if (i + k >= text.size() || (static_cast<uint8_t>(text[i + k]) & 0xc0) != 0x80) {
                    good = false;
                } else {
                    code = (code << 6) | (static_cast<uint8_t>(text[i + k]) & 0x3fu);
                }
            }
            // overlong forms and surrogates are broken input: one question mark, one byte on
            if (good && code >= minimum && !(code >= 0xd800 && code < 0xe000) && code <= 0x10ffff) {
                n = need + 1;
            } else {
                code = 0x3f;
            }
        }
        else if (c >= 0x80) {
            code = 0x3f;
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
    value = std::max(0, std::min(value, 99));
    const int tens = (value / 10) % 10;
    const int units = value % 10;

    return static_cast<uint8_t>(((units ? units : 0xa) << 4) | (tens ? tens : 0xa));
}

int twoDigitsValue(uint8_t byte)
{
    // a digit above 9 (not 1010) is no digit: it counts as 0
    const int tens = (byte & 0xf) > 9 ? 0 : (byte & 0xf);
    const int units = (byte >> 4) > 9 ? 0 : (byte >> 4);

    return tens * 10 + units;
}

// The escape <0xNN> (two hex digits) at text[pos]: its value, or -1.
int escapeAt(const std::string& text, size_t pos)
{
    auto hex = [](char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };

    if (pos + 6 > text.size() || text.compare(pos, 3, "<0x") != 0 || text[pos + 5] != '>') {
        return -1;
    }
    const int high = hex(text[pos + 3]);
    const int low = hex(text[pos + 4]);

    return (high < 0 || low < 0) ? -1 : high * 16 + low;
}

// The character codes of a text to send: characters of the GSM alphabet (the escape 1Bh before
// one of its extension table), '?' for others, and the code NN for an escape <0xNN>. eightBit
// is set if a code is 80h or above: the text needs an NMT 8-bit set.
std::vector<uint8_t> textCodes(const std::string& text, bool& eightBit)
{
    std::vector<uint8_t> codes;
    size_t start = 0;

    eightBit = false;
    for (size_t pos = 0; pos <= text.size(); pos++)
    {
        const int value = (pos < text.size()) ? escapeAt(text, pos) : -1;

        if (value < 0 && pos < text.size()) {
            continue;
        }
        const std::vector<uint8_t> run = smsGsmEncode(text.substr(start, pos - start));
        codes.insert(codes.end(), run.begin(), run.end());
        if (value >= 0)
        {
            codes.push_back(static_cast<uint8_t>(value));
            eightBit = eightBit || value >= 0x80;
            pos += 5;
        }
        start = pos + 1;
    }

    return codes;
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

        if (c == 0x1b)
        {
            // the escape: the next character is taken from the extension table; one that is not
            // in it shows as the character of the default table, a lone escape as a blank
            if (i + 1 >= septets.size())
            {
                utf8Append(out, 0x20);
                continue;
            }
            const uint8_t next = septets[++i] & 0x7f;
            uint32_t code = (next == 0x1b) ? 0x20 : kGsm[next];

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

    // only complete characters: there are no more than the octets hold
    count = std::min(count, length * 8 / 7);
    out.reserve(count);
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

bool smsEncodeAddress(const std::string& number, std::vector<uint8_t>& out, int type, int plan)
{
    // characters that are no digit of an address are left out
    std::vector<int> digits;

    for (char c : number)
    {
        if (semiOctet(c) >= 0) {
            digits.push_back(semiOctet(c));
        }
    }
    if (digits.empty() || digits.size() > kMaxAddressDigits) {
        return false;
    }

    // without a type: international if the number starts with a plus sign, else unknown; the
    // numbering plan is that of the telephone network
    if (type < 0) {
        type = (number[0] == '+') ? 1 : 0;
    }
    if (plan < 0) {
        plan = 1;
    }
    out.push_back(static_cast<uint8_t>(digits.size()));
    out.push_back(static_cast<uint8_t>(0x80 | ((type & 7) << 4) | (plan & 0xf)));
    for (size_t i = 0; i < digits.size(); i += 2)
    {
        const int low = digits[i];
        const int high = (i + 1 < digits.size()) ? digits[i + 1] : 0xf;

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

    if (length < 2 + octets) {
        return SmsParse::NeedMore;
    }
    // Any type is taken and the semi-octets are read as digits; a filler or an unused code inside
    // the number becomes '?'.
    for (size_t i = 0; i < digits; i++)
    {
        const uint8_t byte = data[2 + i / 2];
        const int nibble = (i & 1) ? (byte >> 4) : (byte & 0xf);

        number.push_back((nibble == 0xe || nibble == 0xf) ? '?' : semiOctetChar(nibble));
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
    const int zone = std::min(std::abs(std::max(time.zoneQuarters, -79)), 79);

    out.push_back(twoDigits(time.year));
    out.push_back(twoDigits(time.month));
    out.push_back(twoDigits(time.day));
    out.push_back(twoDigits(time.hour));
    out.push_back(twoDigits(time.minute));
    out.push_back(twoDigits(time.second));
    // The zone (Annex 24 2.2.3.11): quarters of an hour, the units digit in the high semi-octet
    // (a zero is 1010, 2.1.2.3); the low one has the tens digit in 3 bits and the sign in bit 3, so
    // a zero tens digit is 000.
    const int units = zone % 10;

    out.push_back(static_cast<uint8_t>(((units ? units : 0xa) << 4) | ((zone / 10) & 7) | (time.zoneQuarters < 0 ? 8 : 0)));

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
    // a units digit of 1010 or 0000 is zero
    const int zoneUnits = data[6] >> 4;

    time.zoneQuarters = ((data[6] & 7) * 10) + (zoneUnits > 9 ? 0 : zoneUnits);
    if (data[6] & 8) {
        time.zoneQuarters = -time.zoneQuarters;
    }

    return time;
}

std::vector<uint8_t> smsHeader(const std::string& destination)
{
    std::vector<uint8_t> out = {0x01, 0x18, 'S', 'M', 'S', 'H', 0x18, 'A', 'B', 'C'};
    std::vector<uint8_t> address;

    // the header ends at the first 02h: an address that has one in it cannot go in
    if (!destination.empty() && smsEncodeAddress(destination, address)
     && std::find(address.begin(), address.end(), 0x02) == address.end())
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
    // The GSM alphabet goes out in septets (DCS 0). A code of 80h or above, which only an escape
    // <0xNN> can give, needs the NMT 8-bit set 0 (DCS 0x30): one octet per character, the first
    // 128 those of the GSM alphabet.
    bool eightBit = false;
    const std::vector<uint8_t> codes = textCodes(message.text, eightBit);
    const std::vector<uint8_t> userData = eightBit ? codes : smsPackSeptets(codes);

    if (codes.size() > kMaxChars) {
        return false;
    }
    const SmsTime& t = message.time;

    if (t.year < 0 || t.year > 99 || t.month < 0 || t.month > 12 || t.day < 0 || t.day > 31 || t.hour < 0 || t.hour > 23
     || t.minute < 0 || t.minute > 59 || t.second < 0 || t.second > 60 || t.zoneQuarters < -79 || t.zoneQuarters > 79) {
        return false;
    }
    // first octet: SMS-DELIVER, bit 2 is 0 when more messages wait (the other way round than GSM)
    tpdu.push_back(message.moreWaiting ? 0x00 : 0x04);
    if (!smsEncodeAddress(message.originator, tpdu, message.originatorType, message.originatorPlan)) {
        return false;
    }
    tpdu.push_back(0x00);                           // protocol identifier
    tpdu.push_back(eightBit ? 0x30 : 0x00);         // data coding scheme
    const std::vector<uint8_t> time = smsEncodeTime(message.time);
    tpdu.insert(tpdu.end(), time.begin(), time.end());
    tpdu.push_back(static_cast<uint8_t>(codes.size()));
    tpdu.insert(tpdu.end(), userData.begin(), userData.end());

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

    // the user data header bit is not looked at (a header is not defined; it would come as text)
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

    // validity period: none (00), one octet (10, also the reserved 01) or seven octets (11)
    const unsigned vpf = (first >> 3) & 3;
    const size_t vpOctets = (vpf == 0) ? 0 : (vpf == 3 ? 7 : 1);

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
        // 8-bit set: the first 128 characters are those of the GSM alphabet (with the escape), the
        // national ones are passed on as escapes <0xNN>
        std::string text;
        std::vector<uint8_t> run;

        for (size_t i = 0; i < udl; i++)
        {
            if (data[pos + i] & 0x80)
            {
                static const char kHex[] = "0123456789abcdef";

                text += smsGsmDecode(run);
                run.clear();
                text += "<0x";
                text.push_back(kHex[data[pos + i] >> 4]);
                text.push_back(kHex[data[pos + i] & 0xf]);
                text.push_back('>');
            }
            else {
                run.push_back(data[pos + i]);
            }
        }
        out.text = text + smsGsmDecode(run);
    }
    pos += udOctets;
    consumed = pos;

    return SmsParse::Ok;
}

} // namespace

namespace
{

// An optional user data element (41h, length, TPDU) after an RP-ACK or RP-ERROR is skipped.
SmsParse skipUserData(const uint8_t* data, size_t length, size_t& pos)
{
    if (pos < length && data[pos] == 0x41)
    {
        if (length < pos + 2) {
            return SmsParse::NeedMore;
        }
        const size_t n = data[pos + 1];

        if (length < pos + 2 + n) {
            return SmsParse::NeedMore;
        }
        pos += 2 + n;
    }

    return SmsParse::Ok;
}

SmsParse parseFromPhone(const uint8_t* data, size_t length, SmsFromPhone& out, size_t& consumed)
{
    if (length < 2) {
        return SmsParse::NeedMore;
    }

    // bits 7..3 are spare
    const unsigned mti = data[0] & 7;

    out.rpRef = data[1];

    switch (mti)
    {
    case 2:                                         // RP-ACK
    {
        size_t pos = 2;

        out.kind = SmsRpKind::Ack;
        const SmsParse r = skipUserData(data, length, pos);

        consumed = pos;
        return r;
    }

    case 4:                                         // RP-ERROR with a cause
    {
        size_t pos = 2;

        out.kind = SmsRpKind::Error;
        if (length >= 3 && data[2] == 0x42)
        {
            if (length < 4) {
                return SmsParse::NeedMore;
            }
            const size_t n = data[3];

            if (length < 4 + n) {
                return SmsParse::NeedMore;
            }
            out.cause = (n > 0) ? (data[4] & 0x7f) : 0;
            pos = 4 + n;
        }
        else if (length < 3) {
            return SmsParse::NeedMore;
        }
        const SmsParse r = skipUserData(data, length, pos);

        consumed = pos;
        return r;
    }

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
        // The length byte is not trusted (it is 255 for a long one, and wrong in the example of the
        // standard); the fields of the transfer layer say where it ends.
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

} // namespace

SmsParse smsParseFromPhone(const uint8_t* data, size_t length, SmsFromPhone& out, size_t& consumed)
{
    out = SmsFromPhone();
    consumed = 0;

    const SmsParse result = parseFromPhone(data, length, out, consumed);

    // What is not a message cannot be skipped piece by piece: the caller drops what it has.
    if (result == SmsParse::Error)
    {
        out = SmsFromPhone();
        consumed = length;
    }
    else if (result == SmsParse::NeedMore) {
        consumed = 0;
    }

    return result;
}

} // namespace modemnmt
