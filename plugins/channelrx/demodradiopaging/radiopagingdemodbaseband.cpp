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

#include <QDebug>

#include "dsp/dspcommands.h"
#include "dsp/downchannelizer.h"

#include "radiopagingdemodbaseband.h"

MESSAGE_CLASS_DEFINITION(RadioPagingDemodBaseband::MsgConfigureRadioPagingDemodBaseband, Message)

RadioPagingDemodBaseband::RadioPagingDemodBaseband() :
    m_running(false)
{
    qDebug("RadioPagingDemodBaseband::RadioPagingDemodBaseband");

    m_sampleFifo.setSize(SampleSinkFifo::getSizePolicy(48000));
    m_channelizer = new DownChannelizer(&m_sink);
}

RadioPagingDemodBaseband::~RadioPagingDemodBaseband()
{
    m_inputMessageQueue.clear();

    delete m_channelizer;
}

void RadioPagingDemodBaseband::reset()
{
    QMutexLocker mutexLocker(&m_mutex);
    m_inputMessageQueue.clear();
    m_sampleFifo.reset();
}

void RadioPagingDemodBaseband::startWork()
{
    QMutexLocker mutexLocker(&m_mutex);
    QObject::connect(
        &m_sampleFifo,
        &SampleSinkFifo::dataReady,
        this,
        &RadioPagingDemodBaseband::handleData,
        Qt::QueuedConnection
    );
    connect(&m_inputMessageQueue, SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
    m_running = true;
}

void RadioPagingDemodBaseband::stopWork()
{
    QMutexLocker mutexLocker(&m_mutex);
    disconnect(&m_inputMessageQueue, SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
    QObject::disconnect(
        &m_sampleFifo,
        &SampleSinkFifo::dataReady,
        this,
        &RadioPagingDemodBaseband::handleData
    );
    m_running = false;
}

void RadioPagingDemodBaseband::setChannel(ChannelAPI *channel)
{
    m_sink.setChannel(channel);
}

void RadioPagingDemodBaseband::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
{
    m_sampleFifo.write(begin, end);
}

void RadioPagingDemodBaseband::handleData()
{
    QMutexLocker mutexLocker(&m_mutex);

    while ((m_sampleFifo.fill() > 0) && (m_inputMessageQueue.size() == 0))
    {
        SampleVector::iterator part1begin;
        SampleVector::iterator part1end;
        SampleVector::iterator part2begin;
        SampleVector::iterator part2end;

        std::size_t count = m_sampleFifo.readBegin(m_sampleFifo.fill(), &part1begin, &part1end, &part2begin, &part2end);

        // first part of FIFO data
        if (part1begin != part1end) {
            m_channelizer->feed(part1begin, part1end);
        }

        // second part of FIFO data (used when block wraps around)
        if (part2begin != part2end) {
            m_channelizer->feed(part2begin, part2end);
        }

        m_sampleFifo.readCommit((unsigned int) count);
    }
}

void RadioPagingDemodBaseband::handleInputMessages()
{
    Message* message;

    while ((message = m_inputMessageQueue.pop()))
    {
        if (!handleMessage(*message)) {
            qDebug("%s: unhandled message: %s", Q_FUNC_INFO, message->getIdentifier());
        }
        delete message;
    }
}

bool RadioPagingDemodBaseband::handleMessage(const Message& cmd)
{
    if (MsgConfigureRadioPagingDemodBaseband::match(cmd))
    {
        QMutexLocker mutexLocker(&m_mutex);
        const MsgConfigureRadioPagingDemodBaseband& cfg = (const MsgConfigureRadioPagingDemodBaseband&) cmd;
        qDebug() << "RadioPagingDemodBaseband::handleMessage: MsgConfigureRadioPagingDemodBaseband";

        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        QMutexLocker mutexLocker(&m_mutex);
        const DSPSignalNotification& notif = (const DSPSignalNotification&) cmd;
        qDebug() << "RadioPagingDemodBaseband::handleMessage: DSPSignalNotification: basebandSampleRate: " << notif.getSampleRate();
        setBasebandSampleRate(notif.getSampleRate());
        m_sampleFifo.setSize(SampleSinkFifo::getSizePolicy(notif.getSampleRate()));

        return true;
    }
    else
    {
        return false;
    }
}

void RadioPagingDemodBaseband::applySettings(const QStringList& settingsKeys, const RadioPagingDemodSettings& settings, bool force)
{
    if ((settingsKeys.contains("inputFrequencyOffset") && settings.m_inputFrequencyOffset != m_settings.m_inputFrequencyOffset) || force)
    {
        m_channelizer->setChannelization(RadioPagingDemodSettings::m_channelSampleRate, settings.m_inputFrequencyOffset);
        m_sink.applyChannelSettings(m_channelizer->getChannelSampleRate(), m_channelizer->getChannelFrequencyOffset());
    }

    m_sink.applySettings(settingsKeys, settings, force);

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }
}

int RadioPagingDemodBaseband::getChannelSampleRate() const
{
    return m_channelizer->getChannelSampleRate();
}

void RadioPagingDemodBaseband::setBasebandSampleRate(int sampleRate)
{
    m_channelizer->setBasebandSampleRate(sampleRate);
    m_sink.applyChannelSettings(m_channelizer->getChannelSampleRate(), m_channelizer->getChannelFrequencyOffset());
}
