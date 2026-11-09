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

#include "radiopaginggscdecoder.h"

#include <algorithm>

const int RadioPagingGscDecoder::m_minNonBatterySaverBits;
const int RadioPagingGscDecoder::m_maxStartSearchBits;
const int RadioPagingGscDecoder::m_syncHits;

namespace {

const int codewordBits = 46;
const int pairBits = 121;           // Comma, word 1, separator bit, word 2
const int peekBits = 74;            // Comma and word 1
const int hitRing = 2 * codewordBits + 1;
const int maxTransitionsForData = 108;

bool alternates(const uint8_t *bits, int n)
{
    for (int i = 1; i < n; i++)
    {
        if (bits[i] == bits[i - 1]) {
            return false;
        }
    }

    return true;
}

bool isWord1(uint16_t data)
{
    return (RadioPagingGsc::word1Index(data) >= 0) || (RadioPagingGsc::word1Index(data ^ 0xfff) >= 0);
}

} // namespace

RadioPagingGscDecoder::RadioPagingGscDecoder(Polarity polarity) :
    m_polarity(polarity),
    m_nonBatterySaver(false)
{
    reset();
}

void RadioPagingGscDecoder::reset()
{
    m_state = Hunting;
    m_parse = SeekStart;
    m_reg = 0;
    m_regBits = 0;
    std::fill(m_hits, m_hits + hitRing, (int8_t) -1);
    m_hitPos = 0;
    m_patternRun = 0;
    m_buf.clear();
    m_pos = 0;
    m_scan = 0;
    m_preamble = -1;
    m_batch = false;
    m_nbs = false;
    m_slot = 0;
    m_active = false;
    m_current = Message();
    m_currentBits.clear();
    m_blocks = 0;
    m_codewords = 0;
    m_codewordsCorrected = 0;
    m_codewordsUncorrectable = 0;
    m_transmissions = 0;
    m_messageCount = 0;
    m_messages.clear();
}

RadioPagingGscDecoder::Status RadioPagingGscDecoder::getStatus() const
{
    Status status;

    status.m_state = m_state;
    status.m_polarity = m_polarity;
    status.m_codewords = m_codewords;
    status.m_codewordsCorrected = m_codewordsCorrected;
    status.m_codewordsUncorrectable = m_codewordsUncorrectable;
    status.m_transmissions = m_transmissions;
    status.m_messages = m_messageCount;

    return status;
}

RadioPagingGscDecoder::Message RadioPagingGscDecoder::takeMessage()
{
    Message message = m_messages.front();
    m_messages.pop_front();
    return message;
}

// Preamble hit for the last 46 bits: index * 2, plus 1 if complemented. -1 if none
int RadioPagingGscDecoder::preambleHit() const
{
    uint32_t word = 0;

    for (int i = 0; i < 23; i++) {
        word |= (uint32_t) ((m_reg >> (45 - 2 * i)) & 1) << i;
    }

    RadioPagingGsc::GolayResult result = RadioPagingGsc::golayDecode(word);
    int index = RadioPagingGsc::preambleIndex(result.m_data);

    if (index >= 0) {
        return index * 2;
    }

    index = RadioPagingGsc::preambleIndex(result.m_data ^ 0xfff);

    return (index >= 0) ? (index * 2 + 1) : -1;
}

