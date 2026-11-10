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
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStorageInfo>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>

#include "SWGChannelSettings.h"
#include "SWGWorkspaceInfo.h"
#include "SWGChannelReport.h"
#include "SWGChannelActions.h"
#include "SWGRadioPagingModSettings.h"
#include "SWGRadioPagingModReport.h"
#include "SWGRadioPagingModActions.h"
#include "SWGRadioPagingModTx.h"

#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
#include "settings/serializable.h"
#include "dsp/wavfilerecord.h"
#include "util/db.h"
#include "maincore.h"
#include "util/messageendpoint.h"
#include "radiopagingwav.h"
#include "radiopagingvoicestore.h"

#include "radiopagingmodbaseband.h"
#include "radiopagingmod.h"

MESSAGE_CLASS_DEFINITION(RadioPagingMod::MsgConfigureRadioPagingMod, Message)
MESSAGE_CLASS_DEFINITION(RadioPagingMod::MsgTx, Message)
MESSAGE_CLASS_DEFINITION(RadioPagingMod::MsgReportTx, Message)
MESSAGE_CLASS_DEFINITION(RadioPagingMod::MsgTxEvent, Message)
MESSAGE_CLASS_DEFINITION(RadioPagingMod::MsgTxLog, Message)
MESSAGE_CLASS_DEFINITION(RadioPagingMod::MsgLiveVoice, Message)
MESSAGE_CLASS_DEFINITION(RadioPagingMod::MsgLiveEnd, Message)
MESSAGE_CLASS_DEFINITION(RadioPagingMod::MsgClear, Message)

const char* const RadioPagingMod::m_channelIdURI = "sdrangel.channeltx.modradiopaging";
const char* const RadioPagingMod::m_channelId = "RadioPagingMod";
const int RadioPagingMod::m_maxQueued;
const int RadioPagingMod::m_maxTextLength;

RadioPagingMod::RadioPagingMod(DeviceAPI *deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSource),
    m_deviceAPI(deviceAPI),
    m_nextId(1),
    m_accepted(0)
{
    setObjectName(m_channelId);

    m_thread = new QThread(this);
    m_thread->setObjectName("RadioPagingMod");
    m_basebandSource = new RadioPagingModBaseband();
    m_basebandSource->setChannel(this);
    m_basebandSource->moveToThread(m_thread);
    // Sent or dropped: the main thread logs the message and records its voice
    m_basebandSource->setNotify([this](quint64 id, bool sent) {
        getInputMessageQueue()->push(MsgTxEvent::create(id, sent ? MsgTxEvent::Sent : MsgTxEvent::Dropped));
    });
    m_basebandSource->setNotifyTransmitted([this](quint64 id) {
        getInputMessageQueue()->push(MsgTxEvent::create(id, MsgTxEvent::Transmitted));
    });
    m_basebandSource->setNotifyFailed([this](quint64 id) {
        getInputMessageQueue()->push(MsgTxEvent::create(id, MsgTxEvent::Failed));
    });

    applySettings(QStringList(), m_settings, true);

    m_deviceAPI->addChannelSource(this);
    m_deviceAPI->addChannelSourceAPI(this);

    // The message center learns this transmitter at the next announcement, whenever it starts
    m_announceTimer = new QTimer(this);
    QObject::connect(m_announceTimer, &QTimer::timeout, this, [this]() { announce("status"); });
    m_announceTimer->start(5000);

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &RadioPagingMod::networkManagerFinished
    );
}

RadioPagingMod::~RadioPagingMod()
{
    m_announceTimer->stop();
    announce("unregister");
    QObject::disconnect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &RadioPagingMod::networkManagerFinished
    );
    delete m_networkManager;
    m_deviceAPI->removeChannelSourceAPI(this);
    m_deviceAPI->removeChannelSource(this, true);
    stop();
    delete m_basebandSource;
    delete m_thread;
}

void RadioPagingMod::setDeviceAPI(DeviceAPI *deviceAPI)
{
    if (deviceAPI != m_deviceAPI)
    {
        m_deviceAPI->removeChannelSourceAPI(this);
        m_deviceAPI->removeChannelSource(this, false);
        m_deviceAPI = deviceAPI;
        m_deviceAPI->addChannelSource(this);
        m_deviceAPI->addChannelSourceAPI(this);
    }
}

void RadioPagingMod::start()
{
    qDebug("RadioPagingMod::start");
    m_basebandSource->reset();
    m_thread->start();
}

void RadioPagingMod::stop()
{
    qDebug("RadioPagingMod::stop");
    m_thread->exit();
    m_thread->wait();
}

void RadioPagingMod::pull(SampleVector::iterator& begin, unsigned int nbSamples)
{
    m_basebandSource->pull(begin, nbSamples);
}

// Messages accepted and neither sent nor dropped yet, those being sent included. The source
// only counts those that have reached it: the others still wait in the baseband message
// queue (all of them while the device is stopped)
int RadioPagingMod::outstanding() const
{
    RadioPagingModSource::Status status = m_basebandSource->getStatus();
    quint64 done = status.m_sent + status.m_dropped;
    quint64 accepted = m_accepted;

    return (accepted > done) ? (int) (accepted - done) : 0;
}

