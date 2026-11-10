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

#ifndef MODEMRADIOPAGING_RADIOPAGINGFLEX_H
#define MODEMRADIOPAGING_RADIOPAGINGFLEX_H

#include <cstdint>
#include <string>
#include <vector>

#include "export.h"

// FLEX transmission format: codeword helpers and frame builder.
//
// A frame is a sync part sent at 1600 baud with 2 levels (bit sync, a code naming the
// frame mode, the frame information word) followed by a second sync (S2) and the data
// part at the rate and number of levels of the frame mode. The data part is 11 blocks
// of 8 codewords for each phase. Codewords of a block are interleaved: the block is
// sent as 32 bytes, byte i holding bit i (first transmitted bit first) of codeword 0
// to 7 in its bits 7 to 0.
//
// Codeword: 21 data bits d0 to d20 (d0 sent first), 10 check bits, even parity. The
// words of this class are in transmission order, bit 31 is sent first, so the
// BCH code word of RadioPagingBCH is built from the data bits in reverse order.
//
// Modes and phases:
//   1600/2  1600 baud, 2 levels, phase A
//   3200/2  3200 baud, 2 levels, phases A and C, bits alternate A, C, A, C
//   3200/4  1600 baud, 4 levels, phases A and C, a symbol is (A bit, C bit)
//   6400/4  3200 baud, 4 levels, phases A, B, C, D, symbols alternate (A,B) and (C,D)
// Symbols: 2 levels 0 or 1, 4 levels 0 to 3. Symbol 1 (2 levels) and 3 (4 levels) are
// the highest frequency. Dibits are sent most significant bit first and mapped with
// a Gray code (00 -> 0, 01 -> 1, 11 -> 2, 10 -> 3). Inverted polarity mirrors the
// levels (symbol s becomes levels - 1 - s) of the whole frame including the sync part.
class MODEMRADIOPAGING_API RadioPagingFlex
{
public:
    enum Mode {
        Mode1600_2,
        Mode3200_2,
        Mode3200_4,
        Mode6400_4
    };

    enum MessageType {
        Tone,           //!< Address only
        Numeric,        //!< 0-9 U space - ] [ . (and _ as - and u as U)
        Alphanumeric    //!< 7-bit characters
    };

    static const int m_wordsPerBlock = 8;
    static const int m_blocksPerFrame = 11;
    static const int m_wordsPerFrame = 88;      //!< Per phase
    static const int m_maxPhases = 4;
    static const int m_syncBaud = 1600;
    static const int m_maxCollapse = 7;
    static const int m_maxAlphaChars = 248;     //!< Characters of one alphanumeric message
    static const int m_maxNumericChars = 41;
    static const int m_maxPriorityWords = 15;   //!< Priority address words of a phase
    static const uint64_t m_shortMin = 1;
    static const uint64_t m_shortMax = 1933312;
    static const uint64_t m_longMin = 2101249;
    static const uint64_t m_longMax = 4297068542ULL;

    // Message of one phase of a frame
    struct PhaseMessage
    {
        uint64_t m_capcode = 0;
        MessageType m_type = Alphanumeric;
        std::string m_text;
        bool m_priority = false;    //!< Address in the priority part of the address field
        int m_number = 0;           //!< Message number 0-63, sent in alphanumeric messages
    };

    struct PhaseParams
    {
        Mode m_mode = Mode1600_2;
        int m_phase = 0;            //!< Phase index: 0 to phaseCount(mode) - 1
        int m_collapse = 0;         //!< 0-7, sent in the block information word
        int m_carryOn = 0;          //!< 0-3, sent in the block information word
        std::vector<uint32_t> m_extraBiw;   //!< Block information words 2 to 4 (up to 3, as transmitted): before the address field
    };

    struct PhaseResult
    {
        bool m_ok = false;
        std::string m_error;
        std::vector<uint32_t> m_words;      //!< 88 interleaved words: 11 blocks of 32 bytes, big endian
        std::vector<bool> m_packed;         //!< Per input message, true if placed in the phase
        int m_addressWords = 0;             //!< Words of the address field
    };

    struct FrameParams
    {
        Mode m_mode = Mode1600_2;
        unsigned m_cycle = 0;       //!< 0-14
        unsigned m_frame = 0;       //!< 0-127
        unsigned m_traffic = 0;     //!< 4 low traffic flags of the frame information word
        bool m_roaming = false;     //!< Roaming network bit of the frame information word
    };

    struct FrameBytes
    {
        std::vector<uint8_t> m_sync;    //!< Bit sync, mode code, frame information word: 18 bytes at 1600 baud 2 levels
        std::vector<uint8_t> m_data;    //!< Second sync and phase data at the rate of the mode
    };

    struct SymbolBlock
    {
        int m_baud = 0;
        int m_levels = 0;                   //!< 2 or 4
        std::vector<uint8_t> m_symbols;     //!< 0 to m_levels - 1, 0 = lowest frequency
    };

    static int phaseCount(Mode mode);
    static int baud(Mode mode);
    static int levels(Mode mode);
    static int bitRate(Mode mode);
    static const char *modeName(Mode mode);

