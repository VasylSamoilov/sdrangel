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

#include <algorithm>
#include <cmath>

#include "radiopagingbch.h"
#include "radiopagingpocsag.h"

const int RadioPagingPocsagDecoder::m_maxSyncErrors;
const int RadioPagingPocsagDecoder::m_maxMessageBits;
const int RadioPagingPocsagDecoder::m_minExactCodewords;

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
    m_polarity(polarity),
    m_batchWords{},
    m_batchQuality{},
    m_wordError2(0.0f),
    m_wordQualityKnown(false),
    m_wordNoiseQuality(1e9f),
    m_preambleBefore(false)
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
    m_wordError2 = 0.0f;
    m_wordQualityKnown = false;
    m_preambleBefore = false;
    m_leadingConfirmed = false;
    m_batch = 0;
    m_active = false;
    m_currentBits.clear();
    m_uncorrectableRun = 0;
    m_codewords = 0;
    m_codewordsCorrected = 0;
    m_codewordsUncorrectable = 0;
    m_transmissions = 0;
    m_candidatesDropped = 0;
    m_syncMissed = 0;
    m_syncMissedLast = false;
    m_ber.reset();
    m_messages.clear();
}

RadioPagingPocsagDecoder::Status RadioPagingPocsagDecoder::getStatus() const
{
    Status status;

    status.m_state = m_state;
    status.m_polarity = m_polarity;
    status.m_confirmed = m_leadingConfirmed;
    status.m_batches = m_batch;
    status.m_codewords = m_codewords;
    status.m_codewordsCorrected = m_codewordsCorrected;
    status.m_codewordsUncorrectable = m_codewordsUncorrectable;
    status.m_transmissions = m_transmissions;
    status.m_candidatesDropped = m_candidatesDropped;
    status.m_syncMissed = m_syncMissed;
    status.m_ber = m_ber;

    return status;
}

RadioPagingPocsagDecoder::Message RadioPagingPocsagDecoder::takeMessage()
{
    Message message = m_messages.front();
    m_messages.pop_front();
    return message;
}

bool RadioPagingPocsagDecoder::syncMatch() const
{
    return (m_regBits >= 32) && (popcount32(m_reg ^ RadioPagingPocsag::m_syncWord) <= m_maxSyncErrors);
}

void RadioPagingPocsagDecoder::startCandidate()
{
    m_syncWords++;
    m_state = InBatch;
    m_slot = 0;
    m_word = 0;
    m_wordBits = 0;
    m_wordError2 = 0.0f;
    m_preambleBefore = false;
    m_leadingConfirmed = false;
    m_syncMissedLast = false;
    m_batch = 0;
}

// A codeword of the batch is complete: kept with its quality
void RadioPagingPocsagDecoder::storeWord(int slot, uint32_t word)
{
    m_batchWords[slot] = word;
    m_batchQuality[slot] = m_wordQualityKnown ? std::sqrt(m_wordError2 / 32.0f) : -1.0f;
    m_wordError2 = 0.0f;
}

void RadioPagingPocsagDecoder::feed(int symbol, float error)
{
    m_wordQualityKnown = true;
    m_wordError2 += error * error;
    feed(symbol);
}

