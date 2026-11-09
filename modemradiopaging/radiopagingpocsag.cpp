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

#include "radiopagingpocsag.h"

#include <algorithm>
#include <cstdio>

#include "radiopagingbch.h"

const char RadioPagingPocsag::m_numericChars[17] = "0123456789RU -][";
const uint32_t RadioPagingPocsag::m_syncWord;
const uint32_t RadioPagingPocsag::m_idleWord;
const int RadioPagingPocsag::m_preambleBits;
const int RadioPagingPocsag::m_codewordsPerBatch;
const int RadioPagingPocsag::m_framesPerBatch;
const uint32_t RadioPagingPocsag::m_maxAddress;

namespace {

const int dataBitsPerCodeword = 20;

int numericValue(char c)
{
    for (int i = 0; i < 16; i++)
    {
        if (RadioPagingPocsag::m_numericChars[i] == c) {
            return i;
        }
    }

    return -1;
}

// Appends the low nbBits of value, least significant bit first
void appendLsbFirst(std::vector<uint8_t>& bits, unsigned value, int nbBits)
{
    for (int i = 0; i < nbBits; i++) {
        bits.push_back((value >> i) & 1);
    }
}

int streamBit(const std::vector<uint8_t>& packed, int index)
{
    return (packed[index >> 3] >> (7 - (index & 7))) & 1;
}

} // namespace

RadioPagingPocsag::EncodeResult RadioPagingPocsag::encode(const std::vector<TxMessage>& messages, const EncodeOptions& options)
{
    EncodeResult result;
    int slot = m_codewordsPerBatch;     // Forces the first batch to start
    bool previousHadMessage = false;    // The previous message ended with message codewords

    auto startBatch = [&]() {
        result.m_codewords.push_back(m_syncWord);
        slot = 0;
    };

    auto put = [&](uint32_t word) {
        if (slot == m_codewordsPerBatch) {
            startBatch();
        }
        result.m_codewords.push_back(word);
        slot++;
    };

    for (const TxMessage& message : messages)
    {
        if (message.m_address > m_maxAddress)
        {
            result.m_error = "Address out of range";
            return result;
        }
        if ((message.m_address >= 2007664) && (message.m_address <= 2007671))
        {
            result.m_error = "Address collides with the idle codeword";
            return result;
        }
        if ((message.m_function < 0) || (message.m_function > 3))
        {
            result.m_error = "Function must be 0 to 3";
            return result;
        }

        // Message bits
        std::vector<uint8_t> bits;

        if (message.m_type == Numeric)
        {
            for (char c : message.m_text)
            {
                int v = numericValue(c);
                if (v < 0)
                {
                    result.m_error = std::string("Character not allowed in numeric message: ") + c;
                    return result;
                }
                appendLsbFirst(bits, v, 4);
            }
            if (bits.empty())
            {
                result.m_error = "Empty numeric message";
                return result;
            }
            // Pad with spaces
            while ((bits.size() % dataBitsPerCodeword) != 0) {
                appendLsbFirst(bits, numericValue(' '), 4);
            }
        }
        else if (message.m_type == Alphanumeric)
        {
            for (char c : message.m_text)
            {
                unsigned v = (unsigned char) c;
                if (v > 0x7f)
                {
                    result.m_error = "Character above 0x7f in alphanumeric message";
                    return result;
                }
                appendLsbFirst(bits, v, 7);
            }
            if (bits.empty())
            {
                result.m_error = "Empty alphanumeric message";
                return result;
            }
            // Pad with whole padding characters, then zero bits
            unsigned padding = options.m_padding & 0x7f;
            if (padding != 0)
            {
                while ((dataBitsPerCodeword - (int) (bits.size() % dataBitsPerCodeword)) % dataBitsPerCodeword >= 7) {
                    appendLsbFirst(bits, padding, 7);
                }
            }
            while ((bits.size() % dataBitsPerCodeword) != 0) {
                bits.push_back(0);
            }
        }

        // At least one idle or address codeword between the end of a message and the
        // address of the next message
        if (previousHadMessage) {
            put(m_idleWord);
        }

        // Place the address in its frame, in either codeword of the frame
        if (slot == m_codewordsPerBatch) {
            startBatch();
        }

        int frame = message.m_address & 7;
        int first = 2 * frame;

        if (slot > first + 1)
        {
            while (slot < m_codewordsPerBatch) {
                put(m_idleWord);
            }
            startBatch();
        }
        while (slot < first) {
            put(m_idleWord);
        }

        uint32_t data = ((message.m_address >> 3) << 2) | message.m_function;
        put(RadioPagingBCH::encode(data));

        for (size_t i = 0; i < bits.size(); i += dataBitsPerCodeword)
        {
            uint32_t chunk = 0;
            for (int j = 0; j < dataBitsPerCodeword; j++) {
                chunk = (chunk << 1) | bits[i + j];
            }
            put(RadioPagingBCH::encode((1u << 20) | chunk));
        }

        previousHadMessage = !bits.empty();
    }

    if (result.m_codewords.empty())
    {
        result.m_error = "No messages";
        return result;
    }

    // Complete the last batch with idle codewords. The last codeword of a transmission is
    // an idle codeword, so a full batch is followed by a batch of idle codewords
    if (slot == m_codewordsPerBatch) {
        startBatch();
    }
    while (slot < m_codewordsPerBatch) {
        put(m_idleWord);
    }

    if (options.m_preamble)
    {
        for (int i = 0; i < m_preambleBits; i++) {
            result.m_bits.push_back((i & 1) ? 0 : 1);
        }
    }
    for (uint32_t word : result.m_codewords)
    {
        for (int i = 31; i >= 0; i--) {
            result.m_bits.push_back((word >> i) & 1);
        }
    }

    result.m_ok = true;
    return result;
}