    //!< Word with the 21 data bits (d0 = bit 0) of a data word: reversed and extended with check bits and parity
    static uint32_t encodeWord(uint32_t data);
    //!< Data word (d0 = bit 0) of a transmitted word, no error correction
    static uint32_t wordData(uint32_t word);
    //!< Sets the 4-bit checksum in bits 3-0 of an information word (bits 3-0 must be 0)
    static uint32_t addChecksum(uint32_t data);
    static bool checksumValid(uint32_t data);

    static uint32_t frameInfoWord(unsigned cycle, unsigned frame, unsigned roaming, unsigned repeat, unsigned traffic);
    //!< Simulcast system ID words (STD-43A Table 3.7.2-1, 6.1.1): block information words 000 (local
    //!< channel ID 9 bits, coverage zone 5 bits) and 111 (country code 10 bits, traffic management flags 4 bits)
    static uint32_t ssid1Word(unsigned localId, unsigned coverageZone);
    static uint32_t ssid2Word(unsigned countryCode, unsigned trafficFlags);
    //!< Phase that carries the SSID words of a frame (STD-43A 6.1.1.3): 4 phases, the phase is the frame
    //!< modulo 4; 2 phases (a and c), frames 0 and 1 in the first, 2 and 3 in the second; 1 phase, the only one
    static int ssidPhase(Mode mode, unsigned frame);
    //!< addressStart: number of block information words minus 1, vectorStart: word index of the first vector
    static uint32_t blockInfoWord(unsigned priorityWords, unsigned addressStart, unsigned vectorStart, unsigned carryOn, unsigned collapse);

    static bool isShortAddress(uint64_t capcode);
    static bool isLongAddress(uint64_t capcode);
    static bool isValidAddress(uint64_t capcode);
    static uint32_t shortAddressWord(uint32_t capcode);
    static bool longAddressWords(uint64_t capcode, uint32_t words[2]);
    //!< Capcode of a short address word (data bits) or of a pair of long address words (data bits)
    static uint64_t shortAddressCapcode(uint32_t data);
    static bool longAddressCapcode(uint32_t data1, uint32_t data2, uint64_t& capcode);

    static uint32_t alphaVector(unsigned start, unsigned words);
    static uint32_t numericVector(unsigned start, unsigned words, unsigned kBits);
    static const unsigned m_vectorTone = 2;
    static const unsigned m_vectorNumeric = 3;
    static const unsigned m_vectorAlpha = 5;

    //!< Fill a phase with idle words: alternating all ones and all zeros, all zeros in the
    //!< phases carrying the least significant bit of 4-level symbols
    static void fillIdle(uint32_t *words, int phase, Mode mode);
    //!< Interleave the 8 words of a block in place (see class description)
    static void interleaveBlock(uint32_t *words);
    static void deinterleaveBlock(uint32_t *words);

    //!< Words of address, vector and message of a message (long addresses: the first message
    //!< word is in the second vector word). -1 and error if the message cannot be sent
    static int messageCost(const PhaseMessage& message, std::string *error = nullptr);
    //!< Check the capcode and text of a message
    static bool validateMessage(uint64_t capcode, MessageType type, const std::string& text, std::string& error);

    //!< Place messages (priority addresses first, then others, tone only addresses last) in
    //!< a phase and interleave. Messages that do not fit are left out
    static PhaseResult buildPhase(const PhaseParams& params, const std::vector<PhaseMessage>& messages);

    //!< Low traffic flags t0 (phase A) to t3 (phase D) for the number of address field words of
    //!< each phase (one block information word): set if the address field ends in block 0
    static unsigned lowTrafficFlags(const std::vector<int>& addressWords, int phases);

    //!< Sync part, frame information word, second sync and the phases (one vector of
    //!< m_wordsPerFrame interleaved words for each phase of the mode)
    static FrameBytes assemble(const FrameParams& params, const std::vector<std::vector<uint32_t>>& phases);
    //!< Symbols of the sync part (1600 baud, 2 levels) and of the data part (rate of the mode)
    static std::vector<SymbolBlock> toSymbols(const FrameBytes& bytes, Mode mode, bool inverted);
    //!< Symbols of bytes (most significant bit first) for the given number of levels
    static std::vector<uint8_t> bytesToSymbols(const std::vector<uint8_t>& bytes, int levels, bool inverted);

    //!< Emergency re-synchronization (STD-43A 3.2.1, Table 3.2.1-1): BS, Ar, inverted BS and
    //!< inverted Ar repeated at 1600 baud 2 levels, for the given number of bits (the last
    //!< cycle may be cut)
    static SymbolBlock ers(int bits, bool inverted);
    //!< Bits at 1600 baud the re-synchronization lasts at least: the battery saving cycle of a
    //!< pager with the collapse value, 2^collapse frames of 1.875 s (STD-43A 3.2.1)
    static int ersMinBits(int collapse);
    static const int m_frameMs = 1875;          //!< Frame duration, milliseconds
    static const int m_bitsPerFrame = 3000;     //!< At 1600 baud

    static const char m_numericChars[17];   //!< Index = 4-bit value
};

#endif // MODEMRADIOPAGING_RADIOPAGINGFLEX_H
