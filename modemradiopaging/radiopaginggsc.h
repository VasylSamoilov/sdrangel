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

#ifndef MODEMRADIOPAGING_RADIOPAGINGGSC_H
#define MODEMRADIOPAGING_RADIOPAGINGGSC_H

#include <cstdint>
#include <string>
#include <vector>

#include "export.h"

// Sequential Golay paging format: codes, encoder and message interpretation.
//
// The signal is 600 baud two level FSK, a binary 1 is a positive deviation. Every bit
// is sent twice, so a Golay(23,12) codeword takes 46 symbols (bit 0 first, data in
// bits 11-0 and check bits in bits 22-12). A comma is a run of alternating bits.
//
// Transmission (battery saver mode):
//   comma of 28 bits, 18 codewords of the preamble value
//   comma of 28 bits, start code, 1 bit, complement of the start code
//   per message: comma of 28 bits, word 1, 1 bit, word 2, then
//     tone only      comma of 968 bits
//     voice          comma of 28 bits, activation code, 1 bit, complement of the activation
//                    code (voice audio follows)
//     data           one or more blocks of 1 + 120 bits
// The 10 preamble values are selected by (index digit + group digit 0) mod 10. In batch
// mode the preamble codewords are complemented. A batch holds 16 messages (unused slots
// are filled with commas of 121 bits); in an extended batch of up to 32 messages the start
// code is repeated before the 17th message.
//
// Word 1 selects two digits of the address (the table has 50 entries, the second word
// carries a +50 offset for groups 50-99), word 2 the last three digits. The address
// number 1-4 is carried by complementing word 1 (number bit 1) and word 2 (number bit 0).
// An address is written as 6 digits: index, group (2 digits) and 3 address digits.
//
// A data block is 8 BCH(15,7) codewords of 7 bits, interleaved bit by bit into 120 bits.
// Words 0-5 and the low 6 bits of word 6 carry 48 payload bits, bit 6 of word 6 is the
// continuation flag and word 7 is a checksum (sum of the 15-bit codewords 0-6, low 7 bits).
// The payload bits are 8 six bit characters (alphanumeric) or 12 four bit characters
// (numeric), least significant bit first. Alphanumeric messages are padded with 0x3e and
// numeric messages with 0xa. Whether a block holds alphanumeric or numeric characters is
// not signalled.
//
// Message bits in a decoded message are the payload bits of all blocks, first transmitted
// bit first, packed 8 per byte with the first bit in the most significant bit.
class MODEMRADIOPAGING_API RadioPagingGsc
{
public:
    static const uint16_t m_startCode = 713;
    static const uint16_t m_activationCode = 2563;
    static const int m_preambleValues = 10;
    static const int m_commaBits = 28;
    static const int m_codewordBits = 46;               //!< Duplicated Golay codeword
    static const int m_preambleCodewords = 18;
    static const int m_blockBits = 121;                 //!< 1 bit + 120 interleaved bits
    static const int m_toneCommaBits = 968;
    static const int m_slotBits = 121;                  //!< Comma filling an unused batch slot
    static const int m_payloadBitsPerBlock = 48;
    static const int m_maxBlocks = 32;
    static const int m_maxMessageBits = 48 * 32;
    static const int m_batchSlots = 16;
    static const int m_extendedBatchSlots = 32;
    static const int m_nonBatterySaverBits = 752;       //!< Preamble of repeating 1100

    enum MessageType {
        Tone,           //!< Address only
        Voice,          //!< Address and activation code, voice audio follows
        Numeric,
        Alphanumeric,
        Data            //!< Decoded only: alphanumeric or numeric, not signalled
    };

    enum Mode {
        Auto,               //!< Single for one message, otherwise Batch
        Single,             //!< One message after its own preamble
        Batch,              //!< Complemented preamble, 16 slots, 32 if more than 16 messages
        NonBatterySaver     //!< One message after a 1100 pattern, no codeword preamble or start code
    };

    struct TxMessage
    {
        std::string m_address;      //!< 6 digits: index, group, 3 address digits
        int m_addressNumber = 1;    //!< 1-4
        MessageType m_type = Alphanumeric;
        std::string m_text;         //!< Numeric: 0-9 A-H J L N P R U space - = *  Alphanumeric: upper case ASCII
    };

    struct EncodeOptions
    {
        Mode m_mode = Auto;
    };

    struct EncodeResult
    {
        bool m_ok = false;
        std::string m_error;
        std::vector<uint8_t> m_bits;    //!< One bit per element in transmission order, 1 = positive deviation
        int m_preamble = -1;            //!< Preamble value index used, -1 if none
        int m_slots = 0;                //!< Batch slots in use (16 or 32 for a batch, else the number of messages)
    };