RadioPagingPocsag::EncodeResult RadioPagingPocsag::encode(const std::vector<TxMessage>& messages)
{
    return encode(messages, EncodeOptions());
}

std::string RadioPagingPocsag::decodeNumeric(const std::vector<uint8_t>& packed, int nbBits)
{
    std::string text;

    for (int i = 0; i + 4 <= nbBits; i += 4)
    {
        int v = streamBit(packed, i) | (streamBit(packed, i + 1) << 1)
              | (streamBit(packed, i + 2) << 2) | (streamBit(packed, i + 3) << 3);
        text.push_back(m_numericChars[v]);
    }

    // Trailing spaces are padding
    while (!text.empty() && (text.back() == ' ')) {
        text.pop_back();
    }

    return text;
}

std::string RadioPagingPocsag::decodeAlphanumeric(const std::vector<uint8_t>& packed, int nbBits)
{
    std::string text;

    for (int i = 0; i + 7 <= nbBits; i += 7)
    {
        int v = 0;
        for (int j = 0; j < 7; j++) {
            v |= streamBit(packed, i + j) << j;
        }
        text.push_back((char) v);
    }

    // Trailing NUL characters are padding
    while (!text.empty() && (text.back() == 0)) {
        text.pop_back();
    }

    return text;
}

int RadioPagingPocsag::scoreNumeric(const std::string& text)
{
    if (text.empty()) {
        return 0;
    }

    int good = 0;
    int odd = 0;

    for (char c : text)
    {
        if (((c >= '0') && (c <= '9')) || (c == ' ') || (c == '-')) {
            good++;
        } else {
            odd++;
        }
    }

    int score = (100 * (good - odd)) / (int) text.size();
    return std::max(0, std::min(100, score));
}

int RadioPagingPocsag::scoreAlphanumeric(const std::string& text)
{
    if (text.empty()) {
        return 0;
    }

    int printable = 0;
    int bad = 0;

    for (char c : text)
    {
        unsigned char u = (unsigned char) c;
        if (((u >= 0x20) && (u <= 0x7e)) || (u == '\r') || (u == '\n') || (u == '\t')) {
            printable++;
        } else {
            bad++;
        }
    }

    int score = (100 * (printable - 2 * bad)) / (int) text.size();
    return std::max(0, std::min(100, score));
}

RadioPagingPocsag::Interpretation RadioPagingPocsag::interpret(const std::vector<uint8_t>& packed, int nbBits)
{
    Interpretation result;

    if (nbBits <= 0)
    {
        result.m_type = "tone_only";
        return result;
    }

    std::string alpha = decodeAlphanumeric(packed, nbBits);
    std::string numeric = decodeNumeric(packed, nbBits);
    int alphaScore = scoreAlphanumeric(alpha);
    int numericScore = scoreNumeric(numeric);

    // Padding zeros of an alphanumeric message decode as the digit 0, so a tie is alphanumeric
    if (numericScore > alphaScore)
    {
        result.m_type = "numeric";
        result.m_text = numeric;
        result.m_score = numericScore;
        result.m_altType = "alphanumeric";
        result.m_altText = alpha;
        result.m_altScore = alphaScore;
    }
    else
    {
        result.m_type = "alphanumeric";
        result.m_text = alpha;
        result.m_score = alphaScore;
        result.m_altType = "numeric";
        result.m_altText = numeric;
        result.m_altScore = numericScore;
    }

    return result;
}

std::string RadioPagingPocsag::escapeControl(const std::string& text)
{
    std::string out;

    for (char c : text)
    {
        unsigned char u = (unsigned char) c;
        if ((u < 0x20) || (u == 0x7f))
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "<0x%02X>", u);
            out += buf;
        }
        else
        {
            out.push_back(c);
        }
    }

    return out;
}