void RadioPagingGscDecoder::feed(int symbol)
{
    int bit = (m_polarity == Normal) ? (symbol ? 1 : 0) : (symbol ? 0 : 1);

    m_reg = (m_reg << 1) | (uint64_t) bit;
    if (m_regBits < 64) {
        m_regBits++;
    }

    // A new preamble is only looked for when no start code search is going on
    bool enabled = (m_state == Hunting) || (m_state == InTransmission);
    int hit = (m_regBits >= codewordBits) ? preambleHit() : -1;

    m_hitPos = (m_hitPos + 1) % hitRing;
    m_hits[m_hitPos] = (int8_t) hit;

    if (enabled && (hit >= 0))
    {
        int p1 = (m_hitPos + hitRing - codewordBits) % hitRing;
        int p2 = (m_hitPos + hitRing - 2 * codewordBits) % hitRing;

        if ((m_hits[p1] == hit) && (m_hits[p2] == hit))
        {
            if (m_state == InTransmission) {
                endTransmission(true);
            }
            startTransmission(hit >> 1, (hit & 1) != 0);
            return;
        }
    }

    if (m_regBits >= 5)
    {
        int b0 = (int) (m_reg & 1);
        int b2 = (int) ((m_reg >> 2) & 1);
        int b4 = (int) ((m_reg >> 4) & 1);

        if ((b0 == b4) && (b0 != b2))
        {
            m_patternRun++;
        }
        else
        {
            int run = m_patternRun;
            m_patternRun = 0;

            if (m_nonBatterySaver && enabled && (run >= m_minNonBatterySaverBits))
            {
                if (m_state == InTransmission) {
                    endTransmission(true);
                }
                startNonBatterySaver();
                process();
                return;
            }
        }
    }

    if (m_state != Hunting)
    {
        m_buf.push_back((uint8_t) bit);
        process();
    }
}

void RadioPagingGscDecoder::flush()
{
    if (m_state == Hunting) {
        return;
    }

    endTransmission(true);
}

void RadioPagingGscDecoder::startTransmission(int index, bool batch)
{
    std::fill(m_hits, m_hits + hitRing, (int8_t) -1);
    m_patternRun = 0;
    m_state = AwaitStart;
    m_parse = SeekStart;
    m_buf.clear();
    m_pos = 0;
    m_scan = 0;
    m_preamble = index;
    m_batch = batch;
    m_nbs = false;
    m_slot = 0;
    m_active = false;
    m_transmissions++;
}

void RadioPagingGscDecoder::startNonBatterySaver()
{
    std::fill(m_hits, m_hits + hitRing, (int8_t) -1);
    m_patternRun = 0;
    m_state = AwaitStart;
    m_parse = SeekAddress;
    m_buf.clear();

    // The pattern may have run on into the comma by a few bits
    for (int i = 7; i >= 0; i--) {
        m_buf.push_back((uint8_t) ((m_reg >> i) & 1));
    }

    m_pos = 0;
    m_scan = 0;
    m_preamble = -1;
    m_batch = false;
    m_nbs = true;
    m_slot = 0;
    m_active = false;
    m_transmissions++;
}

// Finishes the message in progress, if any, and returns to hunting.
// A message is truncated if the transmission ends before the message does
void RadioPagingGscDecoder::endTransmission(bool truncated)
{
    if (m_active) {
        finishMessage(truncated);
    }

    m_state = Hunting;
    m_buf.clear();
    m_pos = 0;
    m_scan = 0;
}

void RadioPagingGscDecoder::finishMessage(bool truncated)
{
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
    m_messageCount++;
    m_currentBits.clear();
    m_active = false;
}

void RadioPagingGscDecoder::process()
{
    bool again = true;

    while (again && (m_state != Hunting))
    {
        switch (m_parse)
        {
        case SeekStart:
            again = processSeekStart();
            break;
        case SeekAddress:
            again = processSeekAddress();
            break;
        case Next:
            again = processNext();
            break;
        case Classify:
            again = processClassify();
            break;
        default:
            again = processData();
            break;
        }
    }

    trim();
}

void RadioPagingGscDecoder::trim()
{
    if ((m_state == InTransmission) && (m_pos >= 2048))
    {
        int n = std::min(m_pos, (int) m_buf.size());

        m_buf.erase(m_buf.begin(), m_buf.begin() + n);
        m_pos -= n;
    }
}

// The start code and, 47 bits later, its complement, at any bit offset
bool RadioPagingGscDecoder::processSeekStart()
{
    while (m_scan + 2 * codewordBits + 1 <= (int) m_buf.size())
    {
        RadioPagingGsc::GolayResult a = RadioPagingGsc::golayDecode(RadioPagingGsc::resolveDuplicated(&m_buf[m_scan]));

        if (a.m_data == RadioPagingGsc::m_startCode)
        {
            RadioPagingGsc::GolayResult b = RadioPagingGsc::golayDecode(RadioPagingGsc::resolveDuplicated(&m_buf[m_scan + codewordBits + 1]));

            if (b.m_data == (RadioPagingGsc::m_startCode ^ 0xfff))
            {
                m_pos = m_scan + 2 * codewordBits + 1;
                m_state = InTransmission;
                m_parse = Next;
                return true;
            }
        }

        if (++m_scan > m_maxStartSearchBits)
        {
            endTransmission(false);
            return false;
        }
    }

    return false;
}

