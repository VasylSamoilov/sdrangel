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
#include "device/deviceapi.h"
#include "util/messagerecord.h"
#include "maincore.h"

#include "radiopagingdemod.h"

MESSAGE_CLASS_DEFINITION(RadioPagingDemod::MsgConfigureRadioPagingDemod, Message)

const char * const RadioPagingDemod::m_channelIdURI = "sdrangel.channel.radiopagingdemod";
const char * const RadioPagingDemod::m_channelId = "RadioPagingDemod";

RadioPagingDemod::RadioPagingDemod(DeviceAPI *deviceAPI) :
        ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSink),
        m_deviceAPI(deviceAPI),
        m_basebandSampleRate(0),
        m_centerFrequency(0)
{
    setObjectName(m_channelId);
    m_thread.setObjectName("RadioPagingDemodBB");

    m_basebandSink = new RadioPagingDemodBaseband();
    m_basebandSink->setMessageQueueToChannel(getInputMessageQueue());
    m_basebandSink->setChannel(this);
    m_basebandSink->moveToThread(&m_thread);

    applySettings(QStringList(), m_settings, true);

    m_deviceAPI->addChannelSink(this);
    m_deviceAPI->addChannelSinkAPI(this);
}

RadioPagingDemod::~RadioPagingDemod()
{
    qDebug("RadioPagingDemod::~RadioPagingDemod");
    m_deviceAPI->removeChannelSinkAPI(this);
    m_deviceAPI->removeChannelSink(this, true);

    if (m_basebandSink->isRunning()) {
        stop();
    }

    delete m_basebandSink;
}

void RadioPagingDemod::setDeviceAPI(DeviceAPI *deviceAPI)
{
    if (deviceAPI != m_deviceAPI)
    {
        m_deviceAPI->removeChannelSinkAPI(this);
        m_deviceAPI->removeChannelSink(this, false);
        m_deviceAPI = deviceAPI;
        m_deviceAPI->addChannelSink(this);
        m_deviceAPI->addChannelSinkAPI(this);
    }
}

uint32_t RadioPagingDemod::getNumberOfDeviceStreams() const
{
    return m_deviceAPI->getNbSourceStreams();
}

void RadioPagingDemod::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool firstOfBurst)
{
    (void) firstOfBurst;
    m_basebandSink->feed(begin, end);
}

void RadioPagingDemod::start()
{
    qDebug("RadioPagingDemod::start");

    m_basebandSink->reset();
    m_basebandSink->startWork();
    m_thread.start();

    DSPSignalNotification *dspMsg = new DSPSignalNotification(m_basebandSampleRate, m_centerFrequency);
    m_basebandSink->getInputMessageQueue()->push(dspMsg);

    RadioPagingDemodBaseband::MsgConfigureRadioPagingDemodBaseband *msg =
        RadioPagingDemodBaseband::MsgConfigureRadioPagingDemodBaseband::create(QStringList(), m_settings, true);
    m_basebandSink->getInputMessageQueue()->push(msg);
}

void RadioPagingDemod::stop()
{
    qDebug("RadioPagingDemod::stop");
    m_basebandSink->stopWork();
    m_thread.quit();
    m_thread.wait();
}

bool RadioPagingDemod::handleMessage(const Message& cmd)
{
    if (MsgConfigureRadioPagingDemod::match(cmd))
    {
        const MsgConfigureRadioPagingDemod& cfg = (const MsgConfigureRadioPagingDemod&) cmd;
        qDebug() << "RadioPagingDemod::handleMessage: MsgConfigureRadioPagingDemod";
        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        const DSPSignalNotification& notif = (const DSPSignalNotification&) cmd;
        m_basebandSampleRate = notif.getSampleRate();
        m_centerFrequency = notif.getCenterFrequency();
        // Forward to the sink
        DSPSignalNotification* rep = new DSPSignalNotification(notif); // make a copy
        qDebug() << "RadioPagingDemod::handleMessage: DSPSignalNotification";
        m_basebandSink->getInputMessageQueue()->push(rep);
        // Forward to GUI if any
        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(new DSPSignalNotification(notif));
        }

        return true;
    }
    else if (MainCore::MsgMessageRecord::match(cmd))
    {
        const MainCore::MsgMessageRecord& msg = (const MainCore::MsgMessageRecord&) cmd;
        publishRecord(msg.getRecord());

        return true;
    }
    else if (MainCore::MsgChannelDemodQuery::match(cmd))
    {
        qDebug() << "RadioPagingDemod::handleMessage: MsgChannelDemodQuery";
        sendSampleRateToDemodAnalyzer();

        return true;
    }
    else
    {
        return false;
    }
}

