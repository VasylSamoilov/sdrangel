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

#ifndef MODEMRADIOPAGING_RADIOPAGINGPOCSAGDECODER_H
#define MODEMRADIOPAGING_RADIOPAGINGPOCSAGDECODER_H

#include <cstdint>
#include <deque>
#include <vector>

#include "export.h"

// POCSAG bit stream decoder for one polarity. It hunts for sync on its own, using
// nothing but the bits it is fed: a preamble followed by the sync codeword (up to two
// bit errors in the sync), then batch after batch for as long as each batch is
// followed by another sync codeword.
//
// Symbols are the output of an FSK demodulator, 1 = positive deviation. For normal
// polarity (CCIR Rec. 584) a binary 1 is a negative deviation, inverted polarity is
// the opposite. A decoder instance handles one speed; run one per speed and polarity.
class MODEMRADIOPAGING_API RadioPagingPocsagDecoder
{
public:
    enum Polarity {
        Normal,
        Inverted
    };

    enum State {
        Hunting,        //!< Looking for preamble and sync
        InBatch,        //!< Receiving the codewords of a batch
        AwaitSync       //!< Receiving the sync codeword that should follow a batch
    };

    struct Message
    {
        uint32_t m_address = 0;             //!< 21-bit address, frame included
        int m_function = 0;                 //!< 0-3
        std::vector<uint8_t> m_rawData;     //!< Message bits, packed 8 per byte, first bit in the MSB
        int m_rawBits = 0;                  //!< Number of message bits, a multiple of 20; 0 = tone only
        int m_errorsCorrected = 0;          //!< Bits corrected in the message's codewords
        int m_errorsUncorrectable = 0;      //!< Codewords that could not be corrected
        bool m_truncated = false;           //!< Sync was lost before the message ended
        int m_batch = 0;                    //!< Batch of the transmission the address was in, from 0
        Polarity m_polarity = Normal;
    };

    struct Status
    {
        State m_state;
        Polarity m_polarity;
        int m_batches;                      //!< Batches received in the current transmission
        uint32_t m_codewords;               //!< Codewords received since reset
        uint32_t m_codewordsCorrected;      //!< ... with bit errors that were corrected
        uint32_t m_codewordsUncorrectable;
        uint32_t m_transmissions;           //!< Preamble and sync acquisitions since reset
    };

    explicit RadioPagingPocsagDecoder(Polarity polarity = Normal);

    void reset();
    void setPolarity(Polarity polarity) { m_polarity = polarity; }
    //!< Feed one symbol: 1 = positive deviation, 0 = negative deviation
    void feed(int symbol);

    bool isSynced() const { return m_state != Hunting; }
    Status getStatus() const;
    bool hasMessage() const { return !m_messages.empty(); }
    Message takeMessage();

    //!< Alternating bits needed before an exact / a bit error corrected sync codeword
    static const int m_minPreambleBits = 16;
    static const int m_minPreambleBitsCorrected = 32;
    static const int m_maxSyncErrors = 2;
    static const int m_maxMessageBits = 20 * 400;

private:
    Polarity m_polarity;
    State m_state;
    uint64_t m_reg;
    int m_regBits;
    uint32_t m_word;
    int m_wordBits;
    int m_slot;
    int m_batch;
    int m_batches;

    bool m_active;
    Message m_current;
    std::vector<uint8_t> m_currentBits;

    uint32_t m_codewords;
    uint32_t m_codewordsCorrected;
    uint32_t m_codewordsUncorrectable;
    uint32_t m_transmissions;

    std::deque<Message> m_messages;

    int alternatingRun() const;
    void processCodeword(uint32_t word);
    void appendMessageBits(uint32_t data, bool corrected, bool uncorrectable);
    void finishMessage(bool truncated);
};

#endif // MODEMRADIOPAGING_RADIOPAGINGPOCSAGDECODER_H
