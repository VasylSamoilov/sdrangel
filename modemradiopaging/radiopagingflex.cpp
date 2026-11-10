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

#include "radiopagingflex.h"

#include <algorithm>
#include <cstring>

#include "radiopagingbch.h"

const char RadioPagingFlex::m_numericChars[17] = "0123456789.U -][";
const int RadioPagingFlex::m_wordsPerBlock;
const int RadioPagingFlex::m_blocksPerFrame;
const int RadioPagingFlex::m_wordsPerFrame;
const int RadioPagingFlex::m_maxPhases;
const int RadioPagingFlex::m_syncBaud;
const int RadioPagingFlex::m_maxCollapse;
const int RadioPagingFlex::m_maxAlphaChars;
const int RadioPagingFlex::m_maxNumericChars;
const int RadioPagingFlex::m_maxPriorityWords;
const uint64_t RadioPagingFlex::m_shortMin;
const uint64_t RadioPagingFlex::m_shortMax;
const uint64_t RadioPagingFlex::m_longMin;
const uint64_t RadioPagingFlex::m_longMax;
const unsigned RadioPagingFlex::m_vectorTone;
const unsigned RadioPagingFlex::m_vectorNumeric;
const unsigned RadioPagingFlex::m_vectorAlpha;
const unsigned RadioPagingFlex::m_vectorBinary;
const unsigned RadioPagingFlex::m_vectorSecure;
const int RadioPagingFlex::m_frameMs;
const int RadioPagingFlex::m_bitsPerFrame;

namespace {

const uint32_t shortAddressOffset = 32768;
const uint32_t idleOnes = 0xffffffffu;
const uint32_t idleZeros = 0x00000000u;
const int maxAlphaWords = RadioPagingFlex::m_wordsPerFrame - 4;
const int maxNumericWords = 8;
const int numericOverheadBits = 2;
const uint32_t alphaEtx = 0x03;
const int biwWords = 1;

// Second sync: 16 bit pattern and its inverse, sent most significant bit first
const uint32_t syncC = 0xed84;
const uint32_t syncCInv = 0x127b;

// Bit sync (32 bits), mode code (A, 32 bits), B (16 bits) and the inverted mode code
const uint8_t syncBs1[4] = {0xaa, 0xaa, 0xaa, 0xaa};
const uint8_t syncB[2] = {0x55, 0x55};
const uint8_t syncA[4][4] = {
    {0x78, 0xf3, 0x59, 0x39},   // 1600/2
    {0x84, 0xe7, 0x59, 0x39},   // 3200/2
    {0x4f, 0x97, 0x59, 0x39},   // 3200/4
    {0x21, 0x5f, 0x59, 0x39}    // 6400/4
};
const uint8_t syncAInv[4][4] = {
    {0x87, 0x0c, 0xa6, 0xc6},
    {0x7b, 0x18, 0xa6, 0xc6},
    {0xb0, 0x68, 0xa6, 0xc6},
    {0xde, 0xa0, 0xa6, 0xc6}
};

// Emergency re-synchronization cycle, transmission order (STD-43A Table 3.2.1-1): BS, Ar,
// inverted BS, inverted Ar
const uint8_t ersCycle[12] = {
    0xaa, 0xaa,
    0xcb, 0x20, 0x59, 0x39,
    0x55, 0x55,
    0x34, 0xdf, 0xa6, 0xc6
};

uint32_t reverse21(uint32_t value)
{
    return RadioPagingBCH::reverse(value & 0x1fffff) >> 11;
}

// Bits of a message word group sum used by the message checksums
uint32_t groupSum(uint32_t word)
{
    return (word & 0xff) + ((word >> 8) & 0xff) + ((word >> 16) & 0x1f);
}

int numericValue(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }

    switch (c)
    {
    case '.': return 0x0a;
    case 'U':
    case 'u': return 0x0b;
    case ' ': return 0x0c;
    case '-':
    case '_': return 0x0d;
    case ']': return 0x0e;
    case '[': return 0x0f;
    default: return 0;
    }
}

bool numericCharValid(char c)
{
    return ((c >= '0') && (c <= '9')) || (c == '-') || (c == '_') || (c == '[') || (c == ']')
        || (c == ' ') || (c == 'U') || (c == 'u') || (c == '.');
}

uint32_t alphaSignature(const std::string& text)
{
    uint32_t sum = 0;

    for (char c : text)
    {
        unsigned ch = ((unsigned char) c) & 0x7f;
        if (ch != alphaEtx) {
            sum += ch;
        }
    }

    return (~sum) & 0x7f;
}

// Message body words (data bits, not yet encoded) of an alphanumeric message
std::vector<uint32_t> alphaBody(const std::string& text, int number)
{
    std::vector<uint32_t> msg(maxAlphaWords, 0);
    size_t length = std::min<size_t>(text.size(), RadioPagingFlex::m_maxAlphaChars);

    // Header: fragment number 3 (only fragment), message number, retrieval flag
    msg[0] = (3u << 11) | (((uint32_t) number & 0x3f) << 13) | (1u << 19);

    // Characters, 7 bits each, 3 per word; the first character slot of word 1 holds the signature
    int wordIndex = 1;
    int shift = 7;

    for (size_t i = 0; i < length; i++)
    {
        msg[wordIndex] |= (((uint32_t) (unsigned char) text[i]) & 0x7f) << shift;
        shift += 7;
        if (shift == 21)
        {
            if (++wordIndex >= maxAlphaWords) {
                break;
            }
            shift = 0;
        }
    }

    // Unused character slots of the last word hold ETX
    if (shift == 7) {
        msg[wordIndex++] |= (alphaEtx << 7) | (alphaEtx << 14);
    } else if (shift == 14) {
        msg[wordIndex++] |= alphaEtx << 14;
    }

    msg[1] |= alphaSignature(text);

    // Checksum of the message words in 3 groups per word, in the 10 low bits of the header
    uint32_t sum = 0;
    for (int i = 0; i < wordIndex; i++) {
        sum += groupSum(msg[i]);
    }
    msg[0] |= (~sum) & 0x3ff;

    msg.resize(wordIndex);
    return msg;
}

