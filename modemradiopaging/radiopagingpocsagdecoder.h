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
#include "radiopagingberwindow.h"

// POCSAG bit stream decoder for one polarity. It hunts for sync on its own, using
// nothing but the bits it is fed.
//
// Synchronisation uses the codeword structure only. The preamble trains the bit clock of
// the demodulator and is not counted. A sync codeword with up to 2 bit errors starts a
// candidate batch, which is buffered. A batch is decoded when
//   - its sync codeword is confirmed by another sync codeword one batch (544 bits) away,
//     before or after it, or
//   - the batch is valid by itself: all 16 codewords decode within the correction
//     capacity of the code (this covers single batch transmissions).
// A transmission therefore needs no preamble to be acquired: a receiver that starts
// listening during a transmission synchronises on two consecutive batches. Nothing is
// predicted from earlier transmissions. While a candidate is not confirmed, a sync
// codeword found at another position replaces it.
//
// A message ends with an idle or address codeword, or after two successive codewords
// that cannot be decoded. A single undecodable codeword is kept in the message (its 20
// message bits as received) and counted as uncorrectable.
//
// Symbols are the output of an FSK demodulator, 1 = positive deviation. For normal
// polarity a binary 1 is a negative deviation, inverted polarity is the opposite. A
// decoder instance handles one speed; run one per speed and polarity.
class MODEMRADIOPAGING_API RadioPagingPocsagDecoder
{
public:
    enum Polarity {
        Normal,
        Inverted
    };

    enum State {
        Hunting,        //!< Looking for a sync codeword
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
        bool m_truncated = false;           //!< Ended by sync loss or two undecodable codewords
        int m_batch = 0;                    //!< Batch of the transmission the address was in, from 0
        Polarity m_polarity = Normal;
        bool m_duplicate = false;           //!< Set by the receiver: the same message was received shortly before
    };

    struct Status
    {
        State m_state;
        Polarity m_polarity;
        bool m_confirmed;                   //!< The current batch's sync codeword is confirmed
        int m_batches;                      //!< Batches decoded in the current transmission
        uint32_t m_codewords;               //!< Codewords decoded since reset
        uint32_t m_codewordsCorrected;      //!< ... with bit errors that were corrected
        uint32_t m_codewordsUncorrectable;
        uint32_t m_transmissions;           //!< Transmissions with at least one decoded batch
        uint32_t m_candidatesDropped;       //!< Sync candidates whose batch was not decoded
        RadioPagingBerWindow m_ber;         //!< Of the current (or last) transmission
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

    //!< Bit errors corrected in a sync codeword, the correction capacity of the code
    static const int m_maxSyncErrors = 2;
    static const int m_maxMessageBits = 20 * 400;

private:
    Polarity m_polarity;
    State m_state;
    uint32_t m_reg;                     //!< Last 32 bits, newest in bit 0
    int m_regBits;
    uint32_t m_word;
    int m_wordBits;
    int m_slot;
    uint32_t m_batchWords[16];          //!< Codewords of the batch being received
    bool m_leadingConfirmed;            //!< The sync codeword of the current batch is confirmed
    int m_batch;                        //!< Batches decoded in the current transmission

    bool m_active;
    Message m_current;
    std::vector<uint8_t> m_currentBits;
    int m_uncorrectableRun;             //!< Successive undecodable codewords

    uint32_t m_codewords;
    uint32_t m_codewordsCorrected;
    uint32_t m_codewordsUncorrectable;
    uint32_t m_transmissions;
    uint32_t m_candidatesDropped;
    RadioPagingBerWindow m_ber;

    std::deque<Message> m_messages;

    bool syncMatch() const;
    void startCandidate();
    bool batchValid() const;
    void decodeBatch();
    void endTransmission();
    void processCodeword(uint32_t word);
    void appendMessageBits(uint32_t data);
    void finishMessage(bool truncated);
};

#endif // MODEMRADIOPAGING_RADIOPAGINGPOCSAGDECODER_H
