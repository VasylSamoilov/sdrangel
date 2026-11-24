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

#include "ampsframe.h"

#include "ampsbch.h"
#include "ampsstream.h"

namespace modemamps
{

namespace
{

const int kWordBits = kReverseWordBits;                     // 48
const int kRepeats = 5;
const int kBurstBits = kRepeats * kWordBits;                // 240
const int kDccBits = 7;
const uint32_t kSyncMask = (1u << kWordSyncBits) - 1;
const uint32_t kSyncNegative = ~kWordSync & kSyncMask;      // 0x0ed
// osmocom sets the register to this after a sync: no sync within the next bits
const uint32_t kSyncReset = 0x555;
// RVC: the next repeat comes after 37 bits of dotting and the sync; a little more is waited for
const int kRvcGapBits = 37 + kWordSyncBits + 4;

int bitErrors(uint32_t a, uint32_t b)
{
    uint32_t x = a ^ b;
    int n = 0;

    while (x != 0)
    {
        x &= x - 1;
        n++;
    }
    return n;
}

} // namespace

uint64_t ReverseBurst::word() const
{
    for (int i = 0; i < kRepeats; i++)
    {
        if (ok[i]) {
            return words[i];
        }
    }
    return 0;
}

ReverseReceiver::ReverseReceiver(double sampleRate, bool tolerant) :
    m_demodulator(sampleRate, kFskBitRate, kFskDeviation),
    m_tolerant(tolerant),
    m_channel(ChannelRecc),
    m_repeats(0),
    m_between(false),
    m_gap(0)
{
    m_demodulator.setBitSink([this](int bit, double quality, double level) { receiveBit(bit, quality, level); });
    reset();
}

void ReverseReceiver::setChannel(Channel channel)
{
    m_channel = channel;
    reset();
}

void ReverseReceiver::reset()
{
    m_sync = kSyncReset;
    m_inFrame = false;
    m_negative = false;
    m_first = true;
    m_length = (m_channel == ChannelRecc) ? kDccBits + kBurstBits : kBurstBits;
    m_count = 0;
    m_repeats = 0;
    m_between = false;
    m_gap = 0;
    m_level = 0.0;
    m_quality = 0.0;
    m_demodulator.endFrame();
}

int ReverseReceiver::decodeDcc(uint8_t coded)
{
    for (int dcc = 0; dcc < 4; dcc++)
    {
        if (bitErrors(coded, codedDcc(dcc)) <= 1) {
            return dcc;
        }
    }
    return -1;
}

void ReverseReceiver::receiveBit(int bit, double quality, double level)
{
    if (!m_inFrame)
    {
        m_sync = ((m_sync << 1) | static_cast<uint32_t>(bit)) & kSyncMask;

        const int positive = bitErrors(m_sync, kWordSync);
        const int negative = bitErrors(m_sync, kSyncNegative);
        const int allowed = m_tolerant ? 1 : 0;

        if (positive <= allowed || negative <= allowed)
        {
            m_negative = negative <= allowed;
            m_inFrame = true;
            m_first = true;
            m_length = (m_channel == ChannelRecc) ? kDccBits + kBurstBits : kBurstBits;
            m_count = 0;
            m_repeats = 0;
            m_between = false;
            m_level = 0.0;
            m_quality = 0.0;
            m_sync = kSyncReset;
            m_demodulator.startFrame();
        }
        return;
    }

    if (m_between)
    {
        // RVC: the dotting and the sync of the next repeat, the same way up as the first
        m_sync = ((m_sync << 1) | static_cast<uint32_t>(bit)) & kSyncMask;

        if (bitErrors(m_sync, m_negative ? kSyncNegative : kWordSync) <= (m_tolerant ? 1 : 0))
        {
            m_between = false;
            m_sync = kSyncReset;
        }
        else if (++m_gap > kRvcGapBits)
        {
            decode();                   // no more repeats: the ones received
        }
        return;
    }

    m_level += level;
    m_quality += quality;
    m_bits[m_count++] = static_cast<uint8_t>(m_negative ? 1 - bit : bit);
    if (m_channel == ChannelRecc)
    {
        if (m_count == m_length) {
            decode();
        }
    }
    else if (m_count % kWordBits == 0)
    {
        if (++m_repeats == kRepeats)
        {
            decode();
        }
        else
        {
            m_between = true;
            m_gap = 0;
            m_sync = kSyncReset;
        }
    }
}

void ReverseReceiver::decode()
{
    ReverseBurst burst;
    int pos = 0;
    const int repeats = (m_channel == ChannelRecc) ? kRepeats : m_repeats;

    burst.first = m_first;
    burst.negative = m_negative;
    burst.repeats = repeats;
    burst.level = (m_count > 0) ? m_level / m_count : 0.0;
    burst.quality = (m_level > 0.0) ? m_quality / m_level : 0.0;

    if (m_length == kDccBits + kBurstBits)
    {
        uint8_t coded = 0;

        for (; pos < kDccBits; pos++) {
            coded = static_cast<uint8_t>((coded << 1) | m_bits[pos]);
        }
        burst.dcc = decodeDcc(coded);
    }
    for (int i = 0; i < repeats; i++)
    {
        uint64_t word = 0;

        for (int j = 0; j < kWordBits; j++) {
            word = (word << 1) | m_bits[pos++];
        }
        burst.words[i] = word;
        burst.ok[i] = bchCheck(word, kReverseMessageBits);
        burst.okCount += burst.ok[i] ? 1 : 0;
    }

    const bool more = (repeats > 0 && m_handler) ? m_handler(burst) : false;

    m_count = 0;
    m_repeats = 0;
    m_level = 0.0;
    m_quality = 0.0;
    if (more)
    {
        // RECC: the next word follows at once, without a sync; RVC: after its dotting and sync
        m_first = false;
        m_length = kBurstBits;
        m_between = m_channel == ChannelRvc;
        m_gap = 0;
        m_sync = kSyncReset;
    }
    else
    {
        m_inFrame = false;
        m_between = false;
        m_length = (m_channel == ChannelRecc) ? kDccBits + kBurstBits : kBurstBits;
        m_demodulator.endFrame();
    }
}

} // namespace modemamps