// Message body words of a numeric message and the 6-bit checksum
std::vector<uint32_t> numericBody(const std::string& text, uint32_t& kBits)
{
    std::vector<uint32_t> msg(maxNumericWords, 0);
    int bit = numericOverheadBits;

    for (char c : text)
    {
        uint32_t nibble = numericValue(c);
        for (int b = 0; b < 4; b++)
        {
            int wordIndex = bit / 21;
            if (wordIndex >= maxNumericWords) {
                break;
            }
            if (nibble & (1u << b)) {
                msg[wordIndex] |= 1u << (bit % 21);
            }
            bit++;
        }
    }

    // Spaces in the unused digit positions of the last word
    int last = (bit > 0) ? (bit - 1) / 21 : 0;
    int endBit = (last + 1) * 21;

    while (bit + 4 <= endBit)
    {
        for (int b = 0; b < 4; b++)
        {
            if (0x0c & (1u << b)) {
                msg[bit / 21] |= 1u << (bit % 21);
            }
            bit++;
        }
    }

    // Checksum: sum of 3 groups per word, 8 bits kept, top 2 bits added to the low 6, inverted
    uint32_t k = 0;
    for (int i = 0; i <= last; i++) {
        k += groupSum(msg[i]);
    }
    k &= 0xff;
    k = (k & 0x3f) + (k >> 6);
    k = ~k;
    msg[0] |= (k >> 4) & 0x03;
    kBits = k & 0xf;

    msg.resize(last + 1);
    return msg;
}

int hexValue(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }
    return -1;
}

// Data bits of hex digits: digit i holds bits 4i to 4i+3, its least significant bit first. Empty if a
// character is not a hex digit
std::vector<uint8_t> hexBits(const std::string& hex)
{
    std::vector<uint8_t> bits;

    bits.reserve(hex.size() * 4);

    for (char c : hex)
    {
        int v = hexValue(c);

        if (v < 0) {
            return std::vector<uint8_t>();
        }
        for (int b = 0; b < 4; b++) {
            bits.push_back((uint8_t) ((v >> b) & 1));
        }
    }

    return bits;
}

// True if the last blocking bits (the last character) are all 0s or all 1s
bool lastCharacterUniform(const std::vector<uint8_t>& bits, int blocking)
{
    if ((blocking <= 0) || ((int) bits.size() < blocking)) {
        return false;
    }
    for (size_t i = bits.size() - blocking; i < bits.size(); i++)
    {
        if (bits[i] != bits.back()) {
            return false;
        }
    }
    return true;
}

// Data words of binary data in the last (here the only) fragment, terminated (STD-43A 3.10.1.2 p.61-62 rule
// (3), 3.10.1.4 p.70): data ending in the middle of a word, the rest of the word is the inverse of the last
// data bit; data ending exactly at a word end, a word of the inverse of the last data bit is added when the
// last character (blocking bits) is all 0s or all 1s. Not terminated (blocking 0): the unused bits are 0
std::vector<uint32_t> binaryDataWords(const std::vector<uint8_t>& bits, int blocking)
{
    const int wordBits = 21;
    std::vector<uint32_t> words((bits.size() + wordBits - 1) / wordBits, 0);

    for (size_t i = 0; i < bits.size(); i++)
    {
        if (bits[i]) {
            words[i / wordBits] |= 1u << (i % wordBits);
        }
    }

    if ((blocking > 0) && !bits.empty())
    {
        uint32_t inverse = bits.back() ? 0u : 1u;
        size_t rest = bits.size() % wordBits;

        if (rest != 0)
        {
            for (size_t b = rest; b < (size_t) wordBits; b++) {
                words.back() |= inverse << b;
            }
        }
        else if (lastCharacterUniform(bits, blocking))
        {
            words.push_back(inverse ? 0x1fffffu : 0u);
        }
    }

    return words;
}

// 1's complement of the sum of 8-bit groups of the data bits, the first bit of a group the least significant,
// a last group of fewer bits completed with 0 (STD-43A 3.10.1.2 p.61); termination bits are not included
uint32_t binarySignature(const std::vector<uint8_t>& bits)
{
    uint32_t sum = 0;

    for (size_t i = 0; i < bits.size(); i++) {
        sum += (uint32_t) bits[i] << (i % 8);
    }

    return (~sum) & 0xff;
}

int estimateBody(const RadioPagingFlex::PhaseMessage& message)
{
    size_t len = message.m_text.size();

    switch (message.m_type)
    {
    case RadioPagingFlex::Tone:
        return 0;
    case RadioPagingFlex::Alphanumeric:
        if ((len == 0) || (len > (size_t) RadioPagingFlex::m_maxAlphaChars)) {
            return -1;
        }
        return 1 + (int) ((len + 4) / 3);
    case RadioPagingFlex::Numeric:
    {
        if ((len == 0) || (len > (size_t) RadioPagingFlex::m_maxNumericChars)) {
            return -1;
        }
        int words = ((int) len * 4 + numericOverheadBits + 20) / 21;
        return std::max(1, std::min(words, maxNumericWords));
    }
    case RadioPagingFlex::ShortMessage:
        return 0;
    case RadioPagingFlex::Binary:
        return RadioPagingFlex::binaryWords(message.m_text, message.m_binary.m_blocking, 2);
    case RadioPagingFlex::Secure:
        if (len == 0) {
            return -1;
        }
        switch (message.m_binary.m_secureType)
        {
        case RadioPagingFlex::SecureAlphanumeric:
            return 1 + (int) ((len + 2) / 3);
        case RadioPagingFlex::SecureBinary:
            return RadioPagingFlex::binaryWords(message.m_text, 1, 1);
        case RadioPagingFlex::SecureSeparate:
            return hexBits(message.m_text).empty() ? -1 : 1 + (int) ((len * 4 + 20) / 21);
        }
        return -1;
    }

    return -1;
}

} // namespace

