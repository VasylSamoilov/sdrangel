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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "SWGChannelActions.h"
#include "SWGFeatureActions.h"
#include "SWGFeatureReport.h"
#include "SWGFeatureSettings.h"
#include "SWGMessageCenterActions.h"
#include "SWGMessageCenterPost.h"
#include "SWGMessageCenterReport.h"
#include "SWGMessageCenterSettings.h"
#include "SWGRadioPagingModActions.h"
#include "SWGRadioPagingModTx.h"

#include "channel/channelapi.h"
#include "maincore.h"

#include "messagecenter.h"

MESSAGE_CLASS_DEFINITION(MessageCenter::MsgConfigureMessageCenter, Message)
MESSAGE_CLASS_DEFINITION(MessageCenter::MsgPostPage, Message)
MESSAGE_CLASS_DEFINITION(MessageCenter::MsgPageAction, Message)
MESSAGE_CLASS_DEFINITION(MessageCenter::MsgUpdated, Message)

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
}

MessageCenter::~MessageCenter()
{
    m_timer.stop();
    QObject::disconnect(&m_availableChannelHandler, &AvailableChannelOrFeatureHandler::messageEnqueued,
        this, &MessageCenter::handleChannelMessageQueue);
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

    const MessageRecord& r = page.m_record;
    const QVariantMap& d = r.m_protocolData;
    SWGSDRangel::SWGChannelActions query;
    SWGSDRangel::SWGRadioPagingModActions *actions = new SWGSDRangel::SWGRadioPagingModActions();
    SWGSDRangel::SWGRadioPagingModTx *tx = new SWGSDRangel::SWGRadioPagingModTx();
    // The modulator takes the record types (FLEX tone: the address alone; short_message: digits, none for tone only)
    QString type = r.m_msgType.isEmpty() ? QString("alphanumeric") : r.m_msgType;

    tx->setId(new QString(r.m_id));
    tx->setProtocol(new QString(r.m_protocol));
    tx->setType(new QString(type));
    tx->setText(new QString((type == "voice") ? d.value("voiceFile").toString() : r.m_text));
    tx->setPolarity(new QString((r.m_polarity == MessageRecord::PolarityInverted) ? "inverted" : (r.m_polarity == MessageRecord::PolarityNormal) ? "normal" : ""));
    tx->setFunction(d.value("function").toInt());

    if (r.m_protocol == "pocsag")
    {
        tx->setAddress(d.value("address").toInt());
        tx->setSpeed((r.m_speed > 0) ? r.m_speed : 0);
    }
    else if (r.m_protocol == "flex")
    {
        tx->setCapcode(d.value("capcode").toLongLong());
        tx->setPriority(d.value("priority").toInt());

        if (type == "short_message")
        {
            tx->setShortType(d.value("short_type").toInt());
            tx->setSource(d.value("short_source").toInt());
        }
    }
    else {
        tx->setCapcode(d.value("address").toLongLong());       // GSC: the address as an integer of up to 6 digits
    }

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

MessageRecord MessageCenter::pageRecord(const QString& transmitter, const QString& protocol, const QString& address, int function,
    int priority, const QString& type, const QString& text, const QString& voiceFile, int speed, const QString& polarity)
{
    MessageRecord r;
    r.m_direction = MessageRecord::DirectionTX;
    r.m_kind = MessageRecord::KindMessage;
    r.m_protocol = protocol;
    r.m_msgType = type.isEmpty() ? QString("alphanumeric") : type;
    r.m_text = ((r.m_msgType == "tone") || (r.m_msgType == "voice")) ? QString() : text;
    r.m_polarity = (polarity == "normal") ? MessageRecord::PolarityNormal
        : (polarity == "inverted") ? MessageRecord::PolarityInverted : MessageRecord::PolarityUnknown;
    r.m_speed = (speed > 0) ? speed : -1;
    r.m_protocolData.insert("transmitter", transmitter);
    r.m_protocolData.insert("address", address);
    r.m_protocolData.insert("capcode", address);
    r.m_protocolData.insert("function", function);
    r.m_protocolData.insert("priority", priority);
    r.m_protocolData.insert("voiceFile", voiceFile);

    if (protocol == "pocsag") {
        r.m_address = QString("%1%2").arg(address.toLongLong(), 7, 10, QChar('0')).arg(QChar('A' + (function & 3)));
    } else if (protocol == "gsc") {
        r.m_address = address.rightJustified(6, QChar('0')) + QChar('1' + (function & 3));
    } else {
        r.m_address = address;
    }

    return r;
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
        || settingsKeys.contains("sources")) {
        applyEngineSettings();
    }
}

