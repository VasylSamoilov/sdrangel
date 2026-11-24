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

#include "ampsstream.h"

#include "ampsbch.h"

namespace modemamps
{

namespace
{

// dotting 1010..., ending with 1 when the length is odd (1010...101) as the figures show
void dotting(std::vector<uint8_t>& out, int bits)
{
    for (int i = 0; i < bits; i++) {
        out.push_back(static_cast<uint8_t>((i & 1) == 0));
    }
}

void pushBits(std::vector<uint8_t>& out, uint64_t value, int bits)
{
    for (int i = bits - 1; i >= 0; i--) {
        out.push_back(static_cast<uint8_t>((value >> i) & 1u));
    }
}

} // namespace

std::vector<uint8_t> foccTrain(uint64_t wordA, uint64_t wordB, bool busy)
{
    const uint8_t bi = busy ? 0 : 1;
    std::vector<uint8_t> out;

    out.reserve(kFoccTrainBits);
    // the busy-idle bit at the beginning of the dotting is the last one of the previous train
    dotting(out, kFoccDottingBits);
    out.push_back(bi);
    pushBits(out, kWordSync, kWordSyncBits);
    out.push_back(bi);
    for (int repeat = 0; repeat < 5; repeat++)
    {
        for (uint64_t word : {wordA, wordB})
        {
            for (int j = kForwardWordBits - 1; j >= 0; j--)
            {
                out.push_back(static_cast<uint8_t>((word >> j) & 1u));
                if (j % 10 == 0) {
                    out.push_back(bi);
                }
            }
        }
    }
    return out;
}

bool foccBusyIdleBit(int position)
{
    if (position == kFoccDottingBits || position == kFoccDottingBits + 1 + kWordSyncBits) {
        return true;
    }
    const int first = kFoccDottingBits + 1 + kWordSyncBits + 1;     // 23: the first message bit
    return position > first && (position - first) % 11 == 10;
}

std::vector<uint8_t> fvcWord(uint64_t word)
{
    std::vector<uint8_t> out;

    out.reserve(kFvcWordBits);
    for (int repeat = 0; repeat < kFvcRepeats; repeat++)
    {
        dotting(out, repeat == 0 ? 101 : 37);
        pushBits(out, kWordSync, kWordSyncBits);
        pushBits(out, word, kForwardWordBits);
    }
    return out;
}

uint8_t codedDcc(int dcc)
{
    static const uint8_t table[4] = {0x00, 0x1f, 0x63, 0x7c};   // 0000000 0011111 1100011 1111100

    return table[dcc & 3];
}

std::vector<uint8_t> reccMessage(int dcc, const std::vector<uint64_t>& words)
{
    std::vector<uint8_t> out;

    dotting(out, kReccDottingBits);
    pushBits(out, kWordSync, kWordSyncBits);
    pushBits(out, codedDcc(dcc), 7);
    for (uint64_t word : words)
    {
        for (int repeat = 0; repeat < kReccRepeats; repeat++) {
            pushBits(out, word, kReverseWordBits);
        }
    }
    return out;
}

std::vector<uint8_t> rvcMessage(const std::vector<uint64_t>& words)
{
    std::vector<uint8_t> out;
    bool first = true;

    for (uint64_t word : words)
    {
        for (int repeat = 0; repeat < kRvcRepeats; repeat++)
        {
            dotting(out, first ? 101 : 37);
            first = false;
            pushBits(out, kWordSync, kWordSyncBits);
            pushBits(out, word, kReverseWordBits);
        }
    }
    return out;
}

} // namespace modemamps