int RadioPagingFlex::phaseCount(Mode mode)
{
    switch (mode)
    {
    case Mode1600_2: return 1;
    case Mode3200_2:
    case Mode3200_4: return 2;
    case Mode6400_4: return 4;
    }

    return 1;
}

int RadioPagingFlex::baud(Mode mode)
{
    switch (mode)
    {
    case Mode1600_2: return 1600;
    case Mode3200_2: return 3200;
    case Mode3200_4: return 1600;
    case Mode6400_4: return 3200;
    }

    return 1600;
}

int RadioPagingFlex::levels(Mode mode)
{
    return ((mode == Mode3200_4) || (mode == Mode6400_4)) ? 4 : 2;
}

int RadioPagingFlex::bitRate(Mode mode)
{
    return baud(mode) * (levels(mode) / 2);
}

const char *RadioPagingFlex::modeName(Mode mode)
{
    switch (mode)
    {
    case Mode1600_2: return "1600/2";
    case Mode3200_2: return "3200/2";
    case Mode3200_4: return "3200/4";
    case Mode6400_4: return "6400/4";
    }

    return "?";
}

uint32_t RadioPagingFlex::encodeWord(uint32_t data)
{
    return RadioPagingBCH::encode(reverse21(data));
}

uint32_t RadioPagingFlex::wordData(uint32_t word)
{
    return reverse21(RadioPagingBCH::data(word));
}

uint32_t RadioPagingFlex::addChecksum(uint32_t data)
{
    uint32_t sum = ((data >> 4) & 0xf) + ((data >> 8) & 0xf) + ((data >> 12) & 0xf) + ((data >> 16) & 0xf) + ((data >> 20) & 0x1);
    return data | ((~sum) & 0xf);
}

bool RadioPagingFlex::checksumValid(uint32_t data)
{
    uint32_t sum = (data & 0xf) + ((data >> 4) & 0xf) + ((data >> 8) & 0xf) + ((data >> 12) & 0xf) + ((data >> 16) & 0xf) + ((data >> 20) & 0x1);
    return (sum & 0xf) == 0xf;
}

uint32_t RadioPagingFlex::frameInfoWord(unsigned cycle, unsigned frame, unsigned roaming, unsigned repeat, unsigned traffic)
{
    uint32_t data = (cycle & 0xf) << 4;
    data |= (frame & 0x7f) << 8;
    data |= (roaming & 1) << 15;
    data |= (repeat & 1) << 16;
    data |= (traffic & 0xf) << 17;
    return encodeWord(addChecksum(data));
}

// Word types 000 and 111 in bits 6-4; the checksum is in bits 3-0 as in every information word
uint32_t RadioPagingFlex::ssid1Word(unsigned localId, unsigned coverageZone)
{
    uint32_t data = (coverageZone & 0x1f) << 7;
    data |= (localId & 0x1ff) << 12;
    return encodeWord(addChecksum(data));
}

uint32_t RadioPagingFlex::ssid2Word(unsigned countryCode, unsigned trafficFlags)
{
    uint32_t data = 7u << 4;
    data |= (trafficFlags & 0xf) << 7;
    data |= (countryCode & 0x3ff) << 11;
    return encodeWord(addChecksum(data));
}

int RadioPagingFlex::ssidPhase(Mode mode, unsigned frame)
{
    int slot = (int) (frame & 3);

    switch (phaseCount(mode))
    {
    case 4: return slot;
    case 2: return (slot < 2) ? 0 : 1;
    default: return 0;
    }
}

uint32_t RadioPagingFlex::blockInfoWord(unsigned priorityWords, unsigned addressStart, unsigned vectorStart, unsigned carryOn, unsigned collapse)
{
    uint32_t data = (priorityWords & 0xf) << 4;
    data |= (addressStart & 0x3) << 8;
    data |= (vectorStart & 0x3f) << 10;
    data |= (carryOn & 0x3) << 16;
    data |= (collapse & 0x7) << 18;
    return encodeWord(addChecksum(data));
}

bool RadioPagingFlex::isShortAddress(uint64_t capcode)
{
    return (capcode >= m_shortMin) && (capcode <= m_shortMax);
}

bool RadioPagingFlex::isLongAddress(uint64_t capcode)
{
    return (capcode >= m_longMin) && (capcode <= m_longMax);
}

bool RadioPagingFlex::isValidAddress(uint64_t capcode)
{
    return isShortAddress(capcode) || isLongAddress(capcode);
}

uint32_t RadioPagingFlex::shortAddressWord(uint32_t capcode)
{
    return encodeWord((capcode + shortAddressOffset) & 0x1fffff);
}

bool RadioPagingFlex::longAddressWords(uint64_t capcode, uint32_t words[2])
{
    uint32_t w1;
    uint32_t w2;

    if ((capcode >= 2101249ULL) && (capcode <= 1075843072ULL))
    {
        uint64_t result = capcode - 2068481ULL;
        w1 = (uint32_t) (result % shortAddressOffset) + 1;
        w2 = 2097151u - (uint32_t) (result / shortAddressOffset);
    }
    else if ((capcode >= 1075843073ULL) && (capcode <= 3223326720ULL))
    {
        uint64_t result = capcode - 2068481ULL;
        w1 = (uint32_t) (result % shortAddressOffset) + 1;
        w2 = (uint32_t) (result / shortAddressOffset) + 1933312u;
    }
    else if ((capcode >= 3223326721ULL) && (capcode <= m_longMax))
    {
        uint64_t result = capcode - 2068479ULL;
        w1 = (uint32_t) (result % shortAddressOffset) + 2064383u;
        w2 = (uint32_t) (result / shortAddressOffset) + 1867776u;
    }
    else
    {
        return false;
    }

    words[0] = encodeWord(w1);
    words[1] = encodeWord(w2);
    return true;
}