// First address after the pattern, at the bit offset where an alternating comma and a word 1 are
bool RadioPagingGscDecoder::processSeekAddress()
{
    while (m_scan + pairBits <= (int) m_buf.size())
    {
        int o = m_scan;

        if (alternates(&m_buf[o], RadioPagingGsc::m_commaBits))
        {
            RadioPagingGsc::GolayResult r = RadioPagingGsc::golayDecode(RadioPagingGsc::resolveDuplicated(&m_buf[o + RadioPagingGsc::m_commaBits]));
            Message message;
            int e1, e2;

            if (isWord1(r.m_data) && parseAddress(o, message, e1, e2))
            {
                m_pos = o;
                m_state = InTransmission;
                m_parse = Next;
                return true;
            }
        }

        if (++m_scan > 48)
        {
            endTransmission(false);
            return false;
        }
    }

    return false;
}

bool RadioPagingGscDecoder::parseAddress(int pos, Message& message, int& errors1, int& errors2) const
{
    const uint8_t *p = &m_buf[pos];
    RadioPagingGsc::GolayResult r1 = RadioPagingGsc::golayDecode(RadioPagingGsc::resolveDuplicated(p + RadioPagingGsc::m_commaBits));
    uint16_t word1;
    bool inverted1 = false;
    bool inverted2 = false;

    if (RadioPagingGsc::word1Index(r1.m_data) >= 0)
    {
        word1 = r1.m_data;
    }
    else if (RadioPagingGsc::word1Index(r1.m_data ^ 0xfff) >= 0)
    {
        word1 = r1.m_data ^ 0xfff;
        inverted1 = true;
    }
    else
    {
        return false;
    }

    RadioPagingGsc::GolayResult r2 = RadioPagingGsc::golayDecode(RadioPagingGsc::resolveDuplicated(p + RadioPagingGsc::m_commaBits + codewordBits + 1));
    std::string address;

    if (!RadioPagingGsc::decodeAddress(word1, r2.m_data, m_preamble, address))
    {
        if (!RadioPagingGsc::decodeAddress(word1, r2.m_data ^ 0xfff, m_preamble, address)) {
            return false;
        }
        inverted2 = true;
    }

    message = Message();
    message.m_address = address;
    message.m_addressNumber = 1 + (inverted1 ? 2 : 0) + (inverted2 ? 1 : 0);
    message.m_preamble = m_preamble;
    message.m_slot = m_slot;
    message.m_batch = m_batch;
    message.m_nonBatterySaver = m_nbs;
    message.m_polarity = m_polarity;
    errors1 = r1.m_bitErrors;
    errors2 = r2.m_bitErrors;

    return true;
}

// After a message: another address, a repeated start code, or the end of the transmission
bool RadioPagingGscDecoder::processNext()
{
    if ((int) m_buf.size() < m_pos + peekBits) {
        return false;
    }

    RadioPagingGsc::GolayResult r = RadioPagingGsc::golayDecode(RadioPagingGsc::resolveDuplicated(&m_buf[m_pos + RadioPagingGsc::m_commaBits]));

    if (isWord1(r.m_data))
    {
        if ((int) m_buf.size() < m_pos + pairBits) {
            return false;
        }

        Message message;
        int e1, e2;

        if (!parseAddress(m_pos, message, e1, e2))
        {
            endTransmission(false);
            return false;
        }

        m_current = message;
        m_current.m_errorsCorrected = e1 + e2;
        m_currentBits.clear();
        m_active = true;
        m_slot++;
        m_codewords += 2;
        m_codewordsCorrected += (e1 > 0 ? 1 : 0) + (e2 > 0 ? 1 : 0);
        m_pos += pairBits;
        m_parse = Classify;
        return true;
    }

    if (r.m_data == RadioPagingGsc::m_startCode)
    {
        if ((int) m_buf.size() < m_pos + pairBits) {
            return false;
        }

        m_pos += pairBits;
        return true;
    }

    endTransmission(false);
    return false;
}

