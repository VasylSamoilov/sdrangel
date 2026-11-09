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

#include "radiopagingpocsagreceiver.h"

const int RadioPagingPocsagReceiver::m_preambleLockBits;

RadioPagingPocsagReceiver::RadioPagingPocsagReceiver() :
    m_baud(1200),
    m_normalEnabled(true),
    m_invertedEnabled(true),
    m_normal(RadioPagingPocsagDecoder::Normal),
    m_inverted(RadioPagingPocsagDecoder::Inverted)
{
    create(38400.0, 1200);
}

void RadioPagingPocsagReceiver::create(double sampleRate, int baud)
{
    m_baud = baud;
    m_demod.create(sampleRate, baud, 2);
    reset();
}

void RadioPagingPocsagReceiver::reset()
{
    m_demod.reset();
    m_normal.reset();
    m_inverted.reset();
    m_lastBit = -1;
    m_reversals = 0;
    m_preambleLocked = false;
    m_bitsSincePreamble = 0;
    m_messages.clear();
    m_dedup.reset();
    m_time = 0.0;
}

void RadioPagingPocsagReceiver::process(float sample)
{
    RadioPagingFskDemod::Symbol symbol;

    if (!m_demod.process(sample, symbol)) {
        return;
    }

    m_time += 1.0 / m_baud;

    // Reversals of the preamble lock the demodulator after a part of the preamble
    if ((m_lastBit >= 0) && (symbol.m_value != m_lastBit)) {
        m_reversals++;
    } else {
        m_reversals = 0;
    }
    m_lastBit = symbol.m_value;

    if (m_reversals >= m_preambleLockBits)
    {
        m_preambleLocked = true;
        m_bitsSincePreamble = 0;
    }

    if (m_normalEnabled) {
        m_normal.feed(symbol.m_value);
    }
    if (m_invertedEnabled) {
        m_inverted.feed(symbol.m_value);
    }

    bool synced = (m_normalEnabled && m_normal.isSynced()) || (m_invertedEnabled && m_inverted.isSynced());

    // The preamble lock is handed over to the decoders once a batch is being received. The
    // preamble is followed by the sync codeword, so the lock is kept for one codeword after
    // the last reversal and released if no sync codeword came
    if (m_preambleLocked)
    {
        if (synced) {
            m_preambleLocked = false;
        } else if ((m_reversals < m_preambleLockBits) && (++m_bitsSincePreamble > m_preambleLockBits)) {
            m_preambleLocked = false;
        }
    }

    m_demod.setLocked(m_preambleLocked || synced);

    while (m_normal.hasMessage()) {
        collect(m_normal.takeMessage());
    }
    while (m_inverted.hasMessage()) {
        collect(m_inverted.takeMessage());
    }
}

// The same message to the same pager (address and function) again within the window is a
// duplicate. The other polarity is another network
void RadioPagingPocsagReceiver::collect(RadioPagingPocsagDecoder::Message message)
{
    std::string address = std::to_string(message.m_address) + (char) ('A' + (message.m_function & 3));
    int polarity = (message.m_polarity == RadioPagingPocsagDecoder::Inverted) ? 1 : 0;

    message.m_duplicate = m_dedup.check(polarity, address, message.m_rawData, message.m_rawBits, m_time);
    m_messages.push_back(message);
}

RadioPagingPocsagDecoder::Message RadioPagingPocsagReceiver::takeMessage()
{
    RadioPagingPocsagDecoder::Message message = m_messages.front();
    m_messages.pop_front();
    return message;
}

RadioPagingPocsagReceiver::Status RadioPagingPocsagReceiver::getStatus() const
{
    Status status;

    status.m_locked = m_preambleLocked || m_normal.isSynced() || m_inverted.isSynced();
    status.m_preamble = m_reversals >= m_preambleLockBits;
    status.m_normal = m_normal.getStatus();
    status.m_inverted = m_inverted.getStatus();
    status.m_deviation = m_demod.getDeviation();

    return status;
}