uint64_t RadioPagingFlex::shortAddressCapcode(uint32_t data)
{
    return (uint64_t) data - shortAddressOffset;
}

bool RadioPagingFlex::longAddressCapcode(uint32_t data1, uint32_t data2, uint64_t& capcode)
{
    if ((data1 >= 1) && (data1 <= shortAddressOffset))
    {
        if ((data2 >= 2064383u) && (data2 <= 2097151u)) {
            capcode = (uint64_t) (2097151u - data2) * shortAddressOffset + (data1 - 1) + 2068481ULL;
        } else if ((data2 >= 1966081u) && (data2 <= 2031616u)) {
            capcode = (uint64_t) (data2 - 1933312u) * shortAddressOffset + (data1 - 1) + 2068481ULL;
        } else {
            return false;
        }
        return true;
    }
    if ((data1 >= 2064383u) && (data1 < 2064383u + shortAddressOffset) && (data2 >= 1966081u) && (data2 <= 1998848u))
    {
        capcode = (uint64_t) (data2 - 1867776u) * shortAddressOffset + (data1 - 2064383u) + 2068479ULL;
        return true;
    }

    return false;
}

uint32_t RadioPagingFlex::alphaVector(unsigned start, unsigned words)
{
    return messageVector(m_vectorAlpha, start, words);
}

uint32_t RadioPagingFlex::messageVector(unsigned type, unsigned start, unsigned words)
{
    uint32_t data = ((type & 7) << 4) | ((start & 0x7f) << 7) | ((words & 0x7f) << 14);
    return encodeWord(addChecksum(data));
}

int RadioPagingFlex::binaryWords(const std::string& hex, int blocking, int headerWords)
{
    std::vector<uint8_t> bits = hexBits(hex);

    if (bits.empty() || (blocking < 1) || (blocking > 16)) {
        return -1;
    }

    return headerWords + (int) binaryDataWords(bits, blocking).size();
}

std::vector<uint32_t> RadioPagingFlex::binaryBody(const std::string& hex, const BinaryParams& binary, int number, bool retrieval)
{
    std::vector<uint8_t> bits = hexBits(hex);

    if (bits.empty() || (binary.m_blocking < 1) || (binary.m_blocking > 16)) {
        return std::vector<uint32_t>();
    }

    std::vector<uint32_t> data = binaryDataWords(bits, binary.m_blocking);
    std::vector<uint32_t> msg(2, 0);

    // Header word 1 (Fig. 3.10.1.2-1): K in bits 0-11 (set last), C = 0 (bit 12), F = 11 (only fragment,
    // bits 13-14), N in bits 15-20
    msg[0] = (3u << 13) | (((uint32_t) number & 0x3f) << 15);
    // Header word 2: R, M, D, H, B (16 sent as 0000), s = 0000 (reserved), I, S
    msg[1] = (retrieval ? 1u : 0u)
        | ((binary.m_mailDrop ? 1u : 0u) << 1)
        | ((binary.m_rightToLeft ? 1u : 0u) << 2)
        | ((binary.m_header ? 1u : 0u) << 3)
        | (((uint32_t) binary.m_blocking & 0xf) << 4)
        | ((binary.m_statusInfo ? 1u : 0u) << 12)
        | (binarySignature(bits) << 13);
    msg.insert(msg.end(), data.begin(), data.end());

    // K: 1's complement of the sum of 3 groups per word of all words, K counted as 0, 12 bits kept (p.59)
    uint32_t sum = 0;
    for (uint32_t w : msg) {
        sum += groupSum(w);
    }
    msg[0] |= (~sum) & 0xfff;

    return msg;
}

std::vector<uint32_t> RadioPagingFlex::secureBody(const std::string& text, SecureType type, int number)
{
    std::vector<uint32_t> msg(1, 0);

    // Header word (Fig. 3.10.1.4-1): K in bits 0-9 (set last), C = 0 (bit 10), F = 11 (only fragment, bits
    // 11-12), N in bits 13-18, t1 t0 in bits 19-20. The data start at word 2, there is no signature
    msg[0] = (3u << 11) | (((uint32_t) number & 0x3f) << 13) | (((uint32_t) type & 3) << 19);

    if (type == SecureAlphanumeric)
    {
        if (text.empty()) {
            return std::vector<uint32_t>();
        }

        // 7-bit characters, 3 a word from the first slot of word 2; unused slots of the last word ETX
        int shift = 0;

        for (char c : text)
        {
            if (shift == 0) {
                msg.push_back(0);
            }
            msg.back() |= (((uint32_t) (unsigned char) c) & 0x7f) << shift;
            shift = (shift + 7) % 21;
        }
        while (shift != 0)
        {
            msg.back() |= alphaEtx << shift;
            shift = (shift + 7) % 21;
        }
    }
    else if ((type == SecureBinary) || (type == SecureSeparate))
    {
        std::vector<uint8_t> bits = hexBits(text);

        if (bits.empty()) {
            return std::vector<uint32_t>();
        }

        // Binary data are terminated as 1-bit characters (p.70); the termination of data defined separately
        // is not defined: the unused bits are 0
        std::vector<uint32_t> data = binaryDataWords(bits, (type == SecureBinary) ? 1 : 0);
        msg.insert(msg.end(), data.begin(), data.end());
    }
    else
    {
        return std::vector<uint32_t>();
    }

    // K: 1's complement of the sum of 3 groups per word of all words, K counted as 0, 10 bits kept (p.69)
    uint32_t sum = 0;
    for (uint32_t w : msg) {
        sum += groupSum(w);
    }
    msg[0] |= (~sum) & 0x3ff;

    return msg;
}

