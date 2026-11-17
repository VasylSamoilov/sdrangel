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

#include <algorithm>

#include <QDebug>

#include "channel/channelapi.h"
#include "device/deviceset.h"
#include "maincore.h"
#include "pipes/messagepipes.h"
#include "pipes/objectpipe.h"

#include "SWGDeviceState.h"
#include "SWGFeatureSettings.h"
#include "SWGFeatureReport.h"
#include "SWGCellularSettings.h"
#include "SWGCellularNetworkSettings.h"
#include "SWGCellularReport.h"
#include "SWGCellularNetworkReport.h"
#include "SWGCellularCarrierReport.h"

#include "cellularworker.h"
#include "cellular.h"

MESSAGE_CLASS_DEFINITION(Cellular::MsgConfigureCellular, Message)
MESSAGE_CLASS_DEFINITION(Cellular::MsgStartStop, Message)
MESSAGE_CLASS_DEFINITION(Cellular::MsgToRadio, Message)
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
    bool ok;
    {
        QMutexLocker locker(&m_settingsMutex);
        ok = m_settings.deserialize(data);
    }

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
    const QString name = uri == kRxURI ? "Cellular Rx" : "Cellular Tx";
    ChannelAPI *found = nullptr;
    int count = 0;

    // one channel carries all the carriers of the device: with more, which one is meant is a guess
    for (int i = 0; i < deviceSet->getNumberOfChannels(); i++)
    {
        ChannelAPI *channel = deviceSet->getChannelAt(i);

        if (channel && channel->getURI() == uri)
        {
            found = found ? found : channel;
            count++;
        }
    }
    if (count > 1)
    {
        error = QString("device set %1 has %2 %3 channels, it needs exactly one").arg(deviceSetIndex).arg(count).arg(name);
        return nullptr;
    }
    if (!found) {
        error = QString("device set %1 has no %2 channel").arg(deviceSetIndex).arg(name);
    }
    return found;
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
    m_worker = new CellularWorker(getInputMessageQueue(), this,
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
    dropReport(-1);
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
    dropReport(id);
}

void Cellular::keepReport(const MsgReportStatus& report)
{
    QMutexLocker locker(&m_reportMutex);
    std::unique_ptr<MsgReportStatus> copy(MsgReportStatus::create(report.m_network));

    copy->m_onAir = report.m_onAir;
    copy->m_registered = report.m_registered;
    copy->m_calls = report.m_calls;
    copy->m_summary = report.m_summary;
    copy->m_carriers = report.m_carriers;
    m_reports[report.m_network] = std::move(copy);
}

