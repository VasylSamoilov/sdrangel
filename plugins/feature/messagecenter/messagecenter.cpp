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

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <algorithm>

#include "SWGChannelActions.h"
#include "SWGFeatureActions.h"
#include "SWGFeatureReport.h"
#include "SWGFeatureSettings.h"
#include "SWGMessageCenterActions.h"
#include "SWGMessageCenterPost.h"
#include "SWGMessageCenterRedecode.h"
#include "SWGMessageCenterReport.h"
#include "SWGMessageCenterSettings.h"
#include "SWGRadioPagingModActions.h"
#include "SWGRadioPagingModTx.h"

#include "channel/channelapi.h"
#include "maincore.h"
#include "radiopagingpage.h"
#include "radiopagingrecode.h"

#include "messagecenter.h"

MESSAGE_CLASS_DEFINITION(MessageCenter::MsgConfigureMessageCenter, Message)
MESSAGE_CLASS_DEFINITION(MessageCenter::MsgPostPage, Message)
MESSAGE_CLASS_DEFINITION(MessageCenter::MsgPageAction, Message)
MESSAGE_CLASS_DEFINITION(MessageCenter::MsgUpdated, Message)
MESSAGE_CLASS_DEFINITION(MessageCenter::MsgRedecoded, Message)
MESSAGE_CLASS_DEFINITION(MessageCenter::MsgClearLog, Message)

const char* const MessageCenter::m_featureIdURI = "sdrangel.feature.messagecenter";
const char* const MessageCenter::m_featureId = "MessageCenter";

namespace {

// The channels that send messages: the paging demodulator and modulator
const QStringList channelURIs = {"sdrangel.channel.radiopagingdemod", "sdrangel.channeltx.modradiopaging"};

const int tickMs = 500;

} // namespace

MessageCenter::MessageCenter(WebAPIAdapterInterface *webAPIAdapterInterface) :
    Feature(m_featureIdURI, webAPIAdapterInterface),
    m_availableChannelHandler(channelURIs, QStringList{MessageRecord::m_pipeName}, "RTMF", this)
{
    setObjectName(m_featureId);
    setState(StIdle);
    m_settings.setRollupState(nullptr);

    QObject::connect(&m_availableChannelHandler, &AvailableChannelOrFeatureHandler::messageEnqueued,
        this, &MessageCenter::handleChannelMessageQueue);
    m_availableChannelHandler.scanAvailableChannelsAndFeatures();

    QObject::connect(&m_timer, &QTimer::timeout, this, &MessageCenter::tick);
    applyEngineSettings();
    m_timer.start(tickMs);

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(m_networkManager, &QNetworkAccessManager::finished, this, &MessageCenter::networkManagerFinished);
}

MessageCenter::~MessageCenter()
{
    m_timer.stop();
    QObject::disconnect(&m_availableChannelHandler, &AvailableChannelOrFeatureHandler::messageEnqueued,
        this, &MessageCenter::handleChannelMessageQueue);
    QObject::disconnect(m_networkManager, &QNetworkAccessManager::finished, this, &MessageCenter::networkManagerFinished);
    delete m_networkManager;

    // The history file is free for another message center
    if (historyOwner() == this) {
        historyOwner() = nullptr;
    }
}

// The message center that writes the history file: two of them would write the same messages twice
MessageCenter*& MessageCenter::historyOwner()
{
    static MessageCenter *owner = nullptr;
    return owner;
}

QString MessageCenter::historyFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/messagecenter/history.jsonl";
}

// Called with the engine locked
void MessageCenter::applyHistory()
{
    QString path;

    if (m_settings.m_historyEnabled)
    {
        if (historyOwner() && (historyOwner() != this))
        {
            m_historyError = "Another message center keeps the history";
        }
        else
        {
            path = historyFilePath();
            QDir().mkpath(QFileInfo(path).absolutePath());
            historyOwner() = this;
            m_historyError.clear();
        }
    }
    else
    {
        if (historyOwner() == this) {
            historyOwner() = nullptr;
        }

        m_historyError.clear();
    }

    QString error;

    if (!m_engine.setHistory(path, (qint64) m_settings.m_historyMaxMB * 1024 * 1024, error))
    {
        m_historyError = error;
        qWarning("MessageCenter::applyHistory: %s", qPrintable(error));
    }
    if (!m_historyError.isEmpty()) {
        qInfo("MessageCenter::applyHistory: %s", qPrintable(m_historyError));
    }
}

