///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov                                             //
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

#include "channel/channelapi.h"
#include "device/deviceset.h"
#include "maincore.h"
#include "pipes/messagepipes.h"
#include "pipes/objectpipe.h"

#include "SWGDeviceState.h"

#include "cellularworker.h"
#include "cellular.h"

MESSAGE_CLASS_DEFINITION(Cellular::MsgConfigureCellular, Message)
MESSAGE_CLASS_DEFINITION(Cellular::MsgStartStop, Message)
MESSAGE_CLASS_DEFINITION(Cellular::MsgReportLog, Message)
MESSAGE_CLASS_DEFINITION(Cellular::MsgReportStatus, Message)

const char* const Cellular::m_featureIdURI = "sdrangel.feature.cellular";
const char* const Cellular::m_featureId = "Cellular";

namespace
{

const char* const kRxURI = "sdrangel.channel.cellularrx";
const char* const kTxURI = "sdrangel.channeltx.cellulartx";

} // namespace

Cellular::Cellular(WebAPIAdapterInterface *webAPIAdapterInterface) :
    Feature(m_featureIdURI, webAPIAdapterInterface),
    m_thread(nullptr),
    m_worker(nullptr),
    m_rxChannel(nullptr),
    m_txChannel(nullptr),
    m_running(false)
{
    setObjectName(m_featureId);
    m_state = StIdle;
    m_errorMessage = "Cellular error";
}

Cellular::~Cellular()
{
    stop();
}

bool Cellular::deserialize(const QByteArray& data)
{
    const bool ok = m_settings.deserialize(data);

    if (m_guiMessageQueue) {
        m_guiMessageQueue->push(MsgConfigureCellular::create(m_settings));
    }
    return ok;
}

int Cellular::webapiRun(bool run, SWGSDRangel::SWGDeviceState& response, QString& errorMessage)
{
    (void) errorMessage;
    getFeatureStateStr(*response.getState());
    getInputMessageQueue()->push(MsgStartStop::create(run));
    return 202;
}

void Cellular::report(const QString& text)
{
    qInfo() << "Cellular:" << text;
    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgReportLog::create(text));
    }
}

ChannelAPI *Cellular::findChannel(int deviceSetIndex, const QString& uri, QString& error) const
{
    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();

    if (deviceSetIndex < 0 || deviceSetIndex >= static_cast<int>(deviceSets.size()))
    {
        error = QString("there is no device set %1").arg(deviceSetIndex);
        return nullptr;
    }
    DeviceSet *deviceSet = deviceSets[static_cast<size_t>(deviceSetIndex)];

    for (int i = 0; i < deviceSet->getNumberOfChannels(); i++)
    {
        ChannelAPI *channel = deviceSet->getChannelAt(i);

        if (channel && channel->getURI() == uri) {
            return channel;
        }
    }
    error = QString("device set %1 has no %2 channel").arg(deviceSetIndex).arg(uri == kRxURI ? "Cellular Rx" : "Cellular Tx");
    return nullptr;
}