// What follows an address: the activation code is voice, a valid block is data, an
// alternating comma is a tone only message. Anything else is a damaged block
bool RadioPagingGscDecoder::processClassify()
{
    if ((int) m_buf.size() < m_pos + RadioPagingGsc::m_blockBits) {
        return false;
    }

    const uint8_t *p = &m_buf[m_pos];

    RadioPagingGsc::GolayResult r = RadioPagingGsc::golayDecode(RadioPagingGsc::resolveDuplicated(p + RadioPagingGsc::m_commaBits));

    if (r.m_data == RadioPagingGsc::m_activationCode)
    {
        m_current.m_type = RadioPagingGsc::Voice;
        finishMessage(false);
        endTransmission(false);
        return false;
    }

    uint16_t codewords[8];
    uint8_t words[8];
    bool data = true;

    RadioPagingGsc::deinterleaveBlock(p + 1, codewords);

    for (int k = 0; k < 8; k++)
    {
        RadioPagingGsc::BchResult b = RadioPagingGsc::bchDecode(codewords[k]);

        if (b.m_bitErrors < 0)
        {
            data = false;
            break;
        }
        words[k] = b.m_data;
    }

    if (data)
    {
        uint32_t sum = 0;

        for (int k = 0; k < 7; k++) {
            sum += RadioPagingGsc::bchEncode(words[k]);
        }
        data = (sum & 0x7f) == words[7];
    }

    if (!data)
    {
        int transitions = 0;

        for (int i = 1; i < RadioPagingGsc::m_blockBits; i++)
        {
            if (p[i] != p[i - 1]) {
                transitions++;
            }
        }
        data = transitions < maxTransitionsForData;
    }

    if (data)
    {
        m_current.m_type = RadioPagingGsc::Data;
        m_blocks = 0;
        m_parse = Data;
    }
    else
    {
        m_current.m_type = RadioPagingGsc::Tone;
        finishMessage(false);
        m_pos += RadioPagingGsc::m_toneCommaBits;
        m_parse = Next;
    }

    return true;
}

bool RadioPagingGscDecoder::processData()
{
    if ((int) m_buf.size() < m_pos + RadioPagingGsc::m_blockBits) {
        return false;
    }

    uint16_t codewords[8];
    uint8_t words[8];
    bool bad = false;

    RadioPagingGsc::deinterleaveBlock(&m_buf[m_pos + 1], codewords);

    for (int k = 0; k < 8; k++)
    {
        RadioPagingGsc::BchResult b = RadioPagingGsc::bchDecode(codewords[k]);

        m_codewords++;
        if (b.m_bitErrors < 0)
        {
            words[k] = 0;
            bad = true;
            m_current.m_errorsUncorrectable++;
            m_codewordsUncorrectable++;
        }
        else
        {
            words[k] = b.m_data;
            if (b.m_bitErrors > 0)
            {
                m_current.m_errorsCorrected += b.m_bitErrors;
                m_codewordsCorrected++;
            }
        }
    }

    if (!bad)
    {
        uint32_t sum = 0;

        for (int k = 0; k < 7; k++) {
            sum += RadioPagingGsc::bchEncode(words[k]);
        }
        if ((sum & 0x7f) != words[7]) {
            m_current.m_checksumErrors++;
        }
    }

    for (int k = 0; k < 6; k++)
    {
        for (int i = 0; i < 7; i++) {
            m_currentBits.push_back((words[k] >> i) & 1);
        }
    }
    for (int i = 0; i < 6; i++) {
        m_currentBits.push_back((words[6] >> i) & 1);
    }

    bool more = (words[6] & 0x40) != 0;

    m_pos += RadioPagingGsc::m_blockBits;
    m_blocks++;

    if (!more || (m_blocks >= RadioPagingGsc::m_maxBlocks))
    {
        finishMessage(more);
        m_parse = Next;
    }

    return true;
}