void MessageCenter::handleChannelMessageQueue(MessageQueue *messageQueue)
{
    Message *message;

    while ((message = messageQueue->pop()) != nullptr)
    {
        if (handleMessage(*message)) {
            delete message;
        }
    }
}

bool MessageCenter::handleMessage(const Message& cmd)
{
    QMutexLocker mutexLocker(&m_mutex);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (MsgConfigureMessageCenter::match(cmd))
    {
        const MsgConfigureMessageCenter& cfg = (const MsgConfigureMessageCenter&) cmd;
        applySettings(cfg.getSettings(), cfg.getSettingsKeys(), cfg.getForce());
        return true;
    }
    else if (MainCore::MsgMessageRecord::match(cmd))
    {
        const MainCore::MsgMessageRecord& msg = (const MainCore::MsgMessageRecord&) cmd;
        ingest(msg.getRecord(), msg.getPipeSource());
        return true;
    }
    else if (MsgPostPage::match(cmd))
    {
        const MsgPostPage& msg = (const MsgPostPage&) cmd;
        QString id = m_engine.addPage(msg.getRecord(), now);

        if (id.isEmpty()) {
            m_errors.append("A page needs a transmitter and an id of its own");
        }

        m_dirty = true;
        return true;
    }
    else if (MsgPageAction::match(cmd))
    {
        const MsgPageAction& msg = (const MsgPageAction&) cmd;

        if (msg.getAction() == MsgPageAction::Requeue) {
            m_engine.requeue(msg.getId(), now);
        } else if (msg.getAction() == MsgPageAction::Cancel) {
            m_engine.cancel(msg.getId());
        } else {
            m_engine.removeFinished();
        }

        m_dirty = true;
        return true;
    }
    else if (MsgRedecoded::match(cmd))
    {
        const MsgRedecoded& msg = (const MsgRedecoded&) cmd;
        m_engine.replace(msg.getRecord());
        m_dirty = true;
        return true;
    }
    else if (MsgClearLog::match(cmd))
    {
        m_engine.clearLog();
        m_dirty = true;
        return true;
    }

    return false;
}

// A record of a channel. The channels that announce themselves as transmitters are kept to give them
// their pages
void MessageCenter::ingest(const MessageRecord& record, const QObject *source)
{
    MessageEndpoint endpoint;
    QString event;

    if (MessageEndpoint::fromRecord(record, endpoint, event) && (endpoint.m_role == MessageEndpoint::Transmitter))
    {
        ChannelAPI *channel = qobject_cast<ChannelAPI*>(const_cast<QObject*>(source));

        if ((event == "unregister") || !channel) {
            m_transmitters.remove(endpoint.m_alias);
        } else {
            m_transmitters[endpoint.m_alias] = channel;
        }
    }

    m_engine.ingest(record, "channels", QDateTime::currentMSecsSinceEpoch());
    m_dirty = true;
}

void MessageCenter::tick()
{
    QMutexLocker mutexLocker(&m_mutex);
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList errors;

    if (m_engine.pollSources(errors) > 0) {
        m_dirty = true;
    }

    m_errors.append(errors);
    m_engine.expireClaims(now, (qint64) m_settings.m_claimTimeoutSec * 1000);
    m_engine.sweep(now, (qint64) m_settings.m_silentSec * 1000);

    // The pages whose transmitter is ready: its channel takes (claims) the page or refuses it
    for (const MessageCenterPage& page : m_engine.dispatch(now))
    {
        QString error;
        int code = postToTransmitter(page, error);

        if (code / 100 == 2) {
            m_engine.applyEvent(page.m_record.m_id, "claimed", QString(), now);
        } else {
            m_engine.applyEvent(page.m_record.m_id, "rejected", error, now);
        }

        m_dirty = true;
    }

    m_errors.append(m_engine.takeSinkErrors());

    if (m_dirty && getMessageQueueToGUI())
    {
        getMessageQueueToGUI()->push(MsgUpdated::create());
        m_dirty = false;
    }
}

