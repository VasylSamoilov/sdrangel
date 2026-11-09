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

#ifndef MODEMRADIOPAGING_RADIOPAGINGGSCDECODER_H
#define MODEMRADIOPAGING_RADIOPAGINGGSCDECODER_H

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "export.h"
#include "radiopaginggsc.h"

// Sequential Golay bit stream decoder for one polarity. It hunts for sync on its own,
// using nothing but the bits it is fed.
//
// Preamble, start code and addresses are Golay words at 300 bit/s, commas and data blocks
// are at 600 bit/s. The decoder works on 600 baud symbols: a 300 bit/s bit is decided on
// the sum of its two soft symbols, a 600 bit/s bit on its symbol.
//
// Sync is a sliding window of 46 symbols tested as a duplicated Golay codeword against
// the 10 preamble values. Three hits in a row at a distance of 46 symbols, all for the
// same value, lock the decoder. A complemented preamble is a batch. After the lock the
// decoder looks for the start code on the word grid of the preamble: after each further
// preamble word a comma and the start code may follow. The start code is detected on
// Word 1 or on Word 2 (its complement), each on its own, and its position is refined to
// the symbol that correlates best, which gives the timing of the 600 bit/s blocks. A word
// is only taken where its symbols come in equal pairs, so a comma (alternating symbols,
// whose pair sums are noise) is never decoded as a word. Then
// addresses and data blocks are parsed as they arrive, so a message is available as soon
// as its last block is in. A transmission ends when what follows an address is neither a
// start code nor an address, when a new preamble is locked, or on flush().
//
// Symbols are the output of an FSK demodulator, positive = positive deviation. For normal
// polarity a binary 1 is a positive deviation, inverted polarity is the opposite. Run
// one decoder per polarity: a transmission is only decoded by the decoder whose
// polarity matches, because the preamble and the start code tell the polarities apart.
//
// The optional non battery saver mode (a 1100 pattern instead of the preamble, no start
// code) has no such structure: both polarities decode it, the inverted one with the
// address number bits flipped.
class MODEMRADIOPAGING_API RadioPagingGscDecoder
{
public:
    enum Polarity {
        Normal,
        Inverted
    };

    enum State {
        Hunting,        //!< Looking for a preamble
        AwaitStart,     //!< Locked on the preamble, looking for the start code or the address
        InTransmission  //!< Receiving addresses and messages
    };

    struct Message
    {
        std::string m_address;              //!< 6 digits: index, group, 3 address digits. Index is '?' in non battery saver mode
        int m_addressNumber = 1;            //!< 1-4
        RadioPagingGsc::MessageType m_type = RadioPagingGsc::Tone;   //!< Tone, Voice or Data
        std::vector<uint8_t> m_rawData;     //!< Message bits, packed 8 per byte, first bit in the MSB
        int m_rawBits = 0;                  //!< Number of message bits, a multiple of 48; 0 = tone only or voice
        int m_preamble = -1;                //!< Preamble value index, -1 in non battery saver mode
        int m_slot = 0;                     //!< Position of the address in the transmission, from 0
        bool m_batch = false;               //!< Transmission with a complemented preamble
        bool m_nonBatterySaver = false;
        int m_errorsCorrected = 0;          //!< Bits corrected in the codewords of the address and message
        int m_errorsUncorrectable = 0;      //!< BCH codewords that could not be corrected
        int m_checksumErrors = 0;           //!< Blocks with a wrong checksum
        bool m_truncated = false;           //!< The transmission ended before the message did
        Polarity m_polarity = Normal;
    };

    struct Status
    {
        State m_state;
        Polarity m_polarity;
        uint32_t m_codewords;               //!< Golay and BCH codewords of addresses and messages received since reset
        uint32_t m_codewordsCorrected;
        uint32_t m_codewordsUncorrectable;
        uint32_t m_transmissions;           //!< Preamble locks since reset
        uint32_t m_messages;                //!< Messages produced since reset
    };

    explicit RadioPagingGscDecoder(Polarity polarity = Normal);