uint32_t RadioPagingFlex::numericVector(unsigned start, unsigned words, unsigned kBits)
{
    // The length field holds the number of words minus 1
    unsigned length = (words > 0) ? words - 1 : 0;
    uint32_t data = (m_vectorNumeric << 4) | ((start & 0x7f) << 7) | ((length & 0x7) << 14) | ((kBits & 0xf) << 17);
    return encodeWord(addChecksum(data));
}

void RadioPagingFlex::shortMessageVector(ShortType type, const std::string& digits, int source, int number, bool retrieval,
    bool longAddress, uint32_t words[2])
{
    uint32_t d = 0;     // d0-d11
    uint32_t d2 = 0;    // d12-d32

    if (type == ShortNumeric)
    {
        uint32_t nibbles[m_maxShortDigitsLong];

        for (int i = 0; i < m_maxShortDigitsLong; i++) {
            nibbles[i] = (i < (int) digits.size()) ? (uint32_t) numericValue(digits[i]) : 0x0cu;
        }

        d = nibbles[0] | (nibbles[1] << 4) | (nibbles[2] << 8);
        d2 = nibbles[3] | (nibbles[4] << 4) | (nibbles[5] << 8) | (nibbles[6] << 12) | (nibbles[7] << 16);
    }
    else if (type == ShortSource)
    {
        d = (uint32_t) source & 7;
    }
    else
    {
        d = ((uint32_t) source & 7) | (((uint32_t) number & 0x3f) << 3) | ((retrieval ? 1u : 0u) << 9);
    }

    words[0] = encodeWord(addChecksum((m_vectorTone << 4) | (((uint32_t) type & 3) << 7) | (d << 9)));
    words[1] = longAddress ? encodeWord(d2) : 0;
}

void RadioPagingFlex::fillIdle(uint32_t *words, int phase, Mode mode)
{
    bool lsbPhase = false;

    if (mode == Mode3200_4) {
        lsbPhase = (phase == 1);
    } else if (mode == Mode6400_4) {
        lsbPhase = ((phase == 1) || (phase == 3));
    }

    for (int w = 0; w < m_wordsPerFrame; w++) {
        words[w] = lsbPhase ? idleZeros : (((w % 2) == 0) ? idleOnes : idleZeros);
    }
}

void RadioPagingFlex::interleaveBlock(uint32_t *words)
{
    uint8_t bytes[32];

    for (int i = 0; i < 32; i++)
    {
        uint8_t b = 0;
        for (int k = 0; k < m_wordsPerBlock; k++) {
            b |= ((words[k] >> (31 - i)) & 1) << (7 - k);
        }
        bytes[i] = b;
    }
    for (int k = 0; k < m_wordsPerBlock; k++) {
        words[k] = ((uint32_t) bytes[4 * k] << 24) | ((uint32_t) bytes[4 * k + 1] << 16)
                 | ((uint32_t) bytes[4 * k + 2] << 8) | bytes[4 * k + 3];
    }
}

void RadioPagingFlex::deinterleaveBlock(uint32_t *words)
{
    uint8_t bytes[32];
    uint32_t out[m_wordsPerBlock];

    for (int k = 0; k < m_wordsPerBlock; k++)
    {
        bytes[4 * k] = words[k] >> 24;
        bytes[4 * k + 1] = words[k] >> 16;
        bytes[4 * k + 2] = words[k] >> 8;
        bytes[4 * k + 3] = words[k];
        out[k] = 0;
    }
    for (int i = 0; i < 32; i++)
    {
        for (int k = 0; k < m_wordsPerBlock; k++) {
            out[k] |= (uint32_t) ((bytes[i] >> (7 - k)) & 1) << (31 - i);
        }
    }
    for (int k = 0; k < m_wordsPerBlock; k++) {
        words[k] = out[k];
    }
}

bool RadioPagingFlex::validateMessage(uint64_t capcode, MessageType type, const std::string& text, std::string& error)
{
    return validateMessage(capcode, type, text, error, BinaryParams());
}