// The request is resolved with the channel settings and tried on a stream of its own (the
// message encodes; FLEX: some frame of the collapse carries its capcode)
QString RadioPagingMod::checkRequest(const TxRequest& request, RadioPagingModSource::Request& sourceRequest, QString *warning)
{
    QMutexLocker mutexLocker(&m_settingsMutex);
    std::string error;

    // A FLEX network with the mixing option also sends POCSAG messages
    bool mixing = (m_settings.m_protocol == RadioPagingModSettings::Flex) && (m_settings.m_mode == RadioPagingModSettings::Network)
        && m_settings.m_flexMixPocsag;
    RadioPagingModSettings::Protocol protocol = (request.m_protocol >= 0)
        ? (RadioPagingModSettings::Protocol) request.m_protocol : m_settings.m_protocol;

    if ((protocol != m_settings.m_protocol) && !(mixing && (protocol == RadioPagingModSettings::Pocsag)))
    {
        if ((protocol == RadioPagingModSettings::Pocsag) && (m_settings.m_protocol == RadioPagingModSettings::Flex)) {
            return "POCSAG messages on a FLEX channel need the network mode with mixing with POCSAG";
        }

        return QString("The channel transmits %1").arg(RadioPagingModSettings::protocolName(m_settings.m_protocol));
    }
    if ((request.m_polarity < -1) || (request.m_polarity > 1)) {
        return "Polarity must be normal or inverted";
    }

    sourceRequest = RadioPagingModSource::Request();
    sourceRequest.m_protocol = protocol;

    if (protocol == RadioPagingModSettings::Pocsag)
    {
        if ((request.m_speed != 0) && !RadioPagingModSettings::isPocsagSpeed(request.m_speed)) {
            return QString("POCSAG speed %1 baud is not 512, 1200 or 2400").arg(request.m_speed);
        }
        if (request.m_pocsag.m_text.size() > (size_t) m_maxTextLength) {
            return QString("Message text longer than %1 characters").arg(m_maxTextLength);
        }

        sourceRequest.m_pocsag = request.m_pocsag;
        sourceRequest.m_pocsagBaud = (request.m_speed != 0) ? request.m_speed : m_settings.m_pocsagSpeed;
        sourceRequest.m_pocsagInverted = (request.m_polarity < 0) ? m_settings.m_pocsagInverted : (request.m_polarity == 1);
        RadioPagingPocsagTxStream trial(false, sourceRequest.m_pocsagBaud, sourceRequest.m_pocsagInverted);

        if (!trial.add(sourceRequest.m_pocsag, sourceRequest.m_pocsagBaud, sourceRequest.m_pocsagInverted, 0, error)) {
            return QString::fromStdString(error);
        }
    }
    else if (protocol == RadioPagingModSettings::Flex)
    {
        if (request.m_flex.m_text.size() > (size_t) m_maxTextLength) {
            return QString("Message text longer than %1 characters").arg(m_maxTextLength);
        }

        sourceRequest.m_flex = request.m_flex;
        sourceRequest.m_flexInverted = (request.m_polarity < 0) ? m_settings.m_flexInverted : (request.m_polarity == 1);
        // Network: the capcode must listen to some frame of the collapse (mixed: of the frames
        // transmitted); one-shot frames are collapse 0
        bool added;

        if (mixing)
        {
            RadioPagingMixTxStream trial(RadioPagingModSource::mixConfig(m_settings));
            added = trial.addFlex(sourceRequest.m_flex, sourceRequest.m_flexInverted, 0, error);
        }
        else
        {
            RadioPagingFlexTxStream trial(m_settings.m_mode == RadioPagingModSettings::Network,
                (RadioPagingFlex::Mode) m_settings.m_flexMode, m_settings.m_flexInverted, m_settings.m_flexCollapse);
            added = trial.add(sourceRequest.m_flex, sourceRequest.m_flexInverted, 0, error);
        }

        if (!added) {
            return QString::fromStdString(error);
        }
    }
    else if (protocol == RadioPagingModSettings::Gsc)
    {
        if (request.m_gsc.m_text.size() > (size_t) m_maxTextLength) {
            return QString("Message text longer than %1 characters").arg(m_maxTextLength);
        }

        // GSC has no network mode: it is sent one-shot whatever the mode setting
        sourceRequest.m_gsc = request.m_gsc;
        sourceRequest.m_gscInverted = (request.m_polarity < 0) ? m_settings.m_gscInverted : (request.m_polarity == 1);
        RadioPagingGscTxStream trial((RadioPagingGsc::Mode) m_settings.m_gscMode);

        if ((request.m_gsc.m_type == RadioPagingGsc::Voice) && request.m_live)
        {
            // The voice comes from the microphone: nothing to load
            sourceRequest.m_live = true;

            if (!trial.addLive(sourceRequest.m_gsc, sourceRequest.m_gscInverted, 0, error, m_settings.m_gscVoiceSeconds)) {
                return QString::fromStdString(error);
            }

            if (outstanding() + m_inputMessageQueue.size() >= m_maxQueued) {
                return QString("Queue full (%1 messages not sent yet)").arg(m_maxQueued);
            }

            return QString();
        }
        else if ((request.m_gsc.m_type == RadioPagingGsc::Voice) && request.m_voice) {
            sourceRequest.m_voice = request.m_voice;    // Loaded by the check of the WebAPI request
        }
        else if (request.m_gsc.m_type == RadioPagingGsc::Voice)
        {
            // The sound file, cut at the voice length of the settings
            std::shared_ptr<RadioPagingWav::Audio> audio = std::make_shared<RadioPagingWav::Audio>();

            if (request.m_voiceFile.isEmpty()) {
                return "A voice page needs a sound file";
            }
            if (!RadioPagingWav::load(request.m_voiceFile.toStdString(), *audio, error, m_settings.m_gscVoiceSeconds, true)) {
                return QString("%1: %2").arg(request.m_voiceFile).arg(QString::fromStdString(error));
            }
            if (audio->m_trimmed)
            {
                QString text = QString("%1 is %2 s long, cut at %3 s").arg(request.m_voiceFile)
                    .arg(audio->m_fileSeconds, 0, 'f', 1).arg(m_settings.m_gscVoiceSeconds);
                qWarning() << "RadioPagingMod::checkRequest:" << text;

                if (warning) {
                    *warning = text;
                }
            }

            sourceRequest.m_voice = audio;
        }

        if (!trial.add(sourceRequest.m_gsc, sourceRequest.m_gscInverted, 0, error, sourceRequest.m_voice)) {
            return QString::fromStdString(error);
        }
    }
    else
    {
        return QString("%1 transmission is not implemented").arg(RadioPagingModSettings::protocolName(m_settings.m_protocol));
    }

    if (outstanding() + m_inputMessageQueue.size() >= m_maxQueued) {
        return QString("Queue full (%1 messages not sent yet)").arg(m_maxQueued);
    }

    return QString();
}

quint64 RadioPagingMod::transmit(const TxRequest& request, QString& error, QString *warning)
{
    QMutexLocker mutexLocker(&m_settingsMutex);
    RadioPagingModSource::Request sourceRequest;

    // A page is posted once
    if (!request.m_pageId.isEmpty())
    {
        for (const auto& entry : m_pending)
        {
            if (entry.second.m_pageId == request.m_pageId)
            {
                error = QString("Page %1 is already queued").arg(request.m_pageId);
                return 0;
            }
        }
    }

    error = checkRequest(request, sourceRequest, warning);

    if (!error.isEmpty()) {
        return 0;
    }

    sourceRequest.m_id = m_nextId++;
    m_accepted++;

    // The record of the message in the log of the transmitter, and for the message center once sent
    PendingTx pending;
    fillRecord(request, sourceRequest, sourceRequest.m_id, pending);
    m_pending[sourceRequest.m_id] = pending;

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgTxLog::create(pending.m_record, "queued"));
    }

    m_basebandSource->getInputMessageQueue()->push(RadioPagingModBaseband::MsgRequest::create(sourceRequest));

    return sourceRequest.m_id;
}

// The message as the receivers show it (see the records of the demodulator), with the channel
// alias of the transmitter
void RadioPagingMod::fillRecord(const TxRequest& request, const RadioPagingModSource::Request& sourceRequest, quint64 id, PendingTx& pending)
{
    MessageRecord& record = pending.m_record;
    record.resetToDefaults();
    // The id of a page of the message center is the id of its record
    pending.m_pageId = request.m_pageId;
    record.m_id = request.m_pageId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : request.m_pageId;
    record.m_timestamp = QDateTime::currentMSecsSinceEpoch();
    record.m_direction = MessageRecord::DirectionTX;
    record.m_kind = MessageRecord::KindMessage;
    record.m_frequency = m_centerFrequency + m_settings.m_inputFrequencyOffset;
    record.m_channelAlias = m_settings.effectiveChannelAlias(record.m_frequency);
    record.m_deviceSetIndex = getDeviceSetIndex();
    record.m_channelIndex = getIndexInDeviceSet();
    record.m_centerFrequency = m_centerFrequency;
    record.m_channelOffset = m_settings.m_inputFrequencyOffset;
    record.m_protocolData.insert("tx_id", (qulonglong) id);
    bool inverted = false;

    if (sourceRequest.m_protocol == RadioPagingModSettings::Pocsag)
    {
        const RadioPagingPocsag::TxMessage& m = sourceRequest.m_pocsag;
        record.m_protocol = "pocsag";
        record.m_mode = QString::number(sourceRequest.m_pocsagBaud);
        record.m_speed = sourceRequest.m_pocsagBaud;
        record.m_address = QString("%1%2").arg(m.m_address, 7, 10, QChar('0')).arg(QChar('A' + (m.m_function & 3)));
        record.m_msgType = (m.m_type == RadioPagingPocsag::Tone) ? "tone_only"
            : (m.m_type == RadioPagingPocsag::Numeric) ? "numeric" : "alphanumeric";
        record.m_text = QString::fromStdString(m.m_text);
        inverted = sourceRequest.m_pocsagInverted;
    }
    else if (sourceRequest.m_protocol == RadioPagingModSettings::Flex)
    {
        const RadioPagingFlexEncoder::TxMessage& m = sourceRequest.m_flex;
        RadioPagingFlex::Mode mode = (RadioPagingFlex::Mode) m_settings.m_flexMode;
        record.m_protocol = "flex";
        record.m_mode = RadioPagingFlex::modeName(mode);
        record.m_speed = RadioPagingFlex::bitRate(mode);
        record.m_address = QString::number((qulonglong) m.m_capcode);
        record.m_msgType = (m.m_type == RadioPagingFlex::Tone) ? "tone_only"
            : (m.m_type == RadioPagingFlex::Numeric) ? "numeric" : "alphanumeric";
        record.m_text = QString::fromStdString(m.m_text);
        inverted = sourceRequest.m_flexInverted;
    }
    else
    {
        const RadioPagingGsc::TxMessage& m = sourceRequest.m_gsc;
        record.m_protocol = "gsc";
        record.m_mode = "600";
        record.m_speed = 600;
        record.m_address = QString::fromStdString(m.m_address) + QChar('0' + m.m_addressNumber);
        record.m_msgType = (m.m_type == RadioPagingGsc::Voice) ? "voice" : (m.m_type == RadioPagingGsc::Tone) ? "tone_only"
            : (m.m_type == RadioPagingGsc::Numeric) ? "numeric" : "alphanumeric";
        record.m_text = QString::fromStdString(m.m_text);
        inverted = sourceRequest.m_gscInverted;
        pending.m_isVoice = (m.m_type == RadioPagingGsc::Voice);
        pending.m_voice = sourceRequest.m_voice;
    }

    record.m_polarity = inverted ? MessageRecord::PolarityInverted : MessageRecord::PolarityNormal;
    (void) request;
}

