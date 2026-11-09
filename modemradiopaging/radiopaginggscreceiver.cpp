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

#include "radiopaginggscreceiver.h"

const int RadioPagingGscReceiver::m_baud;

RadioPagingGscReceiver::RadioPagingGscReceiver() :
    m_normalEnabled(true),
    m_invertedEnabled(true),
    m_normal(RadioPagingGscDecoder::Normal),
    m_inverted(RadioPagingGscDecoder::Inverted)
{
    create(38400.0);
}

void RadioPagingGscReceiver::create(double sampleRate)
{
    m_demod.create(sampleRate, m_baud, 2);
    reset();
}

void RadioPagingGscReceiver::reset()
{
    m_demod.reset();
    m_normal.reset();
    m_inverted.reset();
    m_messages.clear();
}

void RadioPagingGscReceiver::setNonBatterySaver(bool enable)
{
    m_normal.setNonBatterySaver(enable);
    m_inverted.setNonBatterySaver(enable);
}

void RadioPagingGscReceiver::process(float sample)
{
    RadioPagingFskDemod::Symbol symbol;

    if (!m_demod.process(sample, symbol)) {
        return;
    }

    if (m_normalEnabled) {
        m_normal.feedSoft(symbol.m_soft);
    }
    if (m_invertedEnabled) {
        m_inverted.feedSoft(symbol.m_soft);
    }

    bool synced = (m_normalEnabled && m_normal.isSynced()) || (m_invertedEnabled && m_inverted.isSynced());

    m_demod.setLocked(synced);
    collect();
}

void RadioPagingGscReceiver::flush()
{
    m_normal.flush();
    m_inverted.flush();
    m_demod.setLocked(false);
    collect();
}

void RadioPagingGscReceiver::collect()
{
    while (m_normal.hasMessage()) {
        m_messages.push_back(m_normal.takeMessage());
    }
    while (m_inverted.hasMessage()) {
        m_messages.push_back(m_inverted.takeMessage());
    }
}

RadioPagingGscDecoder::Message RadioPagingGscReceiver::takeMessage()
{
    RadioPagingGscDecoder::Message message = m_messages.front();
    m_messages.pop_front();
    return message;
}

RadioPagingGscReceiver::Status RadioPagingGscReceiver::getStatus() const
{
    Status status;

    status.m_locked = (m_normalEnabled && m_normal.isSynced()) || (m_invertedEnabled && m_inverted.isSynced());
    status.m_normal = m_normal.getStatus();
    status.m_inverted = m_inverted.getStatus();
    status.m_deviation = m_demod.getDeviation();

    return status;
}