bool RadioPagingFlex::validateMessage(uint64_t capcode, MessageType type, const std::string& text, std::string& error,
    const BinaryParams& binary)
{
    if (!isValidAddress(capcode))
    {
        error = "Capcode out of range";
        return false;
    }
    if (type == Numeric)
    {
        if (text.empty())
        {
            error = "Empty numeric message";
            return false;
        }
        if (text.size() > (size_t) m_maxNumericChars)
        {
            error = "Numeric message too long";
            return false;
        }
        for (char c : text)
        {
            if (!numericCharValid(c))
            {
                error = std::string("Character not allowed in numeric message: ") + c;
                return false;
            }
        }
    }
    else if (type == ShortMessage)
    {
        // Empty: every digit a space, the tone only payload
        size_t maxDigits = isLongAddress(capcode) ? m_maxShortDigitsLong : m_maxShortDigits;

        if (text.size() > maxDigits)
        {
            error = "Short message too long: " + std::to_string(maxDigits) + " digits at most";
            return false;
        }
        for (char c : text)
        {
            if (!numericCharValid(c))
            {
                error = std::string("Character not allowed in short message: ") + c;
                return false;
            }
        }
    }
    else if (type == Alphanumeric)
    {
        if (text.empty())
        {
            error = "Empty alphanumeric message";
            return false;
        }
        if (text.size() > (size_t) m_maxAlphaChars)
        {
            error = "Alphanumeric message too long";
            return false;
        }
        for (char c : text)
        {
            if (((unsigned char) c) > 0x7f)
            {
                error = "Character above 0x7f in alphanumeric message";
                return false;
            }
        }
    }
    else if (type == Binary)
    {
        if (text.empty())
        {
            error = "Empty HEX/binary message";
            return false;
        }
        if (hexBits(text).empty())
        {
            error = "HEX/binary message: hex digits only";
            return false;
        }
        if ((binary.m_blocking < 1) || (binary.m_blocking > 16))
        {
            error = "HEX/binary message: blocking length 1 to 16 bits";
            return false;
        }
        // Display direction: only for characters of a blocking length other than 0001 (3.10.1.2 p.60)
        if (binary.m_rightToLeft && (binary.m_blocking == 1))
        {
            error = "HEX/binary message: right to left display not allowed with a blocking length of 1";
            return false;
        }
        // A message ends on a character boundary (3.10.1.2 p.61)
        if (((text.size() * 4) % (size_t) binary.m_blocking) != 0)
        {
            error = "HEX/binary message: " + std::to_string(text.size() * 4) + " bits, not a multiple of the blocking length "
                + std::to_string(binary.m_blocking);
            return false;
        }
        if (binaryWords(text, binary.m_blocking, 2) > maxAlphaWords)
        {
            error = "HEX/binary message too long";
            return false;
        }
    }
    else if (type == Secure)
    {
        if (text.empty())
        {
            error = "Empty secure message";
            return false;
        }
        if (binary.m_secureType == SecureAlphanumeric)
        {
            if (1 + (int) ((text.size() + 2) / 3) > maxAlphaWords)
            {
                error = "Secure message too long";
                return false;
            }
            for (char c : text)
            {
                if (((unsigned char) c) > 0x7f)
                {
                    error = "Character above 0x7f in secure message";
                    return false;
                }
            }
        }
        else if ((binary.m_secureType == SecureBinary) || (binary.m_secureType == SecureSeparate))
        {
            if (hexBits(text).empty())
            {
                error = "Secure binary message: hex digits only";
                return false;
            }
            int words = (binary.m_secureType == SecureBinary) ? binaryWords(text, 1, 1)
                : 1 + (int) ((text.size() * 4 + 20) / 21);
            if (words > maxAlphaWords)
            {
                error = "Secure message too long";
                return false;
            }
        }
        else
        {
            error = "Secure message type: alphanumeric, separate or binary (11 is reserved)";
            return false;
        }
    }

    return true;
}

int RadioPagingFlex::messageCost(const PhaseMessage& message, std::string *error)
{
    std::string reason;

    if (!validateMessage(message.m_capcode, message.m_type, message.m_text, reason, message.m_binary))
    {
        if (error) {
            *error = reason;
        }
        return -1;
    }

    bool isLong = isLongAddress(message.m_capcode);
    int addressWords = isLong ? 2 : 1;

    if (message.m_type == Tone) {
        return addressWords;
    }

    int vectorWords = isLong ? 2 : 1;

    if (message.m_type == ShortMessage)
    {
        if (((message.m_shortType != ShortNumeric) && (message.m_shortType != ShortSource) && (message.m_shortType != ShortNumbered))
            || (message.m_source < 0) || (message.m_source > 7) || ((message.m_shortType != ShortNumeric) && !message.m_text.empty()))
        {
            if (error) {
                *error = "Short message: type numeric, source or numbered, source 0 to 7, digits only in the numeric type";
            }
            return -1;
        }

        return addressWords + vectorWords;
    }

    int body = estimateBody(message);

    // The first message word of a long address is in the second vector word
    if (isLong && (body > 0)) {
        body--;
    }

    return addressWords + vectorWords + body;
}