void Cellular::dropReport(int network)
{
    QMutexLocker locker(&m_reportMutex);

    if (network < 0) {
        m_reports.clear();
    } else {
        m_reports.erase(network);
    }
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
    ChannelAPI *gone = nullptr;
    for (const auto& p : m_pipes)
    {
        if (p.first == object) {
            gone = p.first;
        }
    }
    if (!gone) {
        return;
    }
    // the channel is half destroyed: the worker must not send to it any more, and its pipe is
    // gone with it (nothing to unregister)
    QMetaObject::invokeMethod(m_worker, "radioGone", Qt::BlockingQueuedConnection,
        Q_ARG(qulonglong, static_cast<qulonglong>(reinterpret_cast<quintptr>(gone))));
    m_pipes.erase(gone);

    std::vector<int> stopping;
    for (const auto& a : m_active)
    {
        if (a.second.rx == gone || a.second.tx == gone) {
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
        const MsgConfigureCellular& cfg = static_cast<const MsgConfigureCellular&>(cmd);
        {
            QMutexLocker locker(&m_settingsMutex);
            m_settings = cfg.getSettings();
            m_settings.setRollupState(nullptr);
        }
        if (cfg.getForce() && m_running)
        {
            // all settings apply again: the networks that run start again
            std::vector<int> running;
            for (const auto& a : m_active) {
                running.push_back(a.first);
            }
            for (int id : running)
            {
                stopNetwork(id);
                report(id, "stopped: settings replaced");
            }
        }
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
    else if (MsgToRadio::match(cmd))
    {
        // only to a radio that is in use: a channel that was removed is no longer in m_pipes
        const MsgToRadio& msg = static_cast<const MsgToRadio&>(cmd);

        if (m_pipes.find(msg.getChannel()) != m_pipes.end()) {
            msg.getChannel()->getInputMessageQueue()->push(MainCore::MsgPacket::create(msg.getSource(), msg.getPacket()));
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
        const MsgReportStatus& report = static_cast<const MsgReportStatus&>(cmd);

        if (m_active.find(report.m_network) != m_active.end()) {
            keepReport(report);
        }
        if (getMessageQueueToGUI())
        {
            MsgReportStatus *copy = MsgReportStatus::create(report.m_network);
            copy->m_onAir = report.m_onAir;
            copy->m_registered = report.m_registered;
            copy->m_calls = report.m_calls;
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

// ---- web API ----

int Cellular::webapiSettingsGet(SWGSDRangel::SWGFeatureSettings& response, QString& errorMessage)
{
    (void) errorMessage;
    response.setCellularSettings(new SWGSDRangel::SWGCellularSettings());
    response.getCellularSettings()->init();
    QMutexLocker locker(&m_settingsMutex);
    webapiFormatFeatureSettings(response, m_settings);
    return 200;
}

int Cellular::webapiSettingsPutPatch(bool force, const QStringList& featureSettingsKeys,
    SWGSDRangel::SWGFeatureSettings& response, QString& errorMessage)
{
    (void) errorMessage;
    CellularSettings settings;
    {
        QMutexLocker locker(&m_settingsMutex);
        settings = m_settings;
    }
    webapiUpdateFeatureSettings(settings, featureSettingsKeys, response);

    // the networks that are switched on start, the others stop; a PUT starts the running ones again
    getInputMessageQueue()->push(MsgConfigureCellular::create(settings, force));
    if (m_guiMessageQueue) {
        m_guiMessageQueue->push(MsgConfigureCellular::create(settings));
    }

    webapiFormatFeatureSettings(response, settings);
    return 200;
}

int Cellular::webapiReportGet(SWGSDRangel::SWGFeatureReport& response, QString& errorMessage)
{
    (void) errorMessage;
    response.setCellularReport(new SWGSDRangel::SWGCellularReport());
    response.getCellularReport()->init();
    SWGSDRangel::SWGCellularReport *report = response.getCellularReport();

    report->setRunning(m_running ? 1 : 0);
    QMutexLocker locker(&m_reportMutex);
    for (const auto& r : m_reports)
    {
        const MsgReportStatus& status = *r.second;
        SWGSDRangel::SWGCellularNetworkReport *network = new SWGSDRangel::SWGCellularNetworkReport();

        network->init();
        network->setId(status.m_network);
        network->setOnAir(status.m_onAir ? 1 : 0);
        *network->getSummary() = status.m_summary;
        network->setRegistered(status.m_registered);
        network->setCalls(status.m_calls);
        for (const MsgReportStatus::Carrier& c : status.m_carriers)
        {
            SWGSDRangel::SWGCellularCarrierReport *carrier = new SWGSDRangel::SWGCellularCarrierReport();

            carrier->init();
            carrier->setChannel(c.channel);
            carrier->setDownlink(static_cast<qint64>(c.downlink));
            carrier->setUplink(static_cast<qint64>(c.uplink));
            *carrier->getState() = c.state;
            carrier->setRxLevel(c.rxLevel);
            carrier->setTxFillMs(static_cast<float>(c.txFillMs));
            carrier->setUnderruns(static_cast<qint32>(c.underruns));
            network->getCarriers()->append(carrier);
        }
        report->getNetworks()->append(network);
    }
    return 200;
}

void Cellular::webapiFormatFeatureSettings(SWGSDRangel::SWGFeatureSettings& response, const CellularSettings& settings)
{
    SWGSDRangel::SWGCellularSettings *s = response.getCellularSettings();

    s->setLeadMs(settings.m_leadMs);
    if (s->getTitle()) {
        *s->getTitle() = settings.m_title;
    } else {
        s->setTitle(new QString(settings.m_title));
    }
    s->setRgbColor(static_cast<qint32>(settings.m_rgbColor));

    QList<SWGSDRangel::SWGCellularNetworkSettings*> *list = s->getNetworks();
    if (list)
    {
        qDeleteAll(*list);
        list->clear();
    }
    else
    {
        list = new QList<SWGSDRangel::SWGCellularNetworkSettings*>();
        s->setNetworks(list);
    }
    for (const CellularNetworkSettings& n : settings.m_networks)
    {
        SWGSDRangel::SWGCellularNetworkSettings *item = new SWGSDRangel::SWGCellularNetworkSettings();

        item->init();
        item->setId(n.m_id);
        *item->getName() = n.m_name;
        item->setEnabled(n.m_enabled ? 1 : 0);
        item->setSystem(n.m_system);
        *item->getCountry() = n.m_country;
        item->setArea(n.m_area);
        item->setAreaNo(n.m_areaNo);
        item->setMsPower(n.m_msPower);
        *item->getCarriers() = n.m_carriers;
        item->setCompandor(n.m_compandor ? 1 : 0);
        item->setSendCallerId(n.m_sendCallerId ? 1 : 0);
        item->setSendClock(n.m_sendClock ? 1 : 0);
        *item->getSmscNumber() = n.m_smscNumber;
        item->setFallback(n.m_fallback);
        item->setRxDeviceSetIndex(n.m_rxDeviceSetIndex);
        item->setTxDeviceSetIndex(n.m_txDeviceSetIndex);
        list->append(item);
    }
}

void Cellular::webapiUpdateFeatureSettings(CellularSettings& settings, const QStringList& featureSettingsKeys,
    SWGSDRangel::SWGFeatureSettings& response)
{
    SWGSDRangel::SWGCellularSettings *s = response.getCellularSettings();

    if (featureSettingsKeys.contains("leadMs")) {
        settings.m_leadMs = std::max(20, std::min(1000, s->getLeadMs()));
    }
    if (featureSettingsKeys.contains("title") && s->getTitle()) {
        settings.m_title = *s->getTitle();
    }
    if (featureSettingsKeys.contains("rgbColor")) {
        settings.m_rgbColor = static_cast<quint32>(s->getRgbColor());
    }
    if (!featureSettingsKeys.contains("networks") || !s->getNetworks()) {
        return;
    }

    QList<CellularNetworkSettings> networks;
    int nextId = 1;
    for (const CellularNetworkSettings& n : settings.m_networks) {
        nextId = std::max(nextId, n.m_id + 1);
    }
    int index = 0;
    for (SWGSDRangel::SWGCellularNetworkSettings *item : *s->getNetworks())
    {
        // the fields that were given are the keys "networks[i].field" of the request
        auto given = [&](const char *field) {
            return featureSettingsKeys.contains(QString("networks[%1].%2").arg(index).arg(field));
        };
        const int id = given("id") ? item->getId() : nextId++;
        bool duplicate = id < 1 || id > 200000;
        for (const CellularNetworkSettings& n : networks) {
            duplicate = duplicate || n.m_id == id;
        }
        if (duplicate)
        {
            index++;
            continue;
        }
        const CellularNetworkSettings *existing = settings.network(id);
        CellularNetworkSettings n = existing ? *existing : CellularNetworkSettings(id);

        if (given("name") && item->getName()) {
            n.m_name = *item->getName();
        }
        if (given("enabled")) {
            n.m_enabled = item->getEnabled() != 0;
        }
        if (given("system")) {
            n.m_system = item->getSystem() == 1 ? 1 : 0;
        }
        if (given("country") && item->getCountry()) {
            n.m_country = *item->getCountry();
        }
        if (given("area")) {
            n.m_area = item->getArea();
        }
        if (given("areaNo")) {
            n.m_areaNo = item->getAreaNo();
        }
        if (given("msPower")) {
            n.m_msPower = item->getMsPower();
        }
        if (given("carriers") && item->getCarriers()) {
            n.m_carriers = *item->getCarriers();
        }
        if (given("compandor")) {
            n.m_compandor = item->getCompandor() != 0;
        }
        if (given("sendCallerId")) {
            n.m_sendCallerId = item->getSendCallerId() != 0;
        }
        if (given("sendClock")) {
            n.m_sendClock = item->getSendClock() != 0;
        }
        if (given("smscNumber") && item->getSmscNumber()) {
            n.m_smscNumber = *item->getSmscNumber();
        }
        if (given("fallback")) {
            n.m_fallback = item->getFallback() == CellularNetworkSettings::FallbackMusic
                ? CellularNetworkSettings::FallbackMusic : CellularNetworkSettings::FallbackEcho;
        }
        if (given("rxDeviceSetIndex")) {
            n.m_rxDeviceSetIndex = item->getRxDeviceSetIndex();
        }
        if (given("txDeviceSetIndex")) {
            n.m_txDeviceSetIndex = item->getTxDeviceSetIndex();
        }
        networks.append(n);
        index++;
    }
    settings.m_networks = networks;
}