// A message was sent or dropped: the voice is recorded, the record goes to the log and, if sent, to
// the message pipes
void RadioPagingMod::txEvent(quint64 id, MsgTxEvent::Event event)
{
    auto it = m_pending.find(id);

    if (it == m_pending.end()) {
        return;
    }

    if (event == MsgTxEvent::Transmitted)
    {
        // Once: a voice page is on air with its first block and complete with its last
        if (!it->second.m_transmitted && !it->second.m_pageId.isEmpty()) {
            publishPageEvent(it->second.m_pageId, "transmitted");
        }

        it->second.m_transmitted = true;
        return;
    }

    bool sent = (event == MsgTxEvent::Sent);
    PendingTx pending = it->second;
    m_pending.erase(it);

    // A page of the message center: sent; rejected if it can not be transmitted at all; aborted if it
    // was on air; else released (nothing went out)
    if (!pending.m_pageId.isEmpty())
    {
        if (event == MsgTxEvent::Failed) {
            publishPageEvent(pending.m_pageId, "rejected", "the channel can not transmit it");
        } else {
            publishPageEvent(pending.m_pageId, sent ? "sent" : pending.m_transmitted ? "aborted" : "released");
        }
    }

    if (sent && pending.m_isVoice)
    {
        double seconds = 0.0;
        QString file = recordVoice(pending, seconds);

        pending.m_record.m_protocolData.insert("voice_seconds", seconds);

        if (!file.isEmpty()) {
            pending.m_record.m_protocolData.insert("voice_file", file);
        }
    }

    if (getMessageQueueToGUI()) {
        getMessageQueueToGUI()->push(MsgTxLog::create(pending.m_record, sent ? "sent" : "dropped"));
    }

    if (sent) {
        publishRecord(pending.m_record);
    }
}

// The voice sent, as it was modulated (the samples at the rate of the modulator) in a WAV file of the
// directory of the voice sent; the old files go a few at a time
QString RadioPagingMod::recordVoice(const PendingTx& pending, double& seconds)
{
    const std::vector<float>& samples = pending.m_voice ? pending.m_voice->m_samples : pending.m_live;
    seconds = (double) samples.size() / RadioPagingWav::m_sampleRate;

    if (samples.empty()) {
        return QString();
    }

    QString directory = m_settings.voiceDirectory();

    if (!QDir().mkpath(directory))
    {
        qWarning() << "RadioPagingMod::recordVoice: cannot create" << directory;
        return QString();
    }

    QStorageInfo storage(directory);

    if (storage.isValid() && (storage.bytesAvailable() < 500LL * 1024 * 1024))
    {
        qWarning() << "RadioPagingMod::recordVoice: not enough free space in" << directory;
        return QString();
    }

    WavFileRecord recorder((quint32) RadioPagingWav::m_sampleRate);
    recorder.setMono(true);
    recorder.setFileName(RadioPagingVoiceStore::filePrefix(directory,
        pending.m_record.m_channelAlias, "tx"));

    if (!recorder.startRecording())
    {
        qWarning() << "RadioPagingMod::recordVoice: cannot write in" << directory;
        return QString();
    }

    std::vector<qint16> block;
    block.reserve(samples.size());

    for (float sample : samples) {
        block.push_back((qint16) std::max(-32768.0f, std::min(32767.0f, sample * 32767.0f)));
    }

    recorder.writeMono(block.data(), (int) block.size());
    recorder.stopRecording();
    QString file = recorder.getCurrentFileName();
    RadioPagingVoiceStore::cleanup(directory, m_settings.m_gscVoiceKeepDays, m_settings.m_gscVoiceKeepMin,
        RadioPagingVoiceStore::m_autoCleanupBatch, file);
    return file;
}

void RadioPagingMod::publishPageEvent(const QString& pageId, const QString& state, const QString& reason)
{
    MessageRecord record;
    record.m_id = pageId;
    record.m_timestamp = QDateTime::currentMSecsSinceEpoch();
    record.m_kind = MessageRecord::KindEvent;
    record.m_direction = MessageRecord::DirectionTX;
    record.m_channelAlias = m_settings.effectiveChannelAlias(m_centerFrequency + m_settings.m_inputFrequencyOffset);
    record.m_protocolData.insert("event", "page");
    record.m_protocolData.insert("state", state);

    if (!reason.isEmpty()) {
        record.m_protocolData.insert("reason", reason);
    }

    publishRecord(record);
}

// The transmitter as the message center sees it. It is idle when it has nothing to send; the
// protocols are the one of the channel, and POCSAG too when a FLEX network mixes it
MessageRecord RadioPagingMod::endpointRecord(const QString& event)
{
    QMutexLocker mutexLocker(&m_settingsMutex);
    MessageEndpoint endpoint;
    int outstandingCount = outstanding();
    endpoint.m_frequency = m_centerFrequency + m_settings.m_inputFrequencyOffset;
    endpoint.m_alias = m_settings.effectiveChannelAlias(endpoint.m_frequency);
    endpoint.m_role = MessageEndpoint::Transmitter;
    endpoint.m_status = (event == "unregister") ? MessageEndpoint::Offline
        : (outstandingCount > 0) ? MessageEndpoint::Busy : MessageEndpoint::Idle;
    endpoint.m_protocols.append(RadioPagingModSettings::protocolName(m_settings.m_protocol).toLower());

    if ((m_settings.m_protocol == RadioPagingModSettings::Flex) && (m_settings.m_mode == RadioPagingModSettings::Network)
        && m_settings.m_flexMixPocsag) {
        endpoint.m_protocols.append("pocsag");
    }

    // A POCSAG channel: its messages choose the speed. A FLEX network that mixes POCSAG and GSC have
    // their own speeds in their records: no list
    if (m_settings.m_protocol == RadioPagingModSettings::Pocsag)
    {
        for (int i = 0; i < RadioPagingModSettings::m_nbPocsagSpeeds; i++) {
            endpoint.m_speeds.append(RadioPagingModSettings::m_pocsagSpeeds[i]);
        }
    }

    endpoint.m_polarities = {(int) MessageRecord::PolarityNormal, (int) MessageRecord::PolarityInverted};
    endpoint.m_queueDepth = outstandingCount;
    endpoint.m_capacity = m_maxQueued;
    endpoint.m_capabilities.insert("maxText", m_maxTextLength);
    endpoint.m_capabilities.insert("voice", m_settings.m_protocol == RadioPagingModSettings::Gsc);
    return MessageEndpoint::toRecord(endpoint, event);
}

void RadioPagingMod::announce(const QString& event)
{
    MessageRecord record = endpointRecord(event);
    QString alias = record.m_protocolData.value("endpoint").toString();

    // The alias changed (it is the frequency by default): the old one is gone
    if (!m_lastAlias.isEmpty() && (alias != m_lastAlias) && (event != "unregister"))
    {
        MessageRecord old = record;
        old.m_channelAlias = m_lastAlias;
        old.m_protocolData.insert("event", "unregister");
        old.m_protocolData.insert("endpoint", m_lastAlias);
        old.m_protocolData.insert("status", "offline");
        publishRecord(old);
    }

    m_lastAlias = alias;
    publishRecord(record);
}

void RadioPagingMod::publishRecord(const MessageRecord& record)
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, MessageRecord::m_pipeName, pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        messageQueue->push(MainCore::MsgMessageRecord::create(this, record));
    }
}

void RadioPagingMod::clear(bool abort)
{
    m_basebandSource->getInputMessageQueue()->push(RadioPagingModBaseband::MsgClear::create(abort));
}