// The "tx" action of the paging modulator, as the WebAPI posts it, called in process
int MessageCenter::postToTransmitter(const MessageCenterPage& page, QString& error)
{
    ChannelAPI *channel = m_transmitters.value(page.transmitter());

    if (!channel)
    {
        error = "The transmitter is not there";
        return 404;
    }

    SWGSDRangel::SWGChannelActions query;
    SWGSDRangel::SWGRadioPagingModActions *actions = new SWGSDRangel::SWGRadioPagingModActions();
    SWGSDRangel::SWGRadioPagingModTx *tx = new SWGSDRangel::SWGRadioPagingModTx();

    RadioPagingPage::fromRecord(page.m_record).toTx(*tx);
    actions->setTx(tx);
    query.setRadioPagingModActions(actions);
    QStringList keys = {"tx"};
    return channel->webapiActionsPost(keys, query, error);
}

QStringList MessageCenter::takeErrors()
{
    QMutexLocker mutexLocker(&m_mutex);
    QStringList errors = m_errors;
    m_errors.clear();
    return errors;
}

QByteArray MessageCenter::serialize() const
{
    return m_settings.serialize();
}

bool MessageCenter::deserialize(const QByteArray& data)
{
    bool ok = m_settings.deserialize(data);

    if (!ok) {
        m_settings.resetToDefaults();
    }

    m_inputMessageQueue.push(MsgConfigureMessageCenter::create(m_settings, QStringList(), true));
    return ok;
}

void MessageCenter::applySettings(const MessageCenterSettings& settings, const QStringList& settingsKeys, bool force)
{
    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (force || settingsKeys.contains("logSize") || settingsKeys.contains("routes") || settingsKeys.contains("sinks")
        || settingsKeys.contains("sources") || settingsKeys.contains("historyEnabled") || settingsKeys.contains("historyMaxMB")) {
        applyEngineSettings();
    }

    if (m_settings.m_useReverseAPI)
    {
        bool fullUpdate = (settingsKeys.contains("useReverseAPI") && m_settings.m_useReverseAPI) ||
                settingsKeys.contains("reverseAPIAddress") ||
                settingsKeys.contains("reverseAPIPort") ||
                settingsKeys.contains("reverseAPIFeatureSetIndex") ||
                settingsKeys.contains("reverseAPIFeatureIndex");
        webapiReverseSendSettings(settingsKeys, m_settings, fullUpdate || force);
    }
}

// The settings that changed (all of them but the reverse API's when force is on) to the peer, as a PATCH of
// the settings of its message center
void MessageCenter::webapiReverseSendSettings(const QList<QString>& featureSettingsKeys, const MessageCenterSettings& settings, bool force)
{
    SWGSDRangel::SWGFeatureSettings *swgFeatureSettings = new SWGSDRangel::SWGFeatureSettings();
    swgFeatureSettings->setFeatureType(new QString("MessageCenter"));
    swgFeatureSettings->setMessageCenterSettings(new SWGSDRangel::SWGMessageCenterSettings());
    SWGSDRangel::SWGMessageCenterSettings *swg = swgFeatureSettings->getMessageCenterSettings();

    if (featureSettingsKeys.contains("title") || force) {
        swg->setTitle(new QString(settings.m_title));
    }
    if (featureSettingsKeys.contains("rgbColor") || force) {
        swg->setRgbColor(settings.m_rgbColor);
    }
    if (featureSettingsKeys.contains("logSize") || force) {
        swg->setLogSize(settings.m_logSize);
    }
    if (featureSettingsKeys.contains("historyEnabled") || force) {
        swg->setHistoryEnabled(settings.m_historyEnabled ? 1 : 0);
    }
    if (featureSettingsKeys.contains("historyMaxMB") || force) {
        swg->setHistoryMaxMb(settings.m_historyMaxMB);
    }
    if (featureSettingsKeys.contains("claimTimeoutSec") || force) {
        swg->setClaimTimeoutSec(settings.m_claimTimeoutSec);
    }
    if (featureSettingsKeys.contains("silentSec") || force) {
        swg->setSilentSec(settings.m_silentSec);
    }
    if (featureSettingsKeys.contains("routes") || force) {
        swg->setRoutes(new QString(QString::fromUtf8(MessageCenterSettings::routesToJson(settings.m_routes))));
    }
    if (featureSettingsKeys.contains("sinks") || force) {
        swg->setSinks(new QString(QString::fromUtf8(MessageCenterSettings::filesToJson(settings.m_sinks))));
    }
    if (featureSettingsKeys.contains("sources") || force) {
        swg->setSources(new QString(QString::fromUtf8(MessageCenterSettings::filesToJson(settings.m_sources))));
    }

    QString featureSettingsURL = QString("http://%1:%2/sdrangel/featureset/%3/feature/%4/settings")
            .arg(settings.m_reverseAPIAddress)
            .arg(settings.m_reverseAPIPort)
            .arg(settings.m_reverseAPIFeatureSetIndex)
            .arg(settings.m_reverseAPIFeatureIndex);
    m_networkRequest.setUrl(QUrl(featureSettingsURL));
    m_networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QBuffer *buffer = new QBuffer();
    buffer->open((QBuffer::ReadWrite));
    buffer->write(swgFeatureSettings->asJson().toUtf8());
    buffer->seek(0);

    // Always use PATCH to avoid passing reverse API settings
    QNetworkReply *reply = m_networkManager->sendCustomRequest(m_networkRequest, "PATCH", buffer);
    buffer->setParent(reply);

    delete swgFeatureSettings;
}

