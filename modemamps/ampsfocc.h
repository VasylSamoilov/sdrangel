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

#ifndef MODEMAMPS_AMPSFOCC_H_
#define MODEMAMPS_AMPSFOCC_H_

#include <cstdint>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include "ampssysinfo.h"
#include "export.h"

// The forward control channel of a base station, as osmocom-analog's amps (frame.c
// amps_encode_frame_focc): a stream of frames of 463 bits (46.3 ms), each one word in both streams
// A and B. Every 17 frames (0.79 s) the overhead train, one word per frame; a mobile station
// control message (word 1 and word 2) three times; the control filler in between. The busy-idle
// bits are idle (the system information has BIS 0).
namespace modemamps
{

// a mobile station control message: word 1 and word 2 (3.7.1.1)
struct MODEMAMPS_API FoccMessage
{
    uint64_t word1 = 0;
    uint64_t word2 = 0;

    // a page or an order: word 2 with SCC 11 (page: ORDER, ORDQ and MSG_TYPE 0)
    static FoccMessage order(uint8_t dcc, uint32_t min1, uint16_t min2, uint8_t order, uint8_t ordq, uint8_t msgType);
    // the initial voice channel designation: word 2 with the SAT colour code of the voice channel
    static FoccMessage voiceChannel(uint8_t dcc, uint32_t min1, uint16_t min2, uint8_t scc, uint8_t vmac, uint16_t chan);
};

class MODEMAMPS_API FoccTransmitter
{
public:
    static constexpr int kOverheadRepeat = 17;     // frames from one overhead train to the next
    static constexpr int kMessageRepeats = 3;

    // the registration ID of a train: osmocom's time in seconds
    using RegistrationId = std::function<uint32_t()>;

    FoccTransmitter();

    void setSystemInfo(const SystemInfo& si) { m_si = si; }
    const SystemInfo& systemInfo() const { return m_si; }
    void setRegistrationId(RegistrationId regid) { m_regid = std::move(regid); }
    // Asked for the next message when none is being sent and none is queued, at a frame that is
    // not the overhead train (osmocom's amps_tx_frame_focc: the call processing hands over its
    // message, and a message it handed over before has then gone out completely). False: none.
    using MessageSource = std::function<bool(FoccMessage&)>;
    void setMessageSource(MessageSource source) { m_source = std::move(source); }
    // the message goes out after the ones queued, three times
    void queue(const FoccMessage& message) { m_queue.push_back(message); }
    size_t queued() const { return m_queue.size() + (m_sending ? 1 : 0); }

    // the word of the next frame
    uint64_t nextWord();
    // the bits of the next frame (both streams the word, the busy-idle bits idle)
    std::vector<uint8_t> nextFrame();
    // the next bit of the stream of frames (a bit source of the modulator)
    int nextBit();

private:
    SystemInfo m_si;
    RegistrationId m_regid;
    MessageSource m_source;
    std::deque<FoccMessage> m_queue;
    std::vector<uint64_t> m_train;      // the words of the train still to send
    size_t m_trainPos;
    int m_frameCount;
    bool m_sending;
    FoccMessage m_message;
    int m_wordCount;
    int m_repeat;
    std::vector<uint8_t> m_bits;        // the frame being sent
    size_t m_bitPos;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSFOCC_H_