bool RadioPagingMod::handleMessage(const Message& cmd)
{
    if (MsgConfigureRadioPagingMod::match(cmd))
    {
        MsgConfigureRadioPagingMod& cfg = (MsgConfigureRadioPagingMod&) cmd;
        qDebug() << "RadioPagingMod::handleMessage: MsgConfigureRadioPagingMod";

        applySettings(cfg.getSettingsKeys(), cfg.getSettings(), cfg.getForce());

        return true;
    }
    else if (MsgTx::match(cmd))
    {
        MsgTx& tx = (MsgTx&) cmd;
        QString error;
        QString warning;
        quint64 id = transmit(tx.getRequest(), error, &warning);

        // A WebAPI request was checked when posted; settings applied since may refuse it
        if (id == 0)
        {
            qWarning() << "RadioPagingMod::handleMessage: MsgTx refused:" << error;

            // A page of the message center is told, unless it is the same page posted twice
            if (!tx.getRequest().m_pageId.isEmpty() && !error.contains("already queued")) {
                publishPageEvent(tx.getRequest().m_pageId, "rejected", error);
            }
        }

        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(MsgReportTx::create(id, error, warning));
        }

        return true;
    }
    else if (MsgTxEvent::match(cmd))
    {
        const MsgTxEvent& event = (const MsgTxEvent&) cmd;
        txEvent(event.getId(), event.getEvent());

        return true;
    }
    else if (MsgLiveVoice::match(cmd))
    {
        const MsgLiveVoice& voice = (const MsgLiveVoice&) cmd;
        auto it = m_pending.find(voice.getId());

        if (it != m_pending.end())
        {
            it->second.m_live.insert(it->second.m_live.end(), voice.getSamples().begin(), voice.getSamples().end());
            m_basebandSource->pushLive(voice.getSamples());
        }

        return true;
    }
    else if (MsgLiveEnd::match(cmd))
    {
        m_basebandSource->endLive();

        return true;
    }
    else if (MsgClear::match(cmd))
    {
        MsgClear& clearCmd = (MsgClear&) cmd;
        clear(clearCmd.getAbort());

        return true;
    }
    else if (MainCore::MsgChannelDemodQuery::match(cmd))
    {
        qDebug() << "RadioPagingMod::handleMessage: MsgChannelDemodQuery";
        sendSampleRateToDemodAnalyzer();

        return true;
    }
    else if (DSPSignalNotification::match(cmd))
    {
        // Forward to the source
        DSPSignalNotification& notif = (DSPSignalNotification&) cmd;
        m_centerFrequency = notif.getCenterFrequency();
        announce("register");       // The frequency is known now (the alias is the frequency by default)
        DSPSignalNotification* rep = new DSPSignalNotification(notif); // make a copy
        qDebug() << "RadioPagingMod::handleMessage: DSPSignalNotification";
        m_basebandSource->getInputMessageQueue()->push(rep);

        // Forward to GUI if any
        if (getMessageQueueToGUI()) {
            getMessageQueueToGUI()->push(new DSPSignalNotification(notif));
        }

        return true;
    }
    else
    {
        return false;
    }
}

void RadioPagingMod::setCenterFrequency(qint64 frequency)
{
    QMutexLocker mutexLocker(&m_settingsMutex);
    RadioPagingModSettings settings = m_settings;
    settings.m_inputFrequencyOffset = frequency;
    applySettings(QStringList("inputFrequencyOffset"), settings, false);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureRadioPagingMod *msgToGUI = MsgConfigureRadioPagingMod::create(QStringList("inputFrequencyOffset"), settings, false);
        m_guiMessageQueue->push(msgToGUI);
    }
}

void RadioPagingMod::applySettings(const QStringList& settingsKeys, const RadioPagingModSettings& settings, bool force)
{
    QMutexLocker mutexLocker(&m_settingsMutex);
    qDebug() << "RadioPagingMod::applySettings:" << settings.getDebugString(settingsKeys, force);

    if (settingsKeys.contains("streamIndex") && (m_settings.m_streamIndex != settings.m_streamIndex))
    {
        if (m_deviceAPI->getSampleMIMO()) // change of stream is possible for MIMO devices only
        {
            m_deviceAPI->removeChannelSourceAPI(this);
            m_deviceAPI->removeChannelSource(this, false, m_settings.m_streamIndex);
            m_deviceAPI->addChannelSource(this, settings.m_streamIndex);
            m_deviceAPI->addChannelSourceAPI(this);
            m_settings.m_streamIndex = settings.m_streamIndex; // make sure ChannelAPI::getStreamIndex() is consistent
            emit streamIndexChanged(settings.m_streamIndex);
        }
    }

    RadioPagingModBaseband::MsgConfigureRadioPagingModBaseband *msg
        = RadioPagingModBaseband::MsgConfigureRadioPagingModBaseband::create(settingsKeys, settings, force);
    m_basebandSource->getInputMessageQueue()->push(msg);

    if (settingsKeys.contains("useReverseAPI") && settings.m_useReverseAPI)
    {
        bool fullUpdate = (settingsKeys.contains("useReverseAPI") && (m_settings.m_useReverseAPI != settings.m_useReverseAPI) && settings.m_useReverseAPI) ||
                (settingsKeys.contains("reverseAPIAddress") && (m_settings.m_reverseAPIAddress != settings.m_reverseAPIAddress)) ||
                (settingsKeys.contains("reverseAPIPort") && (m_settings.m_reverseAPIPort != settings.m_reverseAPIPort)) ||
                (settingsKeys.contains("reverseAPIDeviceIndex") && (m_settings.m_reverseAPIDeviceIndex != settings.m_reverseAPIDeviceIndex)) ||
                (settingsKeys.contains("reverseAPIChannelIndex") && (m_settings.m_reverseAPIChannelIndex != settings.m_reverseAPIChannelIndex));
        webapiReverseSendSettings(settingsKeys, settings, fullUpdate || force);
    }

    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "settings", pipes);

    if (pipes.size() > 0) {
        sendChannelSettings(pipes, settingsKeys, settings, force);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    // A new alias, frequency offset or protocol is announced at once
    if (m_announceTimer && !force
        && (settingsKeys.contains("channelAlias") || settingsKeys.contains("inputFrequencyOffset")
            || settingsKeys.contains("protocol") || settingsKeys.contains("mode") || settingsKeys.contains("mixPocsag"))) {
        announce("status");
    }
}

QByteArray RadioPagingMod::serialize() const
{
    return m_settings.serialize();
}

bool RadioPagingMod::deserialize(const QByteArray& data)
{
    QMutexLocker mutexLocker(&m_settingsMutex);
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureRadioPagingMod *msg = MsgConfigureRadioPagingMod::create(QStringList(), m_settings, true);
    m_inputMessageQueue.push(msg);

    return success;
}

int RadioPagingMod::webapiSettingsGet(
    SWGSDRangel::SWGChannelSettings& response,
    QString& errorMessage)
{
    (void) errorMessage;
    QMutexLocker mutexLocker(&m_settingsMutex);
    response.setRadioPagingModSettings(new SWGSDRangel::SWGRadioPagingModSettings());
    response.getRadioPagingModSettings()->init();
    webapiFormatChannelSettings(response, m_settings);

    return 200;
}

int RadioPagingMod::webapiWorkspaceGet(
    SWGSDRangel::SWGWorkspaceInfo& response,
    QString& errorMessage)
{
    (void) errorMessage;
    QMutexLocker mutexLocker(&m_settingsMutex);
    response.setIndex(m_settings.m_workspaceIndex);
    return 200;
}

