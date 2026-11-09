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

#include "radiopagingpocsagdecoder.h"

#include "radiopagingbch.h"
#include "radiopagingpocsag.h"

const int RadioPagingPocsagDecoder::m_minPreambleBits;
const int RadioPagingPocsagDecoder::m_minPreambleBitsCorrected;
const int RadioPagingPocsagDecoder::m_maxSyncErrors;
const int RadioPagingPocsagDecoder::m_maxMessageBits;

namespace {

int popcount32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0f0f0f0fu;
    return (int) ((x * 0x01010101u) >> 24);
}

} // namespace

RadioPagingPocsagDecoder::RadioPagingPocsagDecoder(Polarity polarity) :
    m_polarity(polarity)
{
    reset();
}

void RadioPagingPocsagDecoder::reset()
{
    m_state = Hunting;
    m_reg = 0;
    m_regBits = 0;
    m_word = 0;
    m_wordBits = 0;
    m_slot = 0;
    m_batch = 0;
    m_batches = 0;
    m_active = false;
    m_currentBits.clear();
    m_codewords = 0;
    m_codewordsCorrected = 0;
    m_codewordsUncorrectable = 0;
    m_transmissions = 0;
    m_messages.clear();
}

RadioPagingPocsagDecoder::Status RadioPagingPocsagDecoder::getStatus() const
{
    Status status;

    status.m_state = m_state;
    status.m_polarity = m_polarity;
    status.m_batches = m_batches;
    status.m_codewords = m_codewords;
    status.m_codewordsCorrected = m_codewordsCorrected;
    status.m_codewordsUncorrectable = m_codewordsUncorrectable;
    status.m_transmissions = m_transmissions;

    return status;
}

RadioPagingPocsagDecoder::Message RadioPagingPocsagDecoder::takeMessage()
{
    Message message = m_messages.front();
    m_messages.pop_front();
    return message;
}

// Number of alternating bits immediately before the last 32 bits of the shift register
int RadioPagingPocsagDecoder::alternatingRun() const
{
    if (m_regBits < 64) {
        return 0;
    }

    uint32_t before = (uint32_t) (m_reg >> 32);
    // Bit i of changes is set when bit i and bit i+1 differ
    uint32_t changes = before ^ (before >> 1);
    int run = 1;

    for (int i = 0; i < 31; i++)
    {
        if (changes & (1u << i)) {
            run++;
        } else {
            break;
        }
    }

    return run;
}

void RadioPagingPocsagDecoder::feed(int symbol)
{
    // Binary 1 is a negative deviation for normal polarity
    int bit = (m_polarity == Normal) ? (symbol ? 0 : 1) : (symbol ? 1 : 0);

    m_reg = (m_reg << 1) | (uint64_t) bit;
    if (m_regBits < 64) {
        m_regBits++;
    }

    switch (m_state)
    {
    case Hunting:
        if (m_regBits >= 32)
        {
            int errors = popcount32((uint32_t) m_reg ^ RadioPagingPocsag::m_syncWord);

            if (errors <= m_maxSyncErrors)
            {
                int needed = (errors == 0) ? m_minPreambleBits : m_minPreambleBitsCorrected;

                if (alternatingRun() >= needed)
                {
                    m_state = InBatch;
                    m_slot = 0;
                    m_wordBits = 0;
                    m_word = 0;
                    m_batch = 0;
                    m_batches = 1;
                    m_transmissions++;
                }
            }
        }
        break;

    case InBatch:
        m_word = (m_word << 1) | (uint32_t) bit;
        if (++m_wordBits == 32)
        {
            processCodeword(m_word);
            m_wordBits = 0;
            m_word = 0;
            if (++m_slot == RadioPagingPocsag::m_codewordsPerBatch) {
                m_state = AwaitSync;
            }
        }
        break;

    case AwaitSync:
        m_word = (m_word << 1) | (uint32_t) bit;
        if (++m_wordBits == 32)
        {
            int errors = popcount32(m_word ^ RadioPagingPocsag::m_syncWord);

            m_wordBits = 0;
            m_word = 0;

            if (errors <= m_maxSyncErrors)
            {
                m_state = InBatch;
                m_slot = 0;
                m_batch++;
                m_batches++;
            }
            else
            {
                finishMessage(true);
                m_state = Hunting;
                m_batches = 0;
            }
        }
        break;
    }
}

void RadioPagingPocsagDecoder::processCodeword(uint32_t word)
{
    RadioPagingBCH::Result result = RadioPagingBCH::decode(word);
    m_codewords++;

    if (result.m_bitErrors < 0)
    {
        // Uncorrectable. Bit 31 decides what it was supposed to be, as far as we can tell
        m_codewordsUncorrectable++;
        if (word & 0x80000000u) {
            appendMessageBits(RadioPagingBCH::data(word), false, true);
        } else {
            finishMessage(true);
        }
        return;
    }

    bool corrected = (result.m_bitErrors > 0) || result.m_parityError;
    if (corrected) {
        m_codewordsCorrected++;
    }

    uint32_t corrected_word = result.m_word;

    if (corrected_word == RadioPagingPocsag::m_idleWord)
    {
        finishMessage(false);
    }
    else if ((corrected_word & 0x80000000u) == 0)
    {
        // Address
        finishMessage(false);

        uint32_t data = RadioPagingBCH::data(corrected_word);

        m_active = true;
        m_current = Message();
        m_current.m_address = (((data >> 2) & 0x3ffff) << 3) | (uint32_t) (m_slot >> 1);
        m_current.m_function = data & 3;
        m_current.m_batch = m_batch;
        m_current.m_polarity = m_polarity;
        m_current.m_errorsCorrected = corrected ? (result.m_bitErrors > 0 ? result.m_bitErrors : 1) : 0;
        m_currentBits.clear();
    }
    else
    {
        appendMessageBits(RadioPagingBCH::data(corrected_word), corrected, false);
        if (corrected && m_active) {
            m_current.m_errorsCorrected += (result.m_bitErrors > 0) ? result.m_bitErrors : 1;
        }
    }
}

void RadioPagingPocsagDecoder::appendMessageBits(uint32_t data, bool corrected, bool uncorrectable)
{
    (void) corrected;

    if (!m_active) {
        return;
    }

    if (uncorrectable) {
        m_current.m_errorsUncorrectable++;
    }

    if ((int) m_currentBits.size() >= m_maxMessageBits) {
        return;
    }

    for (int i = 19; i >= 0; i--) {
        m_currentBits.push_back((data >> i) & 1);
    }
}

void RadioPagingPocsagDecoder::finishMessage(bool truncated)
{
    if (!m_active) {
        return;
    }

    m_active = false;
    m_current.m_truncated = truncated;
    m_current.m_rawBits = (int) m_currentBits.size();
    m_current.m_rawData.assign((m_currentBits.size() + 7) / 8, 0);

    for (size_t i = 0; i < m_currentBits.size(); i++)
    {
        if (m_currentBits[i]) {
            m_current.m_rawData[i >> 3] |= (uint8_t) (0x80 >> (i & 7));
        }
    }

    m_messages.push_back(m_current);
    m_currentBits.clear();
}
