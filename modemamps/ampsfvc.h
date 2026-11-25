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

#ifndef MODEMAMPS_AMPSFVC_H_
#define MODEMAMPS_AMPSFVC_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "export.h"

// The forward voice channel orders of a base station, as osmocom-analog's amps (frame.c
// amps_encode_frame_fvc): a mobile station control message is one word (T1T2 10), sent as 11
// repeats with their dotting and sync (1032 bits, 103.2 ms) in place of the audio (blank and
// burst). With nothing queued the stream ends and the audio goes on.
namespace modemamps
{

// the word of a mobile station control message on the FVC (3.7.2.1)
namespace fvc
{
// an order (SCC 11); pscc: the SAT colour code of this voice channel
MODEMAMPS_API uint64_t order(uint8_t pscc, uint8_t order, uint8_t ordq, uint8_t msgType);
// a handoff to another voice channel (SCC its SAT colour code)
MODEMAMPS_API uint64_t handoff(uint8_t scc, uint8_t pscc, uint8_t dtx, uint8_t vmac, uint16_t chan);
// The alert with info: the caller's number for the phone's display, as osmocom-analog sends it
// (frame.c amps_encode_frame_fvc, ORDER 17 ORDQ 0): word 1 the order, word 2 the length (RL_W words of
// characters, CPN_RL characters), SIGNAL 1 (medium pitch, long cadence), PI 0 (presentation allowed;
// 2 number not available without one), SI 3 (network provided), then 3 characters a word.
MODEMAMPS_API std::vector<uint64_t> alertWithInfo(uint8_t pscc, const std::string& callerId);
// The flash with info of a call: a text for the phone's display (osmocom amps_flash_with_info,
// ORDER 18 ORDQ 0: the caller in the call, a call waiting notice): word 2 the length (no SIGNAL), PI
// 0, SI 3, then the characters as the alert with info; at most kFlashInfoMax (osmocom's buffer).
constexpr size_t kFlashInfoMax = 33;
MODEMAMPS_API std::vector<uint64_t> flashWithInfo(uint8_t pscc, const std::string& text);
// The words of characters: 3 a word (T1T2 01), ASCII with the top bit 0, nulls filling the last
MODEMAMPS_API void characterWords(const std::string& text, std::vector<uint64_t>& out);
} // namespace fvc

class MODEMAMPS_API FvcTransmitter
{
public:
    FvcTransmitter();

    void queue(uint64_t word) { m_queue.push_back(word); }
    // Asked for the next word when the last one has gone out and none is queued (osmocom's
    // amps_tx_frame_fvc). False: none, the stream ends.
    using WordSource = std::function<bool(uint64_t&)>;
    void setWordSource(WordSource source) { m_source = std::move(source); }
    // a word queued or being sent
    bool busy() const { return !m_queue.empty() || m_bitPos < m_bits.size(); }
    // the next bit (a bit source of the modulator), -1 when nothing is left: back to audio
    int nextBit();
    void reset();

private:
    WordSource m_source;
    std::deque<uint64_t> m_queue;
    std::vector<uint8_t> m_bits;
    size_t m_bitPos;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSFVC_H_
