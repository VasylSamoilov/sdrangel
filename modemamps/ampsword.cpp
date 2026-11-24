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

#include "ampsword.h"

#include "ampsbch.h"

namespace modemamps
{

int WordDef::messageBits() const
{
    int bits = 0;

    for (const WordField& f : fields) {
        bits += f.bits;
    }
    return bits;
}

uint64_t encodeWord(const WordDef& def, const WordValues& values)
{
    uint64_t message = 0;

    for (const WordField& f : def.fields)
    {
        const auto it = values.find(f.name);
        const uint64_t mask = (f.bits >= 64) ? ~0ull : ((1ull << f.bits) - 1);
        message = (message << f.bits) | ((it == values.end() ? 0 : it->second) & mask);
    }
    return bchEncode(message, def.reverse ? kReverseMessageBits : kForwardMessageBits);
}

bool decodeWord(const WordDef& def, uint64_t word, WordValues& values)
{
    const int messageBits = def.reverse ? kReverseMessageBits : kForwardMessageBits;
    int shift = messageBits + kBchParityBits;

    values.clear();
    for (const WordField& f : def.fields)
    {
        shift -= f.bits;
        values[f.name] = (word >> shift) & ((1ull << f.bits) - 1);
    }
    return bchCheck(word, messageBits);
}

uint64_t wordField(const WordDef& def, uint64_t word, const std::string& name)
{
    const int messageBits = def.reverse ? kReverseMessageBits : kForwardMessageBits;
    int shift = messageBits + kBchParityBits;

    for (const WordField& f : def.fields)
    {
        shift -= f.bits;
        if (name == f.name) {
            return (word >> shift) & ((1ull << f.bits) - 1);
        }
    }
    return 0;
}

} // namespace modemamps
