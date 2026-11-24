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

#include "ampsfocc.h"

#include "ampsmessages.h"
#include "ampsstream.h"

namespace modemamps
{

FoccMessage FoccMessage::order(uint8_t dcc, uint32_t min1, uint16_t min2, uint8_t order, uint8_t ordq, uint8_t msgType)
{
    FoccMessage m;

    m.word1 = encodeWord(words::kAbbreviatedAddress, {{"T1T2", kT1T2First}, {"DCC", dcc}, {"MIN1", min1}});
    m.word2 = encodeWord(words::kExtendedAddressOrder, {{"T1T2", kT1T2Next}, {"SCC", 3}, {"MIN2", min2}, {"EF", 0},
        {"LOCAL/MSG_TYPE", msgType}, {"ORDQ", ordq}, {"ORDER", order}});
    return m;
}

FoccMessage FoccMessage::voiceChannel(uint8_t dcc, uint32_t min1, uint16_t min2, uint8_t scc, uint8_t vmac, uint16_t chan)
{
    FoccMessage m;

    m.word1 = encodeWord(words::kAbbreviatedAddress, {{"T1T2", kT1T2First}, {"DCC", dcc}, {"MIN1", min1}});
    m.word2 = encodeWord(words::kExtendedAddressChannel, {{"T1T2", kT1T2Next}, {"SCC", scc}, {"MIN2", min2},
        {"VMAC", vmac}, {"CHAN", chan}});
    return m;
}

FoccTransmitter::FoccTransmitter() :
    m_trainPos(0),
    m_frameCount(0),
    m_sending(false),
    m_wordCount(0),
    m_repeat(0),
    m_bitPos(0)
{
}

uint64_t FoccTransmitter::nextWord()
{
    // the overhead train every kOverheadRepeat frames, one word per frame
    if (m_frameCount == 0 && m_trainPos >= m_train.size())
    {
        m_train = m_si.overheadTrain(m_regid ? m_regid() : 0);
        m_trainPos = 0;
    }
    if (m_trainPos < m_train.size())
    {
        const uint64_t word = m_train[m_trainPos++];
        if (++m_frameCount >= kOverheadRepeat) {
            m_frameCount = 0;
        }
        return word;
    }

    if (!m_sending && !m_queue.empty())
    {
        m_message = m_queue.front();
        m_queue.pop_front();
        m_sending = true;
        m_wordCount = 0;
        m_repeat = 0;
    }
    if (m_sending)
    {
        const uint64_t word = (m_wordCount == 0) ? m_message.word1 : m_message.word2;

        // the frame count wraps only when the message is done (osmocom)
        ++m_frameCount;
        if (++m_wordCount == 2)
        {
            m_wordCount = 0;
            if (++m_repeat == kMessageRepeats)
            {
                m_sending = false;
                if (m_frameCount >= kOverheadRepeat) {
                    m_frameCount = 0;
                }
            }
        }
        return word;
    }

    if (++m_frameCount >= kOverheadRepeat) {
        m_frameCount = 0;
    }
    return m_si.controlFiller();
}

std::vector<uint8_t> FoccTransmitter::nextFrame()
{
    const uint64_t word = nextWord();
    return foccTrain(word, word, false);
}

int FoccTransmitter::nextBit()
{
    if (m_bitPos >= m_bits.size())
    {
        m_bits = nextFrame();
        m_bitPos = 0;
    }
    return m_bits[m_bitPos++];
}

} // namespace modemamps