void RadioPagingPocsagDecoder::feed(int symbol)
{
    // Binary 1 is a negative deviation for normal polarity
    int bit = (m_polarity == Normal) ? (symbol ? 0 : 1) : (symbol ? 1 : 0);

    m_reg = (m_reg << 1) | (uint32_t) bit;
    if (m_regBits < 32) {
        m_regBits++;
    }

    switch (m_state)
    {
    case Hunting:
        if (syncMatch()) {
            startCandidate();
        }
        break;

    case InBatch:
        // An unconfirmed candidate gives way to a sync codeword at another position
        if (!m_leadingConfirmed && syncMatch())
        {
            m_candidatesDropped++;
            startCandidate();
            break;
        }

        m_word = (m_word << 1) | (uint32_t) bit;
        if (++m_wordBits == 32)
        {
            storeWord(m_slot, m_word);
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
            bool sync = popcount32(m_word ^ RadioPagingPocsag::m_syncWord) <= m_maxSyncErrors;

            m_wordBits = 0;
            m_word = 0;
            m_wordError2 = 0.0f;

            if (sync)
            {
                // The batch lies between two sync codewords one batch apart
                m_syncWords++;
                decodeBatch();
                m_leadingConfirmed = true;
                m_syncMissedLast = false;
                m_slot = 0;
                m_state = InBatch;
            }
            else if (!m_leadingConfirmed && (m_batch == 0) && !m_preambleBefore && !batchValid() && (exactCodewords() < m_minExactCodewords))
            {
                // A sync codeword found alone (no preamble before it, none after it) whose batch has few codewords
                // received exactly: a sync codeword pattern in noise or in another signal
                m_candidatesDropped++;
                endTransmission();
            }
            else if (!m_syncMissedLast)
            {
                // The following sync codeword was missed: the batch is decoded up to the end of the signal in it, which
                // ends the transmission. Otherwise (a fade over the sync codeword) reception goes on at the expected
                // position, as a pager decoder does. Two batches in a row without their sync codeword end the
                // transmission (sync loss)
                if (decodeUnconfirmedBatch())
                {
                    endTransmission();
                }
                else
                {
                    m_syncMissed++;
                    m_syncMissedLast = true;
                    m_leadingConfirmed = false;
                    m_slot = 0;
                    m_state = InBatch;
                }
            }
            else
            {
                decodeUnconfirmedBatch();
                endTransmission();
            }
        }
        break;
    }
}

bool RadioPagingPocsagDecoder::batchValid() const
{
    for (uint32_t word : m_batchWords)
    {
        if (RadioPagingBCH::decode(word).m_bitErrors < 0) {
            return false;
        }
    }

    return true;
}

void RadioPagingPocsagDecoder::decodeBatch()
{
    if (m_batch == 0)
    {
        m_transmissions++;
        m_ber.reset();
    }

    for (m_slot = 0; m_slot < RadioPagingPocsag::m_codewordsPerBatch; m_slot++) {
        processCodeword(m_batchWords[m_slot]);
    }

    m_batch++;
}

// A codeword of a batch no sync codeword follows is decipherable when it decodes and its symbols are better than
// noise gives (noise passes the code a quarter of the time)
bool RadioPagingPocsagDecoder::decipherable(int slot) const
{
    return (RadioPagingBCH::decode(m_batchWords[slot]).m_bitErrors >= 0) && (m_batchQuality[slot] < m_wordNoiseQuality);
}

int RadioPagingPocsagDecoder::exactCodewords() const
{
    int exact = 0;

    for (uint32_t word : m_batchWords)
    {
        RadioPagingBCH::Result result = RadioPagingBCH::decode(word);
        exact += ((result.m_bitErrors == 0) && !result.m_parityError) ? 1 : 0;
    }

    return exact;
}

// Decodes the first received codewords of the batch up to the end of the signal in it: the last decipherable codeword
// (noise has none, so the signal is there up to it; indecipherable codewords before it are a burst of errors, lost
// codewords of the message, never addresses). True if the batch is cut there, or not all its codewords were received:
// the message in progress ends truncated and the transmission is over
bool RadioPagingPocsagDecoder::decodeUnconfirmedBatch(int received)
{
    int cut = 0;

    for (int slot = 0; slot < received; slot++)
    {
        if (decipherable(slot)) {
            cut = slot + 1;
        }
    }

    // The codeword the signal was lost in is partly the transmission's, partly noise or another signal: the code
    // corrects it to some other codeword. Codewords before the cut that needed a correction are not taken
    while ((cut < received) && (cut > 0))
    {
        RadioPagingBCH::Result result = RadioPagingBCH::decode(m_batchWords[cut - 1]);

        if ((result.m_bitErrors == 0) && !result.m_parityError) {
            break;
        }

        cut--;
    }

    if (cut == 0)
    {
        if (m_batch == 0) {
            m_candidatesDropped++;
        }
        finishMessage(true);
        return true;
    }

    if (m_batch == 0)
    {
        m_transmissions++;
        m_ber.reset();
    }

    for (m_slot = 0; m_slot < cut; m_slot++) {
        processCodeword(m_batchWords[m_slot], !decipherable(m_slot));
    }

    m_batch++;

    if (cut < RadioPagingPocsag::m_codewordsPerBatch)
    {
        finishMessage(true);
        return true;
    }

    return false;
}

