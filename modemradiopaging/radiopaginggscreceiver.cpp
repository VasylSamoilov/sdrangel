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
    m_inverted(RadioPagingGscDecoder::Inverted),
    m_nbsPolarity(NbsUndecided)
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
    m_nbsPolarity = NbsUndecided;
    m_nbsPending[0].clear();
    m_nbsPending[1].clear();
}

void RadioPagingGscReceiver::setNonBatterySaver(bool enable)
{
    m_normal.setNonBatterySaver(enable);
    m_inverted.setNonBatterySaver(enable);
}

void RadioPagingGscReceiver::setVoiceTimeout(double seconds)
{
    m_normal.setVoiceTimeout(seconds);
    m_inverted.setVoiceTimeout(seconds);
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

    // In the voice state there are no symbols to track: acquisition timing gains, so the
    // turn-off word or a new preamble is picked up at once, and the levels of the data
    // before are kept
    bool voice = (m_normalEnabled && m_normal.inVoice()) || (m_invertedEnabled && m_inverted.inVoice());

    m_demod.setLocked(isLocked() && !voice);
    m_demod.setHoldLevels(voice);
    collect();
}

bool RadioPagingGscReceiver::isLocked() const
{
    return (m_normalEnabled && m_normal.isSynced()) || (m_invertedEnabled && m_inverted.isSynced());
}

void RadioPagingGscReceiver::flush()
{
    m_normal.flush();
    m_inverted.flush();
    m_demod.setLocked(false);
    m_demod.setHoldLevels(false);
    collect();
}

void RadioPagingGscReceiver::collect()
{
    takeFrom(m_normal);
    takeFrom(m_inverted);

    bool nbsActive = (m_normalEnabled && m_normal.inNonBatterySaver()) || (m_invertedEnabled && m_inverted.inNonBatterySaver());

    if (!nbsActive) {
        endNonBatterySaver();
    }
}

void RadioPagingGscReceiver::takeFrom(RadioPagingGscDecoder& decoder)
{
    while (decoder.hasMessage())
    {
        RadioPagingGscDecoder::Message message = decoder.takeMessage();

        // With one polarity only there is nothing to decide
        if (message.m_nonBatterySaver && m_normalEnabled && m_invertedEnabled) {
            nonBatterySaverMessage(message);
        } else {
            m_messages.push_back(message);
        }
    }
}

int RadioPagingGscReceiver::polarityEvidence(const RadioPagingGscDecoder::Message& message)
{
    if (message.m_type == RadioPagingGsc::Voice) {
        return 1;
    }
    if (message.m_checksumPassed > message.m_checksumErrors) {
        return 1;
    }
    if (message.m_checksumErrors > message.m_checksumPassed) {
        return -1;
    }

    return 0;
}

void RadioPagingGscReceiver::nonBatterySaverMessage(const RadioPagingGscDecoder::Message& message)
{
    int own = (message.m_polarity == RadioPagingGscDecoder::Inverted) ? 1 : 0;

    if (m_nbsPolarity == NbsUndecided)
    {
        int evidence = polarityEvidence(message);

        if (evidence != 0)
        {
            int right = (evidence > 0) ? own : 1 - own;

            m_nbsPolarity = right ? NbsInverted : NbsNormal;
            m_messages.insert(m_messages.end(), m_nbsPending[right].begin(), m_nbsPending[right].end());
            m_nbsPending[0].clear();
            m_nbsPending[1].clear();
        }
    }

    if (m_nbsPolarity == NbsUndecided) {
        m_nbsPending[own].push_back(message);
    } else if ((m_nbsPolarity == NbsInverted) == (own == 1)) {
        m_messages.push_back(message);
    }
}

// The non battery saver transmission has ended: undecided, the normal polarity is taken
void RadioPagingGscReceiver::endNonBatterySaver()
{
    if (m_nbsPolarity == NbsUndecided) {
        m_messages.insert(m_messages.end(), m_nbsPending[0].begin(), m_nbsPending[0].end());
    }

    m_nbsPending[0].clear();
    m_nbsPending[1].clear();
    m_nbsPolarity = NbsUndecided;
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

    status.m_locked = isLocked();
    status.m_normal = m_normal.getStatus();
    status.m_inverted = m_inverted.getStatus();
    status.m_deviation = m_demod.getDeviation();

    return status;
}