int RadioPagingMod::webapiSettingsPutPatch(
    bool force,
    const QStringList& channelSettingsKeys,
    SWGSDRangel::SWGChannelSettings& response,
    QString& errorMessage)
{
    SWGSDRangel::SWGRadioPagingModSettings *swg = response.getRadioPagingModSettings();

    if (!swg)
    {
        errorMessage = "Missing RadioPagingModSettings";
        return 400;
    }
    if (channelSettingsKeys.contains("protocol")
     && ((swg->getProtocol() < (int) RadioPagingModSettings::Pocsag) || (swg->getProtocol() > (int) RadioPagingModSettings::Gsc)))
    {
        errorMessage = "protocol must be 0 (POCSAG), 1 (FLEX) or 2 (GSC)";
        return 400;
    }
    if (channelSettingsKeys.contains("mode")
     && ((swg->getMode() < (int) RadioPagingModSettings::OneShot) || (swg->getMode() > (int) RadioPagingModSettings::Network)))
    {
        errorMessage = "mode must be 0 (one-shot) or 1 (network)";
        return 400;
    }
    if (channelSettingsKeys.contains("pocsagSpeed") && !RadioPagingModSettings::isPocsagSpeed(swg->getPocsagSpeed()))
    {
        errorMessage = "pocsagSpeed must be 512, 1200 or 2400";
        return 400;
    }
    if (channelSettingsKeys.contains("flexMode")
     && ((swg->getFlexMode() < (int) RadioPagingFlex::Mode1600_2) || (swg->getFlexMode() > (int) RadioPagingFlex::Mode6400_4)))
    {
        errorMessage = "flexMode must be 0 (1600/2), 1 (3200/2), 2 (3200/4) or 3 (6400/4)";
        return 400;
    }
    if (channelSettingsKeys.contains("gscMode")
     && ((swg->getGscMode() < (int) RadioPagingGsc::Auto) || (swg->getGscMode() > (int) RadioPagingGsc::NonBatterySaver)))
    {
        errorMessage = "gscMode must be 0 (auto), 1 (single), 2 (batch) or 3 (non battery saver)";
        return 400;
    }
    if (channelSettingsKeys.contains("gscVoiceDirectory") && swg->getGscVoiceDirectory() && !swg->getGscVoiceDirectory()->isEmpty()
     && (!QDir::isAbsolutePath(*swg->getGscVoiceDirectory()) || swg->getGscVoiceDirectory()->split('/').contains("..")))
    {
        errorMessage = "gscVoiceDirectory must be an absolute path without ..";
        return 400;
    }
    if (channelSettingsKeys.contains("gscVoiceSeconds")
     && ((swg->getGscVoiceSeconds() < 1) || (swg->getGscVoiceSeconds() > 50)))
    {
        errorMessage = "gscVoiceSeconds must be 1 to 50";
        return 400;
    }
    if (channelSettingsKeys.contains("flexCollapse")
     && ((swg->getFlexCollapse() < 0) || (swg->getFlexCollapse() > RadioPagingFlex::m_maxCollapse)))
    {
        errorMessage = "flexCollapse must be 0 to 7";
        return 400;
    }
    if (channelSettingsKeys.contains("ssidLocalId") && ((swg->getSsidLocalId() < 0) || (swg->getSsidLocalId() > 511)))
    {
        errorMessage = "ssidLocalId must be 0 to 511";
        return 400;
    }
    if (channelSettingsKeys.contains("ssidCoverageZone") && ((swg->getSsidCoverageZone() < 0) || (swg->getSsidCoverageZone() > 31)))
    {
        errorMessage = "ssidCoverageZone must be 0 to 31";
        return 400;
    }
    if (channelSettingsKeys.contains("ssidCountryCode") && ((swg->getSsidCountryCode() < 0) || (swg->getSsidCountryCode() > 1023)))
    {
        errorMessage = "ssidCountryCode must be 0 to 1023";
        return 400;
    }
    if (channelSettingsKeys.contains("ssidTrafficFlags") && ((swg->getSsidTrafficFlags() < 0) || (swg->getSsidTrafficFlags() > 15)))
    {
        errorMessage = "ssidTrafficFlags must be 0 to 15";
        return 400;
    }

    QMutexLocker mutexLocker(&m_settingsMutex);
    RadioPagingModSettings settings = m_settings;
    mutexLocker.unlock();

    // The frames per minute are limited by the minute: a change of the minute brings the clamped value
    QStringList keys = channelSettingsKeys;

    if (keys.contains("mixShared") && !keys.contains("mixFramesPerMinute")) {
        keys.append("mixFramesPerMinute");
    }

    webapiUpdateChannelSettings(settings, channelSettingsKeys, response);

    MsgConfigureRadioPagingMod *msg = MsgConfigureRadioPagingMod::create(keys, settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureRadioPagingMod *msgToGUI = MsgConfigureRadioPagingMod::create(keys, settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatChannelSettings(response, settings);

    return 200;
}

void RadioPagingMod::webapiUpdateChannelSettings(
    RadioPagingModSettings& settings,
    const QStringList& channelSettingsKeys,
    SWGSDRangel::SWGChannelSettings& response)
{
    SWGSDRangel::SWGRadioPagingModSettings *swg = response.getRadioPagingModSettings();

    if (channelSettingsKeys.contains("inputFrequencyOffset")) {
        settings.m_inputFrequencyOffset = swg->getInputFrequencyOffset();
    }
    if (channelSettingsKeys.contains("protocol"))
    {
        int protocol = swg->getProtocol();
        if ((protocol >= (int) RadioPagingModSettings::Pocsag) && (protocol <= (int) RadioPagingModSettings::Gsc)) {
            settings.m_protocol = (RadioPagingModSettings::Protocol) protocol;
        }
    }
    if (channelSettingsKeys.contains("mode"))
    {
        int mode = swg->getMode();
        if ((mode >= (int) RadioPagingModSettings::OneShot) && (mode <= (int) RadioPagingModSettings::Network)) {
            settings.m_mode = (RadioPagingModSettings::Mode) mode;
        }
    }
    if (channelSettingsKeys.contains("deviation")) {
        settings.m_deviation = swg->getDeviation();
    }
    if (channelSettingsKeys.contains("gain")) {
        settings.m_gain = swg->getGain();
    }
    if (channelSettingsKeys.contains("channelMute")) {
        settings.m_channelMute = swg->getChannelMute() != 0;
    }
    if (channelSettingsKeys.contains("rampUpMs")) {
        settings.m_rampUpMs = swg->getRampUpMs();
    }
    if (channelSettingsKeys.contains("rampDownMs")) {
        settings.m_rampDownMs = swg->getRampDownMs();
    }
    if (channelSettingsKeys.contains("rampRange")) {
        settings.m_rampRange = swg->getRampRange();
    }
    if (channelSettingsKeys.contains("pocsagSpeed") && RadioPagingModSettings::isPocsagSpeed(swg->getPocsagSpeed())) {
        settings.m_pocsagSpeed = swg->getPocsagSpeed();
    }
    if (channelSettingsKeys.contains("pocsagInverted")) {
        settings.m_pocsagInverted = swg->getPocsagInverted() != 0;
    }
    if (channelSettingsKeys.contains("flexMode")) {
        settings.m_flexMode = swg->getFlexMode();
    }
    if (channelSettingsKeys.contains("flexInverted")) {
        settings.m_flexInverted = swg->getFlexInverted() != 0;
    }
    if (channelSettingsKeys.contains("flexCollapse")) {
        settings.m_flexCollapse = swg->getFlexCollapse();
    }
    if (channelSettingsKeys.contains("flexRoaming")) {
        settings.m_flexRoaming = swg->getFlexRoaming() != 0;
    }
    if (channelSettingsKeys.contains("ssidLocalId")) {
        settings.m_ssidLocalId = swg->getSsidLocalId();
    }
    if (channelSettingsKeys.contains("ssidCoverageZone")) {
        settings.m_ssidCoverageZone = swg->getSsidCoverageZone();
    }
    if (channelSettingsKeys.contains("ssidCountryCode")) {
        settings.m_ssidCountryCode = swg->getSsidCountryCode();
    }
    if (channelSettingsKeys.contains("ssidTrafficFlags")) {
        settings.m_ssidTrafficFlags = swg->getSsidTrafficFlags();
    }
    if (channelSettingsKeys.contains("gscMode")) {
        settings.m_gscMode = swg->getGscMode();
    }
    if (channelSettingsKeys.contains("gscInverted")) {
        settings.m_gscInverted = swg->getGscInverted() != 0;
    }
    if (channelSettingsKeys.contains("gscVoiceSeconds")) {
        settings.m_gscVoiceSeconds = swg->getGscVoiceSeconds();
    }
    if (channelSettingsKeys.contains("channelAlias") && swg->getChannelAlias()) {
        settings.m_channelAlias = *swg->getChannelAlias();
    }
    if (channelSettingsKeys.contains("gscVoiceDirectory") && swg->getGscVoiceDirectory()) {
        settings.m_gscVoiceDirectory = *swg->getGscVoiceDirectory();
    }
    if (channelSettingsKeys.contains("gscVoiceKeepDays")) {
        settings.m_gscVoiceKeepDays = std::max(0, std::min(3650, swg->getGscVoiceKeepDays()));
    }
    if (channelSettingsKeys.contains("gscVoiceKeepMin")) {
        settings.m_gscVoiceKeepMin = std::max(0, std::min(100000, swg->getGscVoiceKeepMin()));
    }
    if (channelSettingsKeys.contains("mixPocsag")) {
        settings.m_flexMixPocsag = swg->getMixPocsag() != 0;
    }
    if (channelSettingsKeys.contains("pocsagDeviation")) {
        settings.m_pocsagDeviation = swg->getPocsagDeviation();
    }
    if (channelSettingsKeys.contains("mixShared")) {
        settings.m_mixShared = swg->getMixShared() != 0;
    }
    if (channelSettingsKeys.contains("mixFramesPerMinute")) {
        settings.m_mixFramesPerMinute = swg->getMixFramesPerMinute();
    }
    if (channelSettingsKeys.contains("mixSwitchMs512")) {
        settings.m_mixSwitchMs[0] = swg->getMixSwitchMs512();
    }
    if (channelSettingsKeys.contains("mixSwitchMs1200")) {
        settings.m_mixSwitchMs[1] = swg->getMixSwitchMs1200();
    }
    if (channelSettingsKeys.contains("mixSwitchMs2400")) {
        settings.m_mixSwitchMs[2] = swg->getMixSwitchMs2400();
    }
    if (channelSettingsKeys.contains("rgbColor")) {
        settings.m_rgbColor = swg->getRgbColor();
    }
    if (channelSettingsKeys.contains("title")) {
        settings.m_title = *swg->getTitle();
    }
    if (channelSettingsKeys.contains("streamIndex")) {
        settings.m_streamIndex = swg->getStreamIndex();
    }
    if (channelSettingsKeys.contains("useReverseAPI")) {
        settings.m_useReverseAPI = swg->getUseReverseApi() != 0;
    }
    if (channelSettingsKeys.contains("reverseAPIAddress")) {
        settings.m_reverseAPIAddress = *swg->getReverseApiAddress();
    }
    if (channelSettingsKeys.contains("reverseAPIPort")) {
        settings.m_reverseAPIPort = swg->getReverseApiPort();
    }
    if (channelSettingsKeys.contains("reverseAPIDeviceIndex")) {
        settings.m_reverseAPIDeviceIndex = swg->getReverseApiDeviceIndex();
    }
    if (channelSettingsKeys.contains("reverseAPIChannelIndex")) {
        settings.m_reverseAPIChannelIndex = swg->getReverseApiChannelIndex();
    }
    if (settings.m_channelMarker && channelSettingsKeys.contains("channelMarker")) {
        settings.m_channelMarker->updateFrom(channelSettingsKeys, swg->getChannelMarker());
    }
    if (settings.m_rollupState && channelSettingsKeys.contains("rollupState")) {
        settings.m_rollupState->updateFrom(channelSettingsKeys, swg->getRollupState());
    }

    settings.validate();
}

int RadioPagingMod::webapiReportGet(
    SWGSDRangel::SWGChannelReport& response,
    QString& errorMessage)
{
    (void) errorMessage;
    response.setRadioPagingModReport(new SWGSDRangel::SWGRadioPagingModReport());
    response.getRadioPagingModReport()->init();
    webapiFormatChannelReport(response);
    return 200;
}

// tx: 202 when queued, 400 when the message is not valid for the channel or the queue is full
int RadioPagingMod::webapiActionsPost(
    const QStringList& channelActionsKeys,
    SWGSDRangel::SWGChannelActions& query,
    QString& errorMessage)
{
    SWGSDRangel::SWGRadioPagingModActions *swgActions = query.getRadioPagingModActions();

    if (!swgActions)
    {
        errorMessage = "Missing RadioPagingModActions in query";
        return 400;
    }

    int actions = (channelActionsKeys.contains("tx") ? 1 : 0) + (channelActionsKeys.contains("clear") ? 1 : 0)
        + (channelActionsKeys.contains("abort") ? 1 : 0);

    if (actions != 1)
    {
        errorMessage = "One action expected: tx, clear or abort";
        return 400;
    }

    // Through the channel's message queue, as the GUI: the actions and the settings
    // changes are applied in the order they were posted
    if (channelActionsKeys.contains("abort"))
    {
        if (swgActions->getAbort() != 0) {
            m_inputMessageQueue.push(MsgClear::create(true));
        }
        return 202;
    }

    if (channelActionsKeys.contains("clear"))
    {
        if (swgActions->getClear() != 0) {
            m_inputMessageQueue.push(MsgClear::create(false));
        }
        return 202;
    }

    if (!swgActions->getTx())
    {
        errorMessage = "tx needs a message: POCSAG address, function, type, text; FLEX capcode, type, text";
        return 400;
    }

    {
        SWGSDRangel::SWGRadioPagingModTx *swgTx = swgActions->getTx();
        TxRequest request;
        QString type = swgTx->getType() ? swgTx->getType()->toLower() : QString();
        QString polarity = swgTx->getPolarity() ? swgTx->getPolarity()->toLower() : QString();
        std::string text = swgTx->getText() ? swgTx->getText()->toStdString() : std::string();
        request.m_pageId = swgTx->getId() ? *swgTx->getId() : QString();
        QString messageProtocol = swgTx->getProtocol() ? swgTx->getProtocol()->toLower() : QString();
        QMutexLocker mutexLocker(&m_settingsMutex);
        RadioPagingModSettings::Protocol protocol = m_settings.m_protocol;
        mutexLocker.unlock();

        // A FLEX channel with the mixing option also takes POCSAG messages: checkRequest decides
        if (messageProtocol == "pocsag") {
            protocol = RadioPagingModSettings::Pocsag;
        } else if (messageProtocol == "flex") {
            protocol = RadioPagingModSettings::Flex;
        } else if (messageProtocol == "gsc") {
            protocol = RadioPagingModSettings::Gsc;
        } else if (!messageProtocol.isEmpty())
        {
            errorMessage = "Message protocol must be pocsag, flex or gsc";
            return 400;
        }

        if (!type.isEmpty() && (type != "tone") && (type != "numeric") && (type != "alphanumeric")
         && !((type == "voice") && (protocol == RadioPagingModSettings::Gsc)))
        {
            errorMessage = "Message type must be tone, numeric or alphanumeric (GSC: or voice)";
            return 400;
        }

        if (polarity.isEmpty()) {
            request.m_polarity = -1;
        } else if (polarity == "normal") {
            request.m_polarity = 0;
        } else if (polarity == "inverted") {
            request.m_polarity = 1;
        }
        else
        {
            errorMessage = "Polarity must be normal or inverted";
            return 400;
        }

        request.m_protocol = (int) protocol;

        if (protocol == RadioPagingModSettings::Pocsag)
        {
            if ((swgTx->getAddress() < 0) || ((uint32_t) swgTx->getAddress() > RadioPagingPocsag::m_maxAddress))
            {
                errorMessage = "POCSAG address must be 0 to 2097151";
                return 400;
            }
            if ((swgTx->getFunction() < 0) || (swgTx->getFunction() > 3))
            {
                errorMessage = "POCSAG function must be 0 to 3";
                return 400;
            }

            request.m_pocsag.m_address = (uint32_t) swgTx->getAddress();
            request.m_pocsag.m_function = swgTx->getFunction();
            request.m_pocsag.m_type = (type == "tone") ? RadioPagingPocsag::Tone
                : (type == "numeric") ? RadioPagingPocsag::Numeric : RadioPagingPocsag::Alphanumeric;
            request.m_pocsag.m_text = text;
            request.m_speed = swgTx->getSpeed();
        }
        else if (protocol == RadioPagingModSettings::Flex)
        {
            if (swgTx->getCapcode() <= 0)
            {
                errorMessage = "FLEX capcode must be 1 to 1933312 or 2101249 to 4297068542";
                return 400;
            }

            request.m_flex.m_capcode = (uint64_t) swgTx->getCapcode();
            request.m_flex.m_type = (type == "tone") ? RadioPagingFlex::Tone
                : (type == "numeric") ? RadioPagingFlex::Numeric : RadioPagingFlex::Alphanumeric;
            request.m_flex.m_text = text;
            request.m_flex.m_priority = swgTx->getPriority() != 0;
        }
        else if (protocol == RadioPagingModSettings::Gsc)
        {
            // The address as an integer of up to 6 digits (zero padded: index, group, 3 address
            // digits), the function 0 to 3 as the address number 1 to 4
            if ((swgTx->getCapcode() < 0) || (swgTx->getCapcode() > 999999))
            {
                errorMessage = "GSC address (capcode) must be 0 to 999999";
                return 400;
            }
            if ((swgTx->getFunction() < 0) || (swgTx->getFunction() > 3))
            {
                errorMessage = "GSC address number (function) must be 0 to 3, for 1 to 4";
                return 400;
            }

            request.m_gsc.m_address = QString("%1").arg((qlonglong) swgTx->getCapcode(), 6, 10, QChar('0')).toStdString();
            request.m_gsc.m_addressNumber = swgTx->getFunction() + 1;
            request.m_gsc.m_type = (type == "tone") ? RadioPagingGsc::Tone
                : (type == "voice") ? RadioPagingGsc::Voice
                : (type == "numeric") ? RadioPagingGsc::Numeric : RadioPagingGsc::Alphanumeric;
            request.m_gsc.m_text = ((type == "tone") || (type == "voice")) ? std::string() : text;
            request.m_voiceFile = (type == "voice") ? QString::fromStdString(text) : QString();
        }

        // Checked here for the reply; it is checked again when queued, after the settings
        // posted before it
        RadioPagingModSource::Request sourceRequest;
        QString warning;
        QString error = checkRequest(request, sourceRequest, &warning);

        if (!error.isEmpty())
        {
            errorMessage = error;
            return 400;
        }

        request.m_voice = sourceRequest.m_voice;        // The sound file is read once
        m_inputMessageQueue.push(MsgTx::create(request));

        return 202;
    }
}

void RadioPagingMod::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const RadioPagingModSettings& settings)
{
    SWGSDRangel::SWGRadioPagingModSettings *swg = response.getRadioPagingModSettings();

    swg->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    swg->setProtocol((int) settings.m_protocol);
    swg->setMode((int) settings.m_mode);
    swg->setDeviation(settings.m_deviation);
    swg->setGain(settings.m_gain);
    swg->setChannelMute(settings.m_channelMute ? 1 : 0);
    swg->setRampUpMs(settings.m_rampUpMs);
    swg->setRampDownMs(settings.m_rampDownMs);
    swg->setRampRange(settings.m_rampRange);
    swg->setPocsagSpeed(settings.m_pocsagSpeed);
    swg->setPocsagInverted(settings.m_pocsagInverted ? 1 : 0);
    swg->setFlexMode(settings.m_flexMode);
    swg->setFlexInverted(settings.m_flexInverted ? 1 : 0);
    swg->setFlexCollapse(settings.m_flexCollapse);
    swg->setFlexRoaming(settings.m_flexRoaming ? 1 : 0);
    swg->setSsidLocalId(settings.m_ssidLocalId);
    swg->setSsidCoverageZone(settings.m_ssidCoverageZone);
    swg->setSsidCountryCode(settings.m_ssidCountryCode);
    swg->setSsidTrafficFlags(settings.m_ssidTrafficFlags);
    swg->setGscMode(settings.m_gscMode);
    swg->setGscInverted(settings.m_gscInverted ? 1 : 0);
    swg->setGscVoiceSeconds(settings.m_gscVoiceSeconds);
    swg->setGscVoiceKeepDays(settings.m_gscVoiceKeepDays);
    swg->setGscVoiceKeepMin(settings.m_gscVoiceKeepMin);
    swg->setChannelAlias(new QString(settings.m_channelAlias));
    swg->setGscVoiceDirectory(new QString(settings.m_gscVoiceDirectory));
    swg->setMixPocsag(settings.m_flexMixPocsag ? 1 : 0);
    swg->setPocsagDeviation(settings.m_pocsagDeviation);
    swg->setMixFramesPerMinute(settings.m_mixFramesPerMinute);
    swg->setMixShared(settings.m_mixShared ? 1 : 0);
    swg->setMixSwitchMs512(settings.m_mixSwitchMs[0]);
    swg->setMixSwitchMs1200(settings.m_mixSwitchMs[1]);
    swg->setMixSwitchMs2400(settings.m_mixSwitchMs[2]);
    swg->setRgbColor(settings.m_rgbColor);

    if (swg->getTitle()) {
        *swg->getTitle() = settings.m_title;
    } else {
        swg->setTitle(new QString(settings.m_title));
    }

    swg->setStreamIndex(settings.m_streamIndex);
    swg->setUseReverseApi(settings.m_useReverseAPI ? 1 : 0);

    if (swg->getReverseApiAddress()) {
        *swg->getReverseApiAddress() = settings.m_reverseAPIAddress;
    } else {
        swg->setReverseApiAddress(new QString(settings.m_reverseAPIAddress));
    }

    swg->setReverseApiPort(settings.m_reverseAPIPort);
    swg->setReverseApiDeviceIndex(settings.m_reverseAPIDeviceIndex);
    swg->setReverseApiChannelIndex(settings.m_reverseAPIChannelIndex);

    if (settings.m_channelMarker)
    {
        if (swg->getChannelMarker())
        {
            settings.m_channelMarker->formatTo(swg->getChannelMarker());
        }
        else
        {
            SWGSDRangel::SWGChannelMarker *swgChannelMarker = new SWGSDRangel::SWGChannelMarker();
            settings.m_channelMarker->formatTo(swgChannelMarker);
            swg->setChannelMarker(swgChannelMarker);
        }
    }

    if (settings.m_rollupState)
    {
        if (swg->getRollupState())
        {
            settings.m_rollupState->formatTo(swg->getRollupState());
        }
        else
        {
            SWGSDRangel::SWGRollupState *swgRollupState = new SWGSDRangel::SWGRollupState();
            settings.m_rollupState->formatTo(swgRollupState);
            swg->setRollupState(swgRollupState);
        }
    }
}

void RadioPagingMod::webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response)
{
    SWGSDRangel::SWGRadioPagingModReport *swg = response.getRadioPagingModReport();
    RadioPagingModSource::Status status = getStatus();

    swg->setChannelPowerDb(CalcDb::dbPower(getMagSq()));
    swg->setChannelSampleRate(m_basebandSource->getChannelSampleRate());
    swg->setState((int) status.m_state);
    swg->setQueued(status.m_queued);
    swg->setInFlight(status.m_inFlight);
    swg->setCurrentId((qint64) status.m_currentId);
    swg->setLastSentId((qint64) status.m_lastSentId);
    swg->setSent((qint64) status.m_sent);
    swg->setDropped((qint64) status.m_dropped);
    swg->setErs(status.m_ers ? 1 : 0);
    swg->setErsMs(status.m_ersMs);
    swg->setErsCount((qint64) status.m_ersCount);
    swg->setSampleCounter((qint64) status.m_sampleCounter);
}