void RadioPagingPocsagDecoder::flush()
{
    // The batch received so far, of a transmission (its sync codeword confirmed, an earlier batch decoded, or after
    // a preamble): a candidate found in noise is not decoded
    if ((m_state != Hunting) && (m_leadingConfirmed || (m_batch > 0) || m_preambleBefore))
    {
        int received = (m_state == AwaitSync) ? RadioPagingPocsag::m_codewordsPerBatch : m_slot;

        if (received > 0) {
            decodeUnconfirmedBatch(received);
        }
    }

    endTransmission();
    m_word = 0;
    m_wordBits = 0;
    m_wordError2 = 0.0f;
    m_preambleBefore = false;
}

void RadioPagingPocsagDecoder::endTransmission()
{
    finishMessage(true);
    m_state = Hunting;
    m_leadingConfirmed = false;
    m_syncMissedLast = false;
    m_batch = 0;
    m_uncorrectableRun = 0;
}

// lost: the codeword is taken as undecodable (a codeword of a batch no sync codeword follows that needs two corrected
// bits)
void RadioPagingPocsagDecoder::processCodeword(uint32_t word, bool lost)
{
    RadioPagingBCH::Result result = RadioPagingBCH::decode(word);
    m_codewords++;
    m_ber.add((result.m_bitErrors > 0 ? result.m_bitErrors : 0) + (result.m_parityError ? 1 : 0), 32, result.m_bitErrors < 0);

    if ((result.m_bitErrors < 0) || lost)
    {
        m_codewordsUncorrectable++;
        m_uncorrectableRun++;

        if (m_active)
        {
            m_current.m_errorsUncorrectable++;

            if (m_uncorrectableRun >= 2) {
                finishMessage(true);
            } else {
                appendMessageBits(RadioPagingBCH::data(word) & 0xfffff, 'X');
            }
        }
        return;
    }

    m_uncorrectableRun = 0;

    int corrections = result.m_bitErrors + (result.m_parityError ? 1 : 0);
    if (corrections > 0) {
        m_codewordsCorrected++;
    }

    uint32_t corrected = result.m_word;

    if (corrected == RadioPagingPocsag::m_idleWord)
    {
        finishMessage(false);
    }
    else if ((corrected & 0x80000000u) == 0)
    {
        // Address
        finishMessage(false);

        uint32_t data = RadioPagingBCH::data(corrected);

        m_active = true;
        m_current = Message();
        m_current.m_address = (((data >> 2) & 0x3ffff) << 3) | (uint32_t) (m_slot >> 1);
        m_current.m_function = data & 3;
        m_current.m_batch = m_batch;
        m_current.m_polarity = m_polarity;
        m_current.m_errorsCorrected = corrections;
        m_currentBits.clear();
    }
    else if (m_active)
    {
        appendMessageBits(RadioPagingBCH::data(corrected) & 0xfffff, (corrections > 0) ? 'c' : '.');
        m_current.m_errorsCorrected += corrections;
    }
}

void RadioPagingPocsagDecoder::appendMessageBits(uint32_t data, char status)
{
    if ((int) m_currentBits.size() >= m_maxMessageBits) {
        return;
    }

    for (int i = 19; i >= 0; i--) {
        m_currentBits.push_back((data >> i) & 1);
    }

    m_current.m_wordStatus += status;
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