RadioPagingFlex::PhaseResult RadioPagingFlex::buildPhase(const PhaseParams& params, const std::vector<PhaseMessage>& messages)
{
    struct Info
    {
        int m_addressWords = 0;
        int m_vectorWords = 0;
        int m_bodyWords = 0;        // Estimate, used for the capacity only
        bool m_long = false;
        bool m_valid = false;
        bool m_packed = false;
    };

    PhaseResult result;
    result.m_packed.assign(messages.size(), false);

    uint32_t words[m_wordsPerFrame];
    fillIdle(words, params.m_phase, params.m_mode);

    std::vector<Info> info(messages.size());
    std::vector<int> order;
    std::vector<int> tones;

    // Priority addresses first, then the others; tone only addresses (no vector) last
    for (int pass = 0; pass < 3; pass++)
    {
        for (size_t i = 0; i < messages.size(); i++)
        {
            const PhaseMessage& m = messages[i];
            bool isTone = (m.m_type == Tone);
            bool inPass = (pass == 0) ? (m.m_priority && !isTone) : (pass == 1) ? (!m.m_priority && !isTone) : isTone;

            if (!inPass || !isValidAddress(m.m_capcode)) {
                continue;
            }

            Info& in = info[i];
            in.m_long = isLongAddress(m.m_capcode);
            in.m_addressWords = in.m_long ? 2 : 1;
            in.m_vectorWords = isTone ? 0 : (in.m_long ? 2 : 1);

            if (!isTone && (m.m_type != ShortMessage))
            {
                int body = estimateBody(m);
                if (body < 0) {
                    continue;
                }
                in.m_bodyWords = body;
            }

            in.m_valid = true;
            order.push_back((int) i);
        }
    }

    int totalAddress = 0;
    int totalVector = 0;
    int priorityWords = 0;
    // Block information word 1 and the others (2 to 4), before the address field
    const int extra = (int) std::min<size_t>(params.m_extraBiw.size(), 3);
    const int biwCount = biwWords + extra;
    int capacity = m_wordsPerFrame - biwCount;
    size_t toneStart = order.size();

    for (size_t k = 0; k < order.size(); k++)
    {
        if (messages[order[k]].m_type == Tone)
        {
            toneStart = k;
            break;
        }
    }

    // Greedy packing, stops at the first message that does not fit
    for (size_t k = 0; k < toneStart; k++)
    {
        Info& in = info[order[k]];
        int bodyWords = in.m_bodyWords;

        if (in.m_long && (bodyWords > 0)) {
            bodyWords--;
        }

        int needed = in.m_addressWords + in.m_vectorWords + bodyWords;
        if (needed > capacity) {
            break;
        }

        in.m_packed = true;
        totalAddress += in.m_addressWords;
        totalVector += in.m_vectorWords;
        capacity -= needed;
        if (messages[order[k]].m_priority) {
            priorityWords += in.m_addressWords;
        }
    }

    for (size_t k = toneStart; k < order.size(); k++)
    {
        Info& in = info[order[k]];

        if (in.m_addressWords > capacity) {
            break;
        }

        in.m_packed = true;
        totalAddress += in.m_addressWords;
        capacity -= in.m_addressWords;
    }

    result.m_addressWords = totalAddress;
    priorityWords = std::min(priorityWords, m_maxPriorityWords);

    unsigned vectorStart = (unsigned) (biwCount + totalAddress);
    if (vectorStart > 63)
    {
        result.m_error = "Address field too long";
        return result;
    }

    int fwc = 0;
    words[fwc++] = blockInfoWord(priorityWords, (unsigned) extra, vectorStart, params.m_carryOn, params.m_collapse);

    for (int i = 0; i < extra; i++) {
        words[fwc++] = params.m_extraBiw[i];
    }

    // Address field
    for (int idx : order)
    {
        if (!info[idx].m_packed) {
            continue;
        }

        if (info[idx].m_long)
        {
            uint32_t pair[2] = {0, 0};
            longAddressWords(messages[idx].m_capcode, pair);
            words[fwc++] = pair[0];
            words[fwc++] = pair[1];
        }
        else
        {
            words[fwc++] = shortAddressWord((uint32_t) messages[idx].m_capcode);
        }
    }

    // Vector field and message field
    unsigned vecCursor = vectorStart;
    unsigned msgCursor = vectorStart + (unsigned) totalVector;

    for (size_t k = 0; k < toneStart; k++)
    {
        int idx = order[k];
        const PhaseMessage& m = messages[idx];
        const Info& in = info[idx];

        if (!in.m_packed) {
            continue;
        }

        std::vector<uint32_t> body;
        uint32_t vector;

        if (m.m_type == ShortMessage)
        {
            // No message field: the vector word(s) only
            uint32_t vw[2];
            // Numbered: the retrieval flag is set, as in alphanumeric messages (the pager checks the numbers)
            shortMessageVector(m.m_shortType, m.m_text, m.m_source, m.m_number, true, in.m_long, vw);

            if (vecCursor < (unsigned) m_wordsPerFrame) {
                words[vecCursor++] = vw[0];
            }
            if (in.m_long && (vecCursor < (unsigned) m_wordsPerFrame)) {
                words[vecCursor++] = vw[1];
            }
            continue;
        }

        if (m.m_type == Alphanumeric)
        {
            body = alphaBody(m.m_text, m.m_number);
            vector = alphaVector(msgCursor, (unsigned) body.size());
        }
        else if (m.m_type == Binary)
        {
            // Retrieval flag set, as in alphanumeric messages (the pager checks the numbers)
            body = binaryBody(m.m_text, m.m_binary, m.m_number, true);
            vector = messageVector(m_vectorBinary, msgCursor, (unsigned) body.size());
        }
        else if (m.m_type == Secure)
        {
            // No message numbering for secure messages (3.10.1.4 p.70): N tells the messages of an address apart
            body = secureBody(m.m_text, m.m_binary.m_secureType, m.m_number);
            vector = messageVector(m_vectorSecure, msgCursor, (unsigned) body.size());
        }
        else
        {
            uint32_t kBits = 0;
            body = numericBody(m.m_text, kBits);
            // A one word numeric message of a long address is in the second vector word
            unsigned start = (in.m_long && (in.m_bodyWords == 1)) ? vecCursor + 1 : msgCursor;
            vector = numericVector(start, (unsigned) body.size(), kBits);
        }

        for (uint32_t& w : body) {
            w = encodeWord(w);
        }

        if (vecCursor < (unsigned) m_wordsPerFrame) {
            words[vecCursor++] = vector;
        }
        if (in.m_long && (vecCursor < (unsigned) m_wordsPerFrame)) {
            words[vecCursor++] = body.empty() ? encodeWord(0) : body[0];
        }

        for (size_t j = in.m_long ? 1 : 0; j < body.size(); j++)
        {
            if (msgCursor < (unsigned) m_wordsPerFrame) {
                words[msgCursor++] = body[j];
            }
        }
    }

    if ((msgCursor > (unsigned) m_wordsPerFrame) || (vecCursor > (unsigned) m_wordsPerFrame))
    {
        result.m_error = "Phase overflow";
        return result;
    }

    for (int b = 0; b < m_blocksPerFrame; b++) {
        interleaveBlock(words + b * m_wordsPerBlock);
    }

    for (size_t i = 0; i < messages.size(); i++) {
        result.m_packed[i] = info[i].m_packed;
    }

    result.m_words.assign(words, words + m_wordsPerFrame);
    result.m_ok = true;
    return result;
}

unsigned RadioPagingFlex::lowTrafficFlags(const std::vector<int>& addressWords, int phases)
{
    unsigned flags = 0;

    for (int p = 0; (p < phases) && (p < m_maxPhases) && (p < (int) addressWords.size()); p++)
    {
        if (biwWords + addressWords[p] <= m_wordsPerBlock) {
            flags |= 1u << p;
        }
    }

    if (phases == 1)
    {
        flags = (flags & 1) ? 0xf : 0;
    }
    else if (phases == 2)
    {
        unsigned t0 = flags & 1;
        unsigned t1 = (flags >> 1) & 1;
        flags = t0 | (t0 << 1) | (t1 << 2) | (t1 << 3);
    }

    return flags & 0xf;
}