void MessageCenter::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "MessageCenter::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("MessageCenter::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}

// The sinks, sources and routes of the settings in the engine
void MessageCenter::applyEngineSettings()
{
    QMutexLocker mutexLocker(&m_mutex);
    m_engine.setLogSize(m_settings.m_logSize);
    applyHistory();

    for (const QString& name : m_engine.sinkNames())
    {
        if ((name != "log") && (name != "queue")) {
            m_engine.removeSink(name);
        }
    }
    for (const QString& name : m_engine.sourceNames()) {
        m_engine.removeSource(name);
    }

    for (const MessageCenterFileSetting& sink : m_settings.m_sinks)
    {
        if (!sink.m_name.isEmpty() && !sink.m_path.isEmpty() && (sink.m_name != "log") && (sink.m_name != "queue"))
        {
            m_engine.addSink(std::unique_ptr<MessageCenterSink>(new MessageCenterFileSink(sink.m_name, sink.m_path,
                (MessageCenterFileSink::Format) sink.m_format)));
        }
    }
    for (const MessageCenterFileSetting& source : m_settings.m_sources)
    {
        if (!source.m_name.isEmpty() && !source.m_path.isEmpty()) {
            m_engine.addSource(std::unique_ptr<MessageCenterSource>(new MessageCenterFileSource(source.m_name, source.m_path)));
        }
    }

    m_engine.setRoutes(m_settings.m_routes);
    m_dirty = true;
}

// ---------------------------------------------------------------------------
// WebAPI
// ---------------------------------------------------------------------------

void MessageCenter::webapiFormatFeatureSettings(SWGSDRangel::SWGFeatureSettings& response, const MessageCenterSettings& settings)
{
    SWGSDRangel::SWGMessageCenterSettings *swg = response.getMessageCenterSettings();

    swg->setTitle(new QString(settings.m_title));
    swg->setRgbColor(settings.m_rgbColor);
    swg->setLogSize(settings.m_logSize);
    swg->setHistoryEnabled(settings.m_historyEnabled ? 1 : 0);
    swg->setHistoryMaxMb(settings.m_historyMaxMB);
    swg->setClaimTimeoutSec(settings.m_claimTimeoutSec);
    swg->setSilentSec(settings.m_silentSec);
    swg->setRoutes(new QString(QString::fromUtf8(MessageCenterSettings::routesToJson(settings.m_routes))));
    swg->setSinks(new QString(QString::fromUtf8(MessageCenterSettings::filesToJson(settings.m_sinks))));
    swg->setSources(new QString(QString::fromUtf8(MessageCenterSettings::filesToJson(settings.m_sources))));
    swg->setUseReverseApi(settings.m_useReverseAPI ? 1 : 0);

    if (swg->getReverseApiAddress()) {
        *swg->getReverseApiAddress() = settings.m_reverseAPIAddress;
    } else {
        swg->setReverseApiAddress(new QString(settings.m_reverseAPIAddress));
    }

    swg->setReverseApiPort(settings.m_reverseAPIPort);
    swg->setReverseApiFeatureSetIndex(settings.m_reverseAPIFeatureSetIndex);
    swg->setReverseApiFeatureIndex(settings.m_reverseAPIFeatureIndex);
}