void RadioPagingMod::webapiReverseSendSettings(const QList<QString>& channelSettingsKeys, const RadioPagingModSettings& settings, bool force)
{
    SWGSDRangel::SWGChannelSettings *swgChannelSettings = new SWGSDRangel::SWGChannelSettings();
    webapiFormatChannelSettings(channelSettingsKeys, swgChannelSettings, settings, force);

    QString channelSettingsURL = QString("http://%1:%2/sdrangel/deviceset/%3/channel/%4/settings")
            .arg(settings.m_reverseAPIAddress)
            .arg(settings.m_reverseAPIPort)
            .arg(settings.m_reverseAPIDeviceIndex)
            .arg(settings.m_reverseAPIChannelIndex);
    m_networkRequest.setUrl(QUrl(channelSettingsURL));
    m_networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QBuffer *buffer = new QBuffer();
    buffer->open((QBuffer::ReadWrite));
    buffer->write(swgChannelSettings->asJson().toUtf8());
    buffer->seek(0);

    // Always use PATCH to avoid passing reverse API settings
    QNetworkReply *reply = m_networkManager->sendCustomRequest(m_networkRequest, "PATCH", buffer);
    buffer->setParent(reply);

    delete swgChannelSettings;
}

void RadioPagingMod::sendChannelSettings(
    const QList<ObjectPipe*>& pipes,
    const QList<QString>& channelSettingsKeys,
    const RadioPagingModSettings& settings,
    bool force)
{
    for (const auto& pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);

        if (messageQueue)
        {
            SWGSDRangel::SWGChannelSettings *swgChannelSettings = new SWGSDRangel::SWGChannelSettings();
            webapiFormatChannelSettings(channelSettingsKeys, swgChannelSettings, settings, force);
            MainCore::MsgChannelSettings *msg = MainCore::MsgChannelSettings::create(
                this,
                channelSettingsKeys,
                swgChannelSettings,
                force
            );
            messageQueue->push(msg);
        }
    }
}