    struct GolayResult
    {
        uint32_t m_word;        //!< Corrected 23-bit codeword
        uint16_t m_data;        //!< 12 data bits of the corrected codeword
        int m_bitErrors;        //!< Bits corrected, 0-3. More than 3 errors decode to a wrong codeword
    };

    struct BchResult
    {
        uint16_t m_word;        //!< Corrected 15-bit codeword (unchanged if uncorrectable)
        uint8_t m_data;         //!< 7 data bits of the corrected codeword
        int m_bitErrors;        //!< Bits corrected, 0-2, -1 if uncorrectable
    };

    struct Interpretation
    {
        std::string m_type;         //!< alphanumeric, numeric or tone_only
        std::string m_text;
        int m_score = 0;            //!< Content score of the winner, higher is better, can be negative
        std::string m_altType;      //!< The other candidate, empty if none
        std::string m_altText;
        int m_altScore = 0;
    };

    //!< Golay(23,12), generator x^11 + x^10 + x^6 + x^5 + x^4 + x^2 + 1. Data in bits 11-0
    static uint32_t golayEncode(uint16_t data);
    //!< Corrects up to three bit errors
    static GolayResult golayDecode(uint32_t word);
    //!< Remainder of a 23-bit word divided by the generator, 0 for a codeword
    static uint32_t golaySyndrome(uint32_t word);
    //!< BCH(15,7), generator x^8 + x^4 + x^2 + x + 1. Data in bits 6-0
    static uint16_t bchEncode(uint8_t data);
    //!< Corrects up to two bit errors
    static BchResult bchDecode(uint16_t word);
    //!< Remainder of a 15-bit word divided by the generator, 0 for a codeword
    static uint32_t bchSyndrome(uint16_t word);

    //!< Preamble value of an index 0-9
    static uint16_t preambleValue(int index);
    //!< Index of a preamble value, -1 if it is none
    static int preambleIndex(uint16_t value);
    //!< Words 1 and 2 of a 6 digit address and the preamble index. Fails for non digits and
    //!< for the address digit combinations that are not allowed
    static bool encodeAddress(const std::string& address, int& preamble, uint16_t& word1, uint16_t& word2, std::string *error = nullptr);
    //!< Inverse of encodeAddress(). Needs the preamble index (-1 if unknown: the index digit is then '?')
    static bool decodeAddress(uint16_t word1, uint16_t word2, int preamble, std::string& address);
    //!< Index of a word 1 value in the table of 50 values, -1 if it is none
    static int word1Index(uint16_t word1);
    //!< Seventh digit of the 7 digit functional address: voice 1-4, alphanumeric and numeric 5-8, tone 9 0 3 4
    static char suffixDigit(MessageType type, int addressNumber);

    //!< Build the transmission for the messages in the given order
    static EncodeResult encode(const std::vector<TxMessage>& messages, const EncodeOptions& options);
    static EncodeResult encode(const std::vector<TxMessage>& messages);

    //!< Message bits as stored in a decoded message: packed 8 per byte, first bit in the
    //!< most significant bit of byte 0
    static std::string decodeNumeric(const std::vector<uint8_t>& packed, int nbBits);
    static std::string decodeAlphanumeric(const std::vector<uint8_t>& packed, int nbBits);
    static int scoreNumeric(const std::vector<uint8_t>& packed, int nbBits);
    static int scoreAlphanumeric(const std::vector<uint8_t>& packed, int nbBits);
    //!< Decide between numeric and alphanumeric by content. No bits means tone only
    static Interpretation interpret(const std::vector<uint8_t>& packed, int nbBits);

    //!< Replace characters below 0x20 and 0x7f by <0xNN>
    static std::string escapeControl(const std::string& text);

    //!< Bits of a transmission of an alternating comma of a given length, first bit given
    static void appendComma(std::vector<uint8_t>& bits, int nbBits, int firstBit);
    //!< Golay codeword, LSB first, every bit twice
    static void appendDuplicated(std::vector<uint8_t>& bits, uint32_t codeword);
    //!< Interleaved block of 8 BCH codewords preceded by its separator bit
    static void appendBlock(std::vector<uint8_t>& bits, const uint16_t codewords[8]);
    //!< Golay codeword (23 bits) of 46 symbols: first bit of every pair
    static uint32_t resolveDuplicated(const uint8_t *bits);
    //!< 8 BCH codewords of a block, bits points after the separator bit
    static void deinterleaveBlock(const uint8_t *bits, uint16_t codewords[8]);
};

#endif // MODEMRADIOPAGING_RADIOPAGINGGSC_H