void MessageCenter::webapiUpdateFeatureSettings(MessageCenterSettings& settings, const QStringList& featureSettingsKeys,
    SWGSDRangel::SWGFeatureSettings& response)
{
    SWGSDRangel::SWGMessageCenterSettings *swg = response.getMessageCenterSettings();

    if (featureSettingsKeys.contains("title") && swg->getTitle()) {
        settings.m_title = *swg->getTitle();
    }
    if (featureSettingsKeys.contains("rgbColor")) {
        settings.m_rgbColor = (quint32) swg->getRgbColor();
    }
    if (featureSettingsKeys.contains("logSize")) {
        settings.m_logSize = std::max(100, std::min(1000000, swg->getLogSize()));
    }
    if (featureSettingsKeys.contains("historyEnabled")) {
        settings.m_historyEnabled = swg->getHistoryEnabled() != 0;
    }
    if (featureSettingsKeys.contains("historyMaxMB")) {
        settings.m_historyMaxMB = std::max(1, std::min(10000, swg->getHistoryMaxMb()));
    }
    if (featureSettingsKeys.contains("claimTimeoutSec")) {
        settings.m_claimTimeoutSec = std::max(1, std::min(3600, swg->getClaimTimeoutSec()));
    }
    if (featureSettingsKeys.contains("silentSec")) {
        settings.m_silentSec = std::max(6, std::min(3600, swg->getSilentSec()));
    }
    if (featureSettingsKeys.contains("routes") && swg->getRoutes()) {
        settings.m_routes = MessageCenterSettings::routesFromJson(swg->getRoutes()->toUtf8());
    }
    if (featureSettingsKeys.contains("sinks") && swg->getSinks()) {
        settings.m_sinks = MessageCenterSettings::filesFromJson(swg->getSinks()->toUtf8());
    }
    if (featureSettingsKeys.contains("sources") && swg->getSources()) {
        settings.m_sources = MessageCenterSettings::filesFromJson(swg->getSources()->toUtf8());
    }
    if (featureSettingsKeys.contains("useReverseAPI")) {
        settings.m_useReverseAPI = swg->getUseReverseApi() != 0;
    }
    if (featureSettingsKeys.contains("reverseAPIAddress") && swg->getReverseApiAddress()) {
        settings.m_reverseAPIAddress = *swg->getReverseApiAddress();
    }
    if (featureSettingsKeys.contains("reverseAPIPort")) {
        settings.m_reverseAPIPort = swg->getReverseApiPort();
    }
    if (featureSettingsKeys.contains("reverseAPIFeatureSetIndex")) {
        settings.m_reverseAPIFeatureSetIndex = swg->getReverseApiFeatureSetIndex();
    }
    if (featureSettingsKeys.contains("reverseAPIFeatureIndex")) {
        settings.m_reverseAPIFeatureIndex = swg->getReverseApiFeatureIndex();
    }
}

int MessageCenter::webapiSettingsGet(SWGSDRangel::SWGFeatureSettings& response, QString& errorMessage)
{
    (void) errorMessage;
    QMutexLocker mutexLocker(&m_mutex);
    response.setMessageCenterSettings(new SWGSDRangel::SWGMessageCenterSettings());
    response.getMessageCenterSettings()->init();
    webapiFormatFeatureSettings(response, m_settings);
    return 200;
}