RadioPagingFlex::FrameBytes RadioPagingFlex::assemble(const FrameParams& params, const std::vector<std::vector<uint32_t>>& phases)
{
    FrameBytes out;
    int mode = (int) params.m_mode;
    int phaseTotal = phaseCount(params.m_mode);
    bool fourLevel = (levels(params.m_mode) == 4);

    if ((int) phases.size() != phaseTotal) {
        return out;
    }

    // Sync part: bit sync, mode code, B, inverted mode code, frame information word
    out.m_sync.insert(out.m_sync.end(), syncBs1, syncBs1 + 4);
    out.m_sync.insert(out.m_sync.end(), syncA[mode], syncA[mode] + 4);
    out.m_sync.insert(out.m_sync.end(), syncB, syncB + 2);
    out.m_sync.insert(out.m_sync.end(), syncAInv[mode], syncAInv[mode] + 4);

    uint32_t fiw = frameInfoWord(params.m_cycle, params.m_frame, params.m_roaming ? 1 : 0, 0, params.m_traffic);
    for (int s = 24; s >= 0; s -= 8) {
        out.m_sync.push_back((fiw >> s) & 0xff);
    }

    // Second sync: alternating pattern, C, inverted alternating pattern and inverted C. The
    // pattern has 4, 24 or 64 bits (4-level: 2 bit symbols 10, 00, 10, 00 ...)
    int patternBits = (params.m_mode == Mode1600_2) ? 4 : (params.m_mode == Mode6400_4) ? 64 : 24;
    std::vector<uint8_t> bits;

    auto pattern = [&](bool inverse) {
        if (fourLevel)
        {
            for (int s = 0; s < patternBits / 2; s++)
            {
                bool one = (((s & 1) == 0) != inverse);
                bits.push_back(one ? 1 : 0);
                bits.push_back(0);
            }
        }
        else
        {
            for (int i = 0; i < patternBits; i++) {
                bits.push_back((((i & 1) == 0) != inverse) ? 1 : 0);
            }
        }
    };
    auto sixteen = [&](uint32_t value) {
        for (int i = 15; i >= 0; i--) {
            bits.push_back((value >> i) & 1);
        }
    };

    pattern(false);
    sixteen(syncC);
    pattern(true);
    sixteen(syncCInv);

    for (size_t i = 0; i < bits.size(); i += 8)
    {
        uint8_t b = 0;
        for (size_t j = 0; j < 8; j++) {
            b = (b << 1) | ((i + j < bits.size()) ? bits[i + j] : 0);
        }
        out.m_data.push_back(b);
    }

    // Phase data. The bits of the phases are interleaved word by word (most significant bit first)
    uint8_t cur = 0;
    int curBits = 0;

    auto put = [&](int count, unsigned value) {
        cur = (cur << count) | (value & ((1u << count) - 1));
        curBits += count;
        if (curBits == 8)
        {
            out.m_data.push_back(cur);
            cur = 0;
            curBits = 0;
        }
    };

    for (int w = 0; w < m_wordsPerFrame; w++)
    {
        if (phaseTotal == 1)
        {
            for (int s = 24; s >= 0; s -= 8) {
                out.m_data.push_back((phases[0][w] >> s) & 0xff);
            }
            continue;
        }

        for (int bit = 31; bit >= 0; bit--)
        {
            unsigned a = (phases[0][w] >> bit) & 1;
            unsigned c = (phases[1][w] >> bit) & 1;

            if (phaseTotal == 2)
            {
                if (fourLevel)
                {
                    put(2, (a << 1) | c);
                }
                else
                {
                    put(1, a);
                    put(1, c);
                }
            }
            else
            {
                unsigned b = c;
                unsigned c3 = (phases[2][w] >> bit) & 1;
                unsigned d = (phases[3][w] >> bit) & 1;
                put(2, (a << 1) | b);
                put(2, (c3 << 1) | d);
            }
        }
    }

    return out;
}

std::vector<uint8_t> RadioPagingFlex::bytesToSymbols(const std::vector<uint8_t>& bytes, int levelCount, bool inverted)
{
    static const uint8_t gray[4] = {0, 1, 3, 2};
    std::vector<uint8_t> symbols;

    for (uint8_t byte : bytes)
    {
        if (levelCount == 2)
        {
            for (int i = 7; i >= 0; i--)
            {
                uint8_t s = (byte >> i) & 1;
                symbols.push_back(inverted ? 1 - s : s);
            }
        }
        else
        {
            for (int i = 6; i >= 0; i -= 2)
            {
                uint8_t s = gray[(byte >> i) & 3];
                symbols.push_back(inverted ? 3 - s : s);
            }
        }
    }

    return symbols;
}

std::vector<RadioPagingFlex::SymbolBlock> RadioPagingFlex::toSymbols(const FrameBytes& bytes, Mode mode, bool inverted)
{
    std::vector<SymbolBlock> blocks(2);

    blocks[0].m_baud = m_syncBaud;
    blocks[0].m_levels = 2;
    blocks[0].m_symbols = bytesToSymbols(bytes.m_sync, 2, inverted);
    blocks[1].m_baud = baud(mode);
    blocks[1].m_levels = levels(mode);
    blocks[1].m_symbols = bytesToSymbols(bytes.m_data, levels(mode), inverted);
    return blocks;
}

RadioPagingFlex::SymbolBlock RadioPagingFlex::ers(int bits, bool inverted)
{
    SymbolBlock block;
    std::vector<uint8_t> cycle(ersCycle, ersCycle + sizeof(ersCycle));
    std::vector<uint8_t> cycleSymbols = bytesToSymbols(cycle, 2, inverted);

    block.m_baud = m_syncBaud;
    block.m_levels = 2;
    block.m_symbols.reserve(std::max(0, bits));

    for (int i = 0; i < bits; i++) {
        block.m_symbols.push_back(cycleSymbols[i % cycleSymbols.size()]);
    }

    return block;
}

int RadioPagingFlex::ersMinBits(int collapse)
{
    collapse = std::max(0, std::min(m_maxCollapse, collapse));
    return m_bitsPerFrame << collapse;
}
