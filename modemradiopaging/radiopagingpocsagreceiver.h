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

#ifndef MODEMRADIOPAGING_RADIOPAGINGPOCSAGRECEIVER_H
#define MODEMRADIOPAGING_RADIOPAGINGPOCSAGRECEIVER_H

#include <deque>

#include "export.h"
#include "radiopagingfskdemod.h"
#include "radiopagingpocsagdecoder.h"

// POCSAG receiver for one speed: FM discriminator samples to messages of both polarities.
//
// The preamble (reversals 1010...) is there to train the bit clock. The demodulator
// starts with acquisition loop gains; once it has received a part of the preamble
// (m_preambleLockBits reversals) it is locked: timing and level tracking switch to
// tracking gains, trained on the reversals. Only a part of the preamble is needed, a
// receiver that starts during the preamble locks just the same. The lock is held while a
// decoder is synchronised and released when neither the preamble nor a batch is being
// received. Word and batch synchronisation are the decoders' own (see
// RadioPagingPocsagDecoder): they do not depend on the preamble, so a transmission
// joined after its preamble is still acquired from its batches.
class MODEMRADIOPAGING_API RadioPagingPocsagReceiver
{
public:
    //!< Reversals that lock the demodulator: one codeword, the time a pager needs to see a
    //!< whole codeword of preamble
    static const int m_preambleLockBits = 32;

    struct Status
    {
        bool m_locked;                              //!< Demodulator locked (preamble or batches)
        bool m_preamble;                            //!< Receiving the preamble
        RadioPagingPocsagDecoder::Status m_normal;
        RadioPagingPocsagDecoder::Status m_inverted;
        float m_deviation;                          //!< Measured deviation, discriminator units
    };

    RadioPagingPocsagReceiver();

    //!< sampleRate in Hz, baud 512, 1200 or 2400
    void create(double sampleRate, int baud);
    void reset();
    void setPolarities(bool normal, bool inverted) { m_normalEnabled = normal; m_invertedEnabled = inverted; }
    int getBaud() const { return m_baud; }

    void process(float sample);

    bool hasMessage() const { return !m_messages.empty(); }
    RadioPagingPocsagDecoder::Message takeMessage();
    Status getStatus() const;

private:
    int m_baud;
    bool m_normalEnabled;
    bool m_invertedEnabled;
    RadioPagingFskDemod m_demod;
    RadioPagingPocsagDecoder m_normal;
    RadioPagingPocsagDecoder m_inverted;
    int m_lastBit;
    int m_reversals;                    //!< Consecutive reversals
    bool m_preambleLocked;
    int m_bitsSincePreamble;            //!< Bits since the last run of m_preambleLockBits reversals
    std::deque<RadioPagingPocsagDecoder::Message> m_messages;
};

#endif // MODEMRADIOPAGING_RADIOPAGINGPOCSAGRECEIVER_H