int MessageCenter::webapiSettingsPutPatch(bool force, const QStringList& featureSettingsKeys,
    SWGSDRangel::SWGFeatureSettings& response, QString& errorMessage)
{
    SWGSDRangel::SWGMessageCenterSettings *swg = response.getMessageCenterSettings();

    // The lists are JSON texts: refused if they are not
    for (const char *key : {"routes", "sinks", "sources"})
    {
        QString *text = (QString(key) == "routes") ? swg->getRoutes() : (QString(key) == "sinks") ? swg->getSinks() : swg->getSources();

        if (featureSettingsKeys.contains(key) && text && !QJsonDocument::fromJson(text->toUtf8()).isArray())
        {
            errorMessage = QString("%1 must be a JSON array").arg(key);
            return 400;
        }
    }

    MessageCenterSettings settings;
    {
        QMutexLocker mutexLocker(&m_mutex);
        settings = m_settings;
    }

    webapiUpdateFeatureSettings(settings, featureSettingsKeys, response);
    m_inputMessageQueue.push(MsgConfigureMessageCenter::create(settings, featureSettingsKeys, force));

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgConfigureMessageCenter::create(settings, featureSettingsKeys, force));
    }

    webapiFormatFeatureSettings(response, settings);
    return 200;
}

int MessageCenter::webapiReportGet(SWGSDRangel::SWGFeatureReport& response, QString& errorMessage)
{
    (void) errorMessage;
    static const char *roles[] = {"rx", "tx"};
    static const char *statuses[] = {"offline", "idle", "busy"};
    QMutexLocker mutexLocker(&m_mutex);
    response.setMessageCenterReport(new SWGSDRangel::SWGMessageCenterReport());
    response.getMessageCenterReport()->init();
    SWGSDRangel::SWGMessageCenterReport *swg = response.getMessageCenterReport();

    QJsonArray endpoints;

    for (const MessageEndpoint& e : m_engine.endpoints())
    {
        QJsonObject o;
        o["alias"] = e.m_alias;
        o["role"] = roles[(e.m_role == MessageEndpoint::Transmitter) ? 1 : 0];
        o["status"] = statuses[(int) e.m_status];
        o["protocols"] = QJsonArray::fromStringList(e.m_protocols);
        QJsonArray speeds, polarities;

        for (int s : e.m_speeds) {
            speeds.append(s);
        }
        for (int p : e.m_polarities) {
            polarities.append(p);
        }

        o["speeds"] = speeds;
        o["polarities"] = polarities;
        o["queueDepth"] = e.m_queueDepth;
        o["capacity"] = e.m_capacity;
        o["frequency"] = (double) e.m_frequency;
        o["lastSeen"] = (double) e.m_lastSeen;
        endpoints.append(o);
    }

    QJsonArray pages;

    for (const MessageCenterPage& p : m_engine.pages())
    {
        QJsonObject o;
        o["id"] = p.m_record.m_id;
        o["transmitter"] = p.transmitter();
        o["protocol"] = p.m_record.m_protocol;
        o["address"] = p.m_record.m_address;
        o["text"] = p.m_record.m_text;
        o["state"] = MessageCenterPage::stateName(p.m_state);
        o["attempts"] = p.m_attempts;
        o["retransmits"] = p.m_retransmits;
        o["reason"] = p.m_reason;
        o["claimedBy"] = p.m_claimedBy;
        pages.append(o);
    }

    QJsonArray log;
    const std::deque<MessageRecord>& records = m_engine.log();
    size_t first = (records.size() > 50) ? records.size() - 50 : 0;

    for (size_t i = first; i < records.size(); i++) {
        log.append(records[i].toJson());
    }

    swg->setLogged(m_engine.logged());
    swg->setRouted(m_engine.routed());
    swg->setEndpoints(new QString(QString::fromUtf8(QJsonDocument(endpoints).toJson(QJsonDocument::Compact))));
    swg->setPages(new QString(QString::fromUtf8(QJsonDocument(pages).toJson(QJsonDocument::Compact))));
    swg->setLog(new QString(QString::fromUtf8(QJsonDocument(log).toJson(QJsonDocument::Compact))));
    swg->setErrors(new QString(takeErrors().join('\n')));
    return 200;
}