    void reset();
    void setPolarity(Polarity polarity) { m_polarity = polarity; }
    //!< Enable the detection of the non battery saver pattern. Off by default
    void setNonBatterySaver(bool enable) { m_nonBatterySaver = enable; }
    //!< Feed one hard symbol: 1 = positive deviation, 0 = negative deviation
    void feed(int symbol);
    //!< Feed one soft symbol: positive = positive deviation, the magnitude is the confidence
    void feedSoft(float soft);
    //!< The signal is gone: finish the transmission in progress
    void flush();

    bool isSynced() const { return m_state != Hunting; }
    Status getStatus() const;
    bool hasMessage() const { return !m_messages.empty(); }
    Message takeMessage();

    static const int m_minNonBatterySaverBits = 80;     //!< Bits of the 1100 pattern before the lock
    static const int m_syncHits = 3;
    //!< Preamble words that can follow the lock before the start code
    static const int m_maxPreambleWordsAfterLock = RadioPagingGsc::m_preambleCodewords - m_syncHits;
    //!< Share of the symbol energy of a word kept by the sums of its symbol pairs (1 for
    //!< equal pairs, 0 for a comma) above which it is taken for a word
    static const float m_minPairAgreement;

private:
    enum Parse {
        SeekStart,      //!< Preamble locked, start code not seen yet
        SeekAddress,    //!< Non battery saver pattern ended, first address not seen yet
        Next,           //!< Waiting for the next address, start code or the end
        Classify,       //!< Address received, telling tone, voice and data apart
        Data            //!< Receiving the blocks of a message
    };

    Polarity m_polarity;
    bool m_nonBatterySaver;
    State m_state;
    Parse m_parse;

    uint64_t m_reg;                     //!< Last 64 bits, newest in bit 0
    int m_regBits;
    float m_softRing[RadioPagingGsc::m_codewordBits];   //!< Last 46 soft symbols, oldest at m_softPos
    int m_softPos;
    int8_t m_hits[2 * RadioPagingGsc::m_codewordBits + 1];  //!< Preamble hit history, ring indexed by m_hitPos
    int m_hitPos;
    int m_patternRun;                   //!< Consecutive bits of the 1100 pattern

    std::vector<uint8_t> m_buf;         //!< Bits received since the lock
    std::vector<float> m_soft;          //!< Soft symbols of m_buf, positive = 1
    int m_pos;                          //!< Parse position in m_buf (may be past its end while skipping a comma)
    int m_scan;                         //!< Next start code or address candidate offset

    int m_preamble;
    bool m_batch;
    bool m_nbs;
    int m_slot;

    bool m_active;                      //!< A message is being received
    Message m_current;
    std::vector<uint8_t> m_currentBits;
    int m_blocks;

    uint32_t m_codewords;
    uint32_t m_codewordsCorrected;
    uint32_t m_codewordsUncorrectable;
    uint32_t m_transmissions;
    uint32_t m_messageCount;

    std::deque<Message> m_messages;

    int preambleHit() const;
    void startTransmission(int index, bool batch);
    void startNonBatterySaver();
    void endTransmission(bool truncated);
    void process();
    void clearBuffer();
    //!< Golay codeword of the 46 symbols at a position of m_buf
    uint32_t wordAt(int pos) const;
    //!< Soft correlation of the 46 symbols at a position of m_buf with a codeword
    float correlation(int pos, uint32_t codeword) const;
    //!< The 46 symbols at a position of m_buf are pairs of equal symbols, not a comma
    bool isWord(int pos) const;
    //!< Decodes the word at a position of m_buf. False if the symbols are not a word
    bool decodeWord(int pos, RadioPagingGsc::GolayResult& result) const;
    bool processSeekStart();
    bool processSeekAddress();
    bool processNext();
    bool processClassify();
    bool processData();
    bool parseAddress(int pos, Message& message, int& errors1, int& errors2) const;
    void finishMessage(bool truncated);
    void trim();
};

#endif // MODEMRADIOPAGING_RADIOPAGINGGSCDECODER_H