// The sinks, sources and routes of the settings in the engine
void MessageCenter::applyEngineSettings()
{
    QMutexLocker mutexLocker(&m_mutex);
    m_engine.setLogSize(m_settings.m_logSize);

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
    swg->setClaimTimeoutSec(settings.m_claimTimeoutSec);
    swg->setSilentSec(settings.m_silentSec);
    swg->setRoutes(new QString(QString::fromUtf8(MessageCenterSettings::routesToJson(settings.m_routes))));
    swg->setSinks(new QString(QString::fromUtf8(MessageCenterSettings::filesToJson(settings.m_sinks))));
    swg->setSources(new QString(QString::fromUtf8(MessageCenterSettings::filesToJson(settings.m_sources))));
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
        + (featureActionsKeys.contains("cancel") ? 1 : 0) + (featureActionsKeys.contains("clearSent") ? 1 : 0);

    if (actions != 1)
    {
        errorMessage = "One action expected: post, requeue, cancel or clearSent";
        return 400;
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
        QString protocol = text(post->getProtocol()).toLower();
        QString type = text(post->getType()).toLower();
        QString polarity = text(post->getPolarity()).toLower();
        QString address = text(post->getAddress()).trimmed();
        QString transmitter = text(post->getTransmitter());

        if (transmitter.isEmpty())
        {
            errorMessage = "A page needs a transmitter (its alias)";
            return 400;
        }
        if ((protocol != "pocsag") && (protocol != "flex") && (protocol != "gsc"))
        {
            errorMessage = "Protocol must be pocsag, flex or gsc";
            return 400;
        }
        if (!type.isEmpty() && (type != "alphanumeric") && (type != "numeric") && (type != "tone") && (type != "voice")
         && !((type == "short_message") && (protocol == "flex")))
        {
            errorMessage = "Type must be alphanumeric, numeric, tone or voice (FLEX: or short_message)";
            return 400;
        }
        if ((type == "voice") && ((protocol != "gsc") || text(post->getVoiceFile()).isEmpty()))
        {
            errorMessage = "A voice page is GSC and needs the WAV file (voiceFile)";
            return 400;
        }
        bool addressOk = false;
        qlonglong addressValue = address.toLongLong(&addressOk);

        if (!addressOk || (addressValue < 0))
        {
            errorMessage = "The address must be a number";
            return 400;
        }
        if ((post->getFunction() < 0) || (post->getFunction() > 3))
        {
            errorMessage = "Function must be 0 to 3";
            return 400;
        }
        if (!polarity.isEmpty() && (polarity != "normal") && (polarity != "inverted"))
        {
            errorMessage = "Polarity must be normal or inverted";
            return 400;
        }

        if ((type == "short_message")
         && ((post->getShortType() < 0) || (post->getShortType() > 2) || (post->getSource() < 0) || (post->getSource() > 7)))
        {
            errorMessage = "FLEX short message: shortType 0 (numeric), 1 (source) or 2 (numbered), source 0 to 7";
            return 400;
        }

        MessageRecord record = pageRecord(transmitter, protocol, address, post->getFunction(), post->getPriority(), type,
            text(post->getText()), text(post->getVoiceFile()), post->getSpeed(), polarity);
        record.m_id = text(post->getId());

        if (type == "short_message")
        {
            record.m_protocolData.insert("short_type", post->getShortType());
            record.m_protocolData.insert("short_source", post->getSource());
        }
        m_inputMessageQueue.push(MsgPostPage::create(record));
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