int MessageCenter::webapiActionsPost(const QStringList& featureActionsKeys, SWGSDRangel::SWGFeatureActions& query, QString& errorMessage)
{
    SWGSDRangel::SWGMessageCenterActions *swg = query.getMessageCenterActions();

    if (!swg)
    {
        errorMessage = "Missing MessageCenterActions in query";
        return 400;
    }

    int actions = (featureActionsKeys.contains("post") ? 1 : 0) + (featureActionsKeys.contains("requeue") ? 1 : 0)
        + (featureActionsKeys.contains("cancel") ? 1 : 0) + (featureActionsKeys.contains("clearSent") ? 1 : 0)
        + (featureActionsKeys.contains("redecode") ? 1 : 0);

    if (actions != 1)
    {
        errorMessage = "One action expected: post, requeue, cancel, clearSent or redecode";
        return 400;
    }

    // A received message of the log decoded again: checked here, the log changes in the thread of the feature
    if (featureActionsKeys.contains("redecode"))
    {
        SWGSDRangel::SWGMessageCenterRedecode *redecode = swg->getRedecode();
        const QString id = (redecode && redecode->getId()) ? *redecode->getId() : QString();
        const QString type = (redecode && redecode->getType()) ? redecode->getType()->toLower() : QString();
        const MessageRecord *record = m_engine.find(id);
        MessageRecord out;

        if (!record)
        {
            errorMessage = QString("No record %1 in the log").arg(id);
            return 404;
        }
        if (!RadioPagingRecode::redecode(*record, RadioPagingPage::charsetKey(redecode->getCharset()), type, out, errorMessage)) {
            return 400;
        }

        m_inputMessageQueue.push(MsgRedecoded::create(out));
        return 202;
    }

    if (featureActionsKeys.contains("post"))
    {
        SWGSDRangel::SWGMessageCenterPost *post = swg->getPost();

        if (!post)
        {
            errorMessage = "Missing post";
            return 400;
        }

        auto text = [](QString *s) { return s ? *s : QString(); };
        QString polarity = text(post->getPolarity()).toLower();
        RadioPagingPage page;
        page.m_id = text(post->getId());
        page.m_transmitter = text(post->getTransmitter());
        page.m_protocol = text(post->getProtocol()).toLower();
        page.m_type = text(post->getType()).toLower();
        page.m_address = text(post->getAddress()).trimmed();
        page.m_function = post->getFunction();
        page.m_priority = post->getPriority() != 0;
        page.m_groupMembers = text(post->getGroupMembers());
        page.m_text = text(post->getText());
        page.m_voiceFile = text(post->getVoiceFile());
        page.m_speed = post->getSpeed();
        page.m_polarity = polarity.isEmpty() ? -1 : (polarity == "normal") ? 0 : (polarity == "inverted") ? 1 : -2;
        page.m_shortType = post->getShortType();
        page.m_source = post->getSource();
        page.m_blocking = (post->getBlocking() == 0) ? 1 : post->getBlocking();
        page.m_mailDrop = post->getMailDrop() != 0;
        page.m_rightToLeft = post->getRightToLeft() != 0;
        page.m_headerMessage = post->getHeaderMessage() != 0;
        page.m_statusInfo = post->getStatusInfo() != 0;
        page.m_secureType = post->getSecureType();
        page.m_retransmit = post->getRetransmit();
        // 0 (not given) or negative: the default of the transmitter
        page.m_retransmitInterval = (post->getRetransmitInterval() > 0.0f) ? post->getRetransmitInterval() : -1.0f;
        page.m_sendDelay = post->getSendDelay();
        page.m_charset = RadioPagingPage::charsetKey(post->getCharset());

        if (page.m_transmitter.isEmpty())
        {
            errorMessage = "A page needs a transmitter (its alias)";
            return 400;
        }

        // The limits of the page here; the transmitter checks the rest (its mode, numbering, the encoder)
        errorMessage = page.check();

        if (!errorMessage.isEmpty()) {
            return 400;
        }

        m_inputMessageQueue.push(MsgPostPage::create(page.toRecord()));
        return 202;
    }
    if (featureActionsKeys.contains("requeue") && swg->getRequeue())
    {
        m_inputMessageQueue.push(MsgPageAction::create(*swg->getRequeue(), MsgPageAction::Requeue));
        return 202;
    }
    if (featureActionsKeys.contains("cancel") && swg->getCancel())
    {
        m_inputMessageQueue.push(MsgPageAction::create(*swg->getCancel(), MsgPageAction::Cancel));
        return 202;
    }
    if (featureActionsKeys.contains("clearSent"))
    {
        m_inputMessageQueue.push(MsgPageAction::create(QString(), MsgPageAction::ClearSent));
        return 202;
    }

    errorMessage = "Missing the id of the page";
    return 400;
}