// The decoder filled the protocol fields; the channel adds where the message was received
void RadioPagingDemod::publishRecord(MessageRecord record)
{
    record.m_frequency = m_centerFrequency + m_settings.m_inputFrequencyOffset;
    record.m_channelAlias = m_settings.m_title;
    record.m_deviceSetIndex = getDeviceSetIndex();
    record.m_channelIndex = getIndexInDeviceSet();
    record.m_centerFrequency = m_centerFrequency;
    record.m_channelOffset = m_settings.m_inputFrequencyOffset;

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MainCore::MsgMessageRecord::create(this, record));
    }

    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, MessageRecord::m_pipeName, pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        messageQueue->push(MainCore::MsgMessageRecord::create(this, record));
    }
}

void RadioPagingDemod::setCenterFrequency(qint64 frequency)
{
    RadioPagingDemodSettings settings = m_settings;
    settings.m_inputFrequencyOffset = frequency;
    applySettings(QStringList("inputFrequencyOffset"), settings, false);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureRadioPagingDemod *msgToGUI = MsgConfigureRadioPagingDemod::create(QStringList("inputFrequencyOffset"), settings, false);
        m_guiMessageQueue->push(msgToGUI);
    }
}

void RadioPagingDemod::applySettings(const QStringList& settingsKeys, const RadioPagingDemodSettings& settings, bool force)
{
    qDebug() << "RadioPagingDemod::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (settingsKeys.contains("streamIndex") && (m_settings.m_streamIndex != settings.m_streamIndex))
    {
        if (m_deviceAPI->getSampleMIMO()) // change of stream is possible for MIMO devices only
        {
            m_deviceAPI->removeChannelSinkAPI(this);
            m_deviceAPI->removeChannelSink(this, m_settings.m_streamIndex);
            m_deviceAPI->addChannelSink(this, settings.m_streamIndex);
            m_deviceAPI->addChannelSinkAPI(this);
            m_settings.m_streamIndex = settings.m_streamIndex; // make sure ChannelAPI::getStreamIndex() is consistent
            emit streamIndexChanged(settings.m_streamIndex);
        }
    }

    RadioPagingDemodBaseband::MsgConfigureRadioPagingDemodBaseband *msg =
        RadioPagingDemodBaseband::MsgConfigureRadioPagingDemodBaseband::create(settingsKeys, settings, force);
    m_basebandSink->getInputMessageQueue()->push(msg);

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }
}

QByteArray RadioPagingDemod::serialize() const
{
    return m_settings.serialize();
}

bool RadioPagingDemod::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        MsgConfigureRadioPagingDemod *msg = MsgConfigureRadioPagingDemod::create(QStringList(), m_settings, true);
        m_inputMessageQueue.push(msg);
        return true;
    }
    else
    {
        m_settings.resetToDefaults();
        MsgConfigureRadioPagingDemod *msg = MsgConfigureRadioPagingDemod::create(QStringList(), m_settings, true);
        m_inputMessageQueue.push(msg);
        return false;
    }
}

void RadioPagingDemod::sendSampleRateToDemodAnalyzer()
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "reportdemod", pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        MainCore::MsgChannelDemodReport *msg = MainCore::MsgChannelDemodReport::create(
            this,
            RadioPagingDemodSettings::m_channelSampleRate
        );
        messageQueue->push(msg);
    }
}

int RadioPagingDemod::getChannelSampleRate() const
{
    return RadioPagingDemodSettings::m_channelSampleRate;
}