// Transfer the settings that have been modified. When force is on, all settings except
// the reverse API settings
void RadioPagingMod::webapiFormatChannelSettings(
    const QList<QString>& channelSettingsKeys,
    SWGSDRangel::SWGChannelSettings *swgChannelSettings,
    const RadioPagingModSettings& settings,
    bool force
)
{
    swgChannelSettings->setDirection(1); // single source (Tx)
    swgChannelSettings->setOriginatorChannelIndex(getIndexInDeviceSet());
    swgChannelSettings->setOriginatorDeviceSetIndex(getDeviceSetIndex());
    swgChannelSettings->setChannelType(new QString(m_channelId));
    swgChannelSettings->setRadioPagingModSettings(new SWGSDRangel::SWGRadioPagingModSettings());
    SWGSDRangel::SWGRadioPagingModSettings *swg = swgChannelSettings->getRadioPagingModSettings();

    if (channelSettingsKeys.contains("inputFrequencyOffset") || force) {
        swg->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    }
    if (channelSettingsKeys.contains("protocol") || force) {
        swg->setProtocol((int) settings.m_protocol);
    }
    if (channelSettingsKeys.contains("mode") || force) {
        swg->setMode((int) settings.m_mode);
    }
    if (channelSettingsKeys.contains("deviation") || force) {
        swg->setDeviation(settings.m_deviation);
    }
    if (channelSettingsKeys.contains("gain") || force) {
        swg->setGain(settings.m_gain);
    }
    if (channelSettingsKeys.contains("channelMute") || force) {
        swg->setChannelMute(settings.m_channelMute ? 1 : 0);
    }
    if (channelSettingsKeys.contains("rampUpMs") || force) {
        swg->setRampUpMs(settings.m_rampUpMs);
    }
    if (channelSettingsKeys.contains("rampDownMs") || force) {
        swg->setRampDownMs(settings.m_rampDownMs);
    }
    if (channelSettingsKeys.contains("rampRange") || force) {
        swg->setRampRange(settings.m_rampRange);
    }
    if (channelSettingsKeys.contains("pocsagSpeed") || force) {
        swg->setPocsagSpeed(settings.m_pocsagSpeed);
    }
    if (channelSettingsKeys.contains("pocsagInverted") || force) {
        swg->setPocsagInverted(settings.m_pocsagInverted ? 1 : 0);
    }
    if (channelSettingsKeys.contains("flexMode") || force) {
        swg->setFlexMode(settings.m_flexMode);
    }
    if (channelSettingsKeys.contains("flexInverted") || force) {
        swg->setFlexInverted(settings.m_flexInverted ? 1 : 0);
    }
    if (channelSettingsKeys.contains("flexCollapse") || force) {
        swg->setFlexCollapse(settings.m_flexCollapse);
    }
    if (channelSettingsKeys.contains("flexRoaming") || force) {
        swg->setFlexRoaming(settings.m_flexRoaming ? 1 : 0);
    }
    if (channelSettingsKeys.contains("ssidLocalId") || force) {
        swg->setSsidLocalId(settings.m_ssidLocalId);
    }
    if (channelSettingsKeys.contains("ssidCoverageZone") || force) {
        swg->setSsidCoverageZone(settings.m_ssidCoverageZone);
    }
    if (channelSettingsKeys.contains("ssidCountryCode") || force) {
        swg->setSsidCountryCode(settings.m_ssidCountryCode);
    }
    if (channelSettingsKeys.contains("ssidTrafficFlags") || force) {
        swg->setSsidTrafficFlags(settings.m_ssidTrafficFlags);
    }
    if (channelSettingsKeys.contains("gscMode") || force) {
        swg->setGscMode(settings.m_gscMode);
    }
    if (channelSettingsKeys.contains("gscInverted") || force) {
        swg->setGscInverted(settings.m_gscInverted ? 1 : 0);
    }
    if (channelSettingsKeys.contains("gscVoiceSeconds") || force) {
        swg->setGscVoiceSeconds(settings.m_gscVoiceSeconds);
    }
    if (channelSettingsKeys.contains("channelAlias") || force) {
        swg->setChannelAlias(new QString(settings.m_channelAlias));
    }
    if (channelSettingsKeys.contains("gscVoiceDirectory") || force) {
        swg->setGscVoiceDirectory(new QString(settings.m_gscVoiceDirectory));
    }
    if (channelSettingsKeys.contains("gscVoiceKeepDays") || force) {
        swg->setGscVoiceKeepDays(settings.m_gscVoiceKeepDays);
    }
    if (channelSettingsKeys.contains("gscVoiceKeepMin") || force) {
        swg->setGscVoiceKeepMin(settings.m_gscVoiceKeepMin);
    }
    if (channelSettingsKeys.contains("mixPocsag") || force) {
        swg->setMixPocsag(settings.m_flexMixPocsag ? 1 : 0);
    }
    if (channelSettingsKeys.contains("pocsagDeviation") || force) {
        swg->setPocsagDeviation(settings.m_pocsagDeviation);
    }
    if (channelSettingsKeys.contains("mixFramesPerMinute") || force) {
        swg->setMixFramesPerMinute(settings.m_mixFramesPerMinute);
    }
    if (channelSettingsKeys.contains("mixShared") || force) {
        swg->setMixShared(settings.m_mixShared ? 1 : 0);
    }
    if (channelSettingsKeys.contains("mixSwitchMs512") || force) {
        swg->setMixSwitchMs512(settings.m_mixSwitchMs[0]);
    }
    if (channelSettingsKeys.contains("mixSwitchMs1200") || force) {
        swg->setMixSwitchMs1200(settings.m_mixSwitchMs[1]);
    }
    if (channelSettingsKeys.contains("mixSwitchMs2400") || force) {
        swg->setMixSwitchMs2400(settings.m_mixSwitchMs[2]);
    }
    if (channelSettingsKeys.contains("rgbColor") || force) {
        swg->setRgbColor(settings.m_rgbColor);
    }
    if (channelSettingsKeys.contains("title") || force) {
        swg->setTitle(new QString(settings.m_title));
    }
    if (channelSettingsKeys.contains("streamIndex") || force) {
        swg->setStreamIndex(settings.m_streamIndex);
    }

    if (settings.m_channelMarker && (channelSettingsKeys.contains("channelMarker") || force))
    {
        SWGSDRangel::SWGChannelMarker *swgChannelMarker = new SWGSDRangel::SWGChannelMarker();
        settings.m_channelMarker->formatTo(swgChannelMarker);
        swg->setChannelMarker(swgChannelMarker);
    }

    if (settings.m_rollupState && (channelSettingsKeys.contains("rollupState") || force))
    {
        SWGSDRangel::SWGRollupState *swgRollupState = new SWGSDRangel::SWGRollupState();
        settings.m_rollupState->formatTo(swgRollupState);
        swg->setRollupState(swgRollupState);
    }
}

