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
    applyNetworks();
    return ok;
}

int Cellular::webapiRun(bool run, SWGSDRangel::SWGDeviceState& response, QString& errorMessage)
{
    (void) errorMessage;
    getFeatureStateStr(*response.getState());
    getInputMessageQueue()->push(MsgStartStop::create(run));
    return 202;
}

void Cellular::report(int network, const QString& text)
{
    qInfo().noquote() << QString("Cellular: network %1: %2").arg(network).arg(text);
    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgReportLog::create(network, text));
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

// the pipe of what the radio sends us, registered with its first network
MessageQueue *Cellular::usePipe(ChannelAPI *channel)
{
    Pipe& p = m_pipes[channel];

    if (!p.pipe)
    {
        p.pipe = MainCore::instance()->getMessagePipes().registerProducerToConsumer(channel, this, "cellular");
        connect(p.pipe, SIGNAL(toBeDeleted(int, QObject*)), this, SLOT(handlePipeToBeDeleted(int, QObject*)));
    }
    p.users++;
    return qobject_cast<MessageQueue*>(p.pipe->m_element);
}

// and unregistered with its last
void Cellular::releasePipe(ChannelAPI *channel)
{
    auto it = m_pipes.find(channel);

    if (it == m_pipes.end() || --it->second.users > 0) {
        return;
    }
    disconnect(it->second.pipe, SIGNAL(toBeDeleted(int, QObject*)), this, SLOT(handlePipeToBeDeleted(int, QObject*)));
    MainCore::instance()->getMessagePipes().unregisterProducerToConsumer(channel, this, "cellular");
    m_pipes.erase(it);
}

void Cellular::start()
{
    if (m_running) {
        return;
    }
    m_thread = new QThread();
    m_thread->setObjectName("Cellular");
    m_worker = new CellularWorker(getInputMessageQueue(),
        static_cast<uint32_t>(m_settings.m_leadMs * modemnmt::NmtBaseStation::kAudioRate / 1000.0));
    m_worker->moveToThread(m_thread);
    m_thread->start();
    m_running = true;
    setState(StRunning);
    report(0, QString("started, transmit lead %1 ms").arg(m_settings.m_leadMs));
    applyNetworks();
}

void Cellular::stop()
{
    if (!m_running) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "removeAll", Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_worker;
    m_worker = nullptr;
    delete m_thread;
    m_thread = nullptr;

    for (auto& a : m_active)
    {
        releasePipe(a.second.rx);
        releasePipe(a.second.tx);
    }
    m_active.clear();
    m_running = false;
    setState(StIdle);
    report(0, "stopped");
}

void Cellular::startNetwork(const CellularNetworkSettings& network)
{
    modemnmt::NmtNetworkConfig config;
    QString error;

    if (!network.networkConfig(config, error))
    {
        report(network.m_id, "error: carriers: " + error);
        return;
    }
    ChannelAPI *rx = findChannel(network.m_rxDeviceSetIndex, kRxURI, error);
    ChannelAPI *tx = rx ? findChannel(network.m_txDeviceSetIndex, kTxURI, error) : nullptr;
    if (!rx || !tx)
    {
        report(network.m_id, "error: " + error);
        return;
    }

    CellularWorker::MsgAddNetwork *msg = CellularWorker::MsgAddNetwork::create();
    msg->m_id = network.m_id;
    msg->m_name = network.m_name;
    msg->m_config = config;
    msg->m_fallback = network.m_fallback;
    msg->m_rxChannel = rx;
    msg->m_rxPipe = usePipe(rx);
    msg->m_txChannel = tx;
    msg->m_txPipe = usePipe(tx);
    m_worker->getInputMessageQueue()->push(msg);

    Active& a = m_active[network.m_id];
    a.settings = network;
    a.rx = rx;
    a.tx = tx;
}

void Cellular::stopNetwork(int id)
{
    auto it = m_active.find(id);

    if (it == m_active.end()) {
        return;
    }
    QMetaObject::invokeMethod(m_worker, "removeNetwork", Qt::BlockingQueuedConnection, Q_ARG(int, id));
    releasePipe(it->second.rx);
    releasePipe(it->second.tx);
    m_active.erase(it);
}

// the networks that are on and the ones that run come to agree
void Cellular::applyNetworks()
{
    if (!m_running) {
        return;
    }

    std::vector<int> stopping;
    for (const auto& a : m_active)
    {
        const CellularNetworkSettings *n = m_settings.network(a.first);

        if (!n || !n->m_enabled) {
            stopping.push_back(a.first);
        }
    }
    for (int id : stopping) {
        stopNetwork(id);
    }
    for (const CellularNetworkSettings& n : m_settings.m_networks)
    {
        if (n.m_enabled && m_active.find(n.m_id) == m_active.end()) {
            startNetwork(n);
        }
    }
}

void Cellular::handlePipeToBeDeleted(int reason, QObject *object)
{
    // reason 0: the producer (a radio) goes away: the networks on it stop
    if (reason != 0 || !m_running) {
        return;
    }
    std::vector<int> stopping;
    for (const auto& a : m_active)
    {
        if (a.second.rx == object || a.second.tx == object) {
            stopping.push_back(a.first);
        }
    }
    for (int id : stopping)
    {
        stopNetwork(id);
        report(id, "stopped: its radio channel was removed");
    }
}

bool Cellular::handleMessage(const Message& cmd)
{
    if (MsgConfigureCellular::match(cmd))
    {
        m_settings = static_cast<const MsgConfigureCellular&>(cmd).getSettings();
        m_settings.setRollupState(nullptr);
        applyNetworks();
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
        if (getMessageQueueToGUI())
        {
            const MsgReportLog& report = static_cast<const MsgReportLog&>(cmd);
            getMessageQueueToGUI()->push(MsgReportLog::create(report.getNetwork(), report.getText()));
        }
        return true;
    }
    else if (MsgReportStatus::match(cmd))
    {
        if (getMessageQueueToGUI())
        {
            const MsgReportStatus& report = static_cast<const MsgReportStatus&>(cmd);
            MsgReportStatus *copy = MsgReportStatus::create(report.m_network);
            copy->m_summary = report.m_summary;
            copy->m_carriers = report.m_carriers;
            auto active = m_active.find(report.m_network);
            const CellularNetworkSettings *current = m_settings.network(report.m_network);
            copy->m_stale = active != m_active.end() && current && !active->second.settings.sameNetwork(*current);
            getMessageQueueToGUI()->push(copy);
        }
        return true;
    }

    return false;
}
