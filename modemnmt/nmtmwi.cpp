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

#include "nmtmwi.h"

#include <cctype>

namespace modemnmt
{

namespace
{

// i1..i6 as the six bits of the frame, i1 first (most significant)
uint16_t sixBits(uint8_t flags)
{
    return static_cast<uint16_t>(((flags & MwiSms) ? 0x20 : 0) | ((flags & MwiVoice) ? 0x10 : 0)
                               | ((flags & MwiFax) ? 0x08 : 0) | ((flags & MwiEmail) ? 0x04 : 0)
                               | ((flags & MwiData) ? 0x02 : 0));
}

} // namespace

uint16_t mwiWireInfo(uint8_t flags)
{
    const uint16_t i = sixBits(flags);

    return static_cast<uint16_t>((i << 6) | i);
}

bool mwiFlagsFromWire(uint16_t info, uint8_t& flags)
{
    const uint16_t i = (info >> 6) & 0x3f;

    if (i != (info & 0x3f) || (info & ~0xfffu)) {
        return false;
    }

    flags = static_cast<uint8_t>(((i & 0x20) ? MwiSms : 0) | ((i & 0x10) ? MwiVoice : 0) | ((i & 0x08) ? MwiFax : 0)
                               | ((i & 0x04) ? MwiEmail : 0) | ((i & 0x02) ? MwiData : 0));
    return true;
}

bool mwiParseFlags(const std::string& text, uint8_t& flags)
{
    // a number
    if (!text.empty() && std::isdigit(static_cast<unsigned char>(text[0])))
    {
        unsigned value = 0;

        for (char c : text)
        {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                return false;
            }
            value = value * 10 + static_cast<unsigned>(c - '0');
            if (value > MwiAll) {
                return false;
            }
        }
        if (value == 0) {
            return false;
        }
        flags = static_cast<uint8_t>(value);
        return true;
    }

    // words separated by commas, spaces or plus signs
    uint8_t result = 0;
    std::string word;

    for (size_t i = 0; i <= text.size(); i++)
    {
        const char c = i < text.size() ? static_cast<char>(std::tolower(static_cast<unsigned char>(text[i]))) : ',';

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '-')
        {
            word.push_back(c);
            continue;
        }
        if (word.empty()) {
            continue;
        }
        if (word == "sms") {
            result |= MwiSms;
        } else if (word == "voice") {
            result |= MwiVoice;
        } else if (word == "fax") {
            result |= MwiFax;
        } else if (word == "email" || word == "e-mail") {
            result |= MwiEmail;
        } else if (word == "data") {
            result |= MwiData;
        } else {
            return false;
        }
        word.clear();
    }

    if (result == 0) {
        return false;
    }
    flags = result;
    return true;
}

std::string mwiFlagsText(uint8_t flags)
{
    std::string text;
    const struct { uint8_t flag; const char* name; } names[] = {
        {MwiSms, "sms"}, {MwiVoice, "voice"}, {MwiFax, "fax"}, {MwiEmail, "email"}, {MwiData, "data"}
    };

    for (const auto& entry : names)
    {
        if (flags & entry.flag)
        {
            if (!text.empty()) {
                text += ',';
            }
            text += entry.name;
        }
    }

    return text;
}

} // namespace modemnmt