void RadioPagingMod::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "RadioPagingMod::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("RadioPagingMod::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}

double RadioPagingMod::getMagSq() const
{
    return m_basebandSource->getMagSq();
}

void RadioPagingMod::setLevelMeter(QObject *levelMeter)
{
    connect(m_basebandSource, SIGNAL(levelChanged(qreal, qreal, int)), levelMeter, SLOT(levelChanged(qreal, qreal, int)));
}

uint32_t RadioPagingMod::getNumberOfDeviceStreams() const
{
    return m_deviceAPI->getNbSinkStreams();
}

RadioPagingModSource::Status RadioPagingMod::getStatus() const
{
    RadioPagingModSource::Status status = m_basebandSource->getStatus();
    quint64 done = status.m_sent + status.m_dropped + status.m_inFlight;
    quint64 accepted = m_accepted;

    status.m_queued = (accepted > done) ? (int) (accepted - done) : 0;
    return status;
}

int RadioPagingMod::getSourceChannelSampleRate() const
{
    return m_basebandSource->getSourceChannelSampleRate();
}

int RadioPagingMod::getAudioSampleRate() const
{
    return getSourceChannelSampleRate();
}

void RadioPagingMod::sendSampleRateToDemodAnalyzer()
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "reportdemod", pipes);

    for (const auto& pipe : pipes)
    {
        MessageQueue* messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);

        if (messageQueue) {
            messageQueue->push(MainCore::MsgChannelDemodReport::create(this, getSourceChannelSampleRate()));
        }
    }
}

int RadioPagingMod::getChannelSampleRate() const
{
    return m_basebandSource ? m_basebandSource->getChannelSampleRate() : 0;
}
