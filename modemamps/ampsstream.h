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

#ifndef MODEMAMPS_AMPSSTREAM_H_
#define MODEMAMPS_AMPSSTREAM_H_

#include <cstdint>
#include <vector>

#include "export.h"

namespace modemamps
{

// The bit streams of the AMPS data channels before the Manchester coding (TIA/EIA-553-A 3.7.1,
// 3.7.2, 2.7.1, 2.7.2), 10 kbit/s. Bits are 0 or 1, the first one sent first; words are given
// with their parity (see ampsbch.h), the most significant bit first.

// dotting 1010...  and the word sync of all four channels
constexpr uint32_t kWordSync = 0x712;           // 11100010010
constexpr int kWordSyncBits = 11;

// Forward control channel (3.7.1, figure 3.7.1-1): 10 bits of dotting, the word sync, then the
// words A and B interleaved, each repeated 5 times (A1 B1 A2 B2 .. A5 B5). A busy-idle bit is at
// the beginning of the dotting, at the beginning of the word sync, at the beginning of the first
// repeat of word A and after every 10 message bits thereafter: 463 bits, of which 42 are
// busy-idle bits (the last one is the one at the beginning of the next train's dotting).
constexpr int kFoccDottingBits = 10;
constexpr int kFoccTrainBits = 463;
// busy-idle: busy is 0, idle is 1 (the state of the reverse control channel)
MODEMAMPS_API std::vector<uint8_t> foccTrain(uint64_t wordA, uint64_t wordB, bool busy);
// true for the positions of the busy-idle bits in a train
MODEMAMPS_API bool foccBusyIdleBit(int position);

// Forward voice channel (3.7.2, figure 3.7.2-1): the word repeated 11 times, each repeat after 37
// bits of dotting and the word sync, the first one after 101 bits of dotting: 1032 bits.
constexpr int kFvcRepeats = 11;
constexpr int kFvcWordBits = 1032;
MODEMAMPS_API std::vector<uint8_t> fvcWord(uint64_t word);

// Reverse control channel (2.7.1, figure 2.7.1-1): the seizure precursor (30 bits of dotting, the
// word sync, the coded digital color code of 7 bits, table 2.7.1-1), then each word repeated 5
// times.
constexpr int kReccDottingBits = 30;
constexpr int kReccPrecursorBits = kReccDottingBits + kWordSyncBits + 7;    // 48
constexpr int kReccRepeats = 5;
// the coded DCC of the 2 bit DCC
MODEMAMPS_API uint8_t codedDcc(int dcc);
MODEMAMPS_API std::vector<uint8_t> reccMessage(int dcc, const std::vector<uint64_t>& words);

// Reverse voice channel (2.7.2, figure 2.7.2-1): each word repeated 5 times, each repeat after 37
// bits of dotting and the word sync, the first repeat of the first word after 101 bits.
constexpr int kRvcRepeats = 5;
MODEMAMPS_API std::vector<uint8_t> rvcMessage(const std::vector<uint64_t>& words);

} // namespace modemamps

#endif // MODEMAMPS_AMPSSTREAM_H_