void Cellular::start()
{
    if (m_running) {
        return;
    }

    modemnmt::NmtNetworkConfig config;
    QString error;

    if (!m_settings.networkConfig(config, error))
    {
        report("error: carriers: " + error);
        setState(StError);
        return;
    }
    m_rxChannel = findChannel(m_settings.m_rxDeviceSetIndex, kRxURI, error);
    if (m_rxChannel) {
        m_txChannel = findChannel(m_settings.m_txDeviceSetIndex, kTxURI, error);
    }
    if (!m_rxChannel || !m_txChannel)
    {
        report("error: " + error);
        m_rxChannel = nullptr;
        m_txChannel = nullptr;
        setState(StError);
        return;
    }

    MessagePipes& messagePipes = MainCore::instance()->getMessagePipes();
    ObjectPipe *rxPipe = messagePipes.registerProducerToConsumer(m_rxChannel, this, "cellular");
    ObjectPipe *txPipe = messagePipes.registerProducerToConsumer(m_txChannel, this, "cellular");
    connect(rxPipe, SIGNAL(toBeDeleted(int, QObject*)), this, SLOT(handlePipeToBeDeleted(int, QObject*)));
    connect(txPipe, SIGNAL(toBeDeleted(int, QObject*)), this, SLOT(handlePipeToBeDeleted(int, QObject*)));

    m_thread = new QThread();
    m_thread->setObjectName("Cellular");
    m_worker = new CellularWorker();
    m_worker->moveToThread(m_thread);
    m_thread->start();

    CellularWorker::MsgStart *msg = CellularWorker::MsgStart::create();
    msg->m_config = config;
    msg->m_rxChannel = m_rxChannel;
    msg->m_txChannel = m_txChannel;
    msg->m_rxPipe = qobject_cast<MessageQueue*>(rxPipe->m_element);
    msg->m_txPipe = qobject_cast<MessageQueue*>(txPipe->m_element);
    msg->m_reports = getInputMessageQueue();
    msg->m_lead = static_cast<uint32_t>(m_settings.m_leadMs * modemnmt::NmtBaseStation::kAudioRate / 1000.0);
    msg->m_fallback = m_settings.m_fallback;
    m_worker->getInputMessageQueue()->push(msg);

    m_running = true;
    setState(StRunning);
    report(QString("started: receiver in device set %1, transmitter in device set %2")
        .arg(m_settings.m_rxDeviceSetIndex).arg(m_settings.m_txDeviceSetIndex));
}

void Cellular::stop()
{
    if (!m_running) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "stopWork", Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
    m_worker = nullptr;
    delete m_thread;
    m_thread = nullptr;
    unregisterPipes();
    m_running = false;
    setState(StIdle);
}

void Cellular::unregisterPipes()
{
    MessagePipes& messagePipes = MainCore::instance()->getMessagePipes();

    for (ChannelAPI *channel : {m_rxChannel, m_txChannel})
    {
        if (!channel) {
            continue;
        }
        ObjectPipe *pipe = messagePipes.unregisterProducerToConsumer(channel, this, "cellular");
        if (pipe) {
            disconnect(pipe, SIGNAL(toBeDeleted(int, QObject*)), this, SLOT(handlePipeToBeDeleted(int, QObject*)));
        }
    }
    m_rxChannel = nullptr;
    m_txChannel = nullptr;
}

void Cellular::handlePipeToBeDeleted(int reason, QObject *object)
{
    // reason 0: the producer (one of the radios) goes away
    if (reason == 0 && m_running && (object == m_rxChannel || object == m_txChannel))
    {
        if (object == m_rxChannel) {
            m_rxChannel = nullptr;
        } else {
            m_txChannel = nullptr;
        }
        stop();
        report("stopped: a radio channel was removed");
    }
}

bool Cellular::handleMessage(const Message& cmd)
{
    if (MsgConfigureCellular::match(cmd))
    {
        m_settings = static_cast<const MsgConfigureCellular&>(cmd).getSettings();
        return true;
    }
    else if (MsgStartStop::match(cmd))
    {
        if (static_cast<const MsgStartStop&>(cmd).getStartStop()) {
            start();
        } else {
            stop();
        }
        return true;
    }
    else if (MsgReportLog::match(cmd))
    {
        // from the worker, on to the GUI
        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(MsgReportLog::create(static_cast<const MsgReportLog&>(cmd).getText()));
        }
        return true;
    }
    else if (MsgReportStatus::match(cmd))
    {
        if (getMessageQueueToGUI())
        {
            const MsgReportStatus& report = static_cast<const MsgReportStatus&>(cmd);
            MsgReportStatus *copy = MsgReportStatus::create();
            copy->m_summary = report.m_summary;
            copy->m_carriers = report.m_carriers;
            getMessageQueueToGUI()->push(copy);
        }
        return true;
    }

    return false;
}
