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

#include <algorithm>

#include <QDebug>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QBuffer>
#include <QTimer>

#include "SWGChannelSettings.h"
#include "SWGWorkspaceInfo.h"
#include "SWGRadioPagingDemodSettings.h"
#include "SWGChannelReport.h"
#include "SWGRadioPagingDemodReport.h"

#include "dsp/dspcommands.h"
#include "device/deviceapi.h"
#include "settings/serializable.h"
#include "util/messageendpoint.h"
#include "util/db.h"
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

    m_networkManager = new QNetworkAccessManager();
    QObject::connect(m_networkManager, &QNetworkAccessManager::finished, this, &RadioPagingDemod::networkManagerFinished);
    QObject::connect(this, &ChannelAPI::indexInDeviceSetChanged, this, &RadioPagingDemod::handleIndexInDeviceSetChanged);

    // The message center learns this receiver at the next announcement, whenever it starts
    m_announceTimer = new QTimer(this);
    QObject::connect(m_announceTimer, &QTimer::timeout, this, [this]() { announce("status"); });
    m_announceTimer->start(5000);
}

// The receiver as the message center sees it: every protocol, speed and polarity is decoded
void RadioPagingDemod::announce(const QString& event)
{
    MessageEndpoint endpoint;
    endpoint.m_frequency = m_centerFrequency + m_settings.m_inputFrequencyOffset;
    endpoint.m_alias = m_settings.effectiveChannelAlias(endpoint.m_frequency);
    endpoint.m_role = MessageEndpoint::Receiver;
    endpoint.m_status = (event == "unregister") ? MessageEndpoint::Offline : MessageEndpoint::Idle;
    endpoint.m_protocols = QStringList({"pocsag", "flex", "gsc"});
    endpoint.m_capabilities.insert("voice", true);
    endpoint.m_capabilities.insert("voiceRecording", m_settings.m_gscVoiceRecord);
    // The alias changed (it is the frequency by default): the old one is gone
    if (!m_lastAlias.isEmpty() && (endpoint.m_alias != m_lastAlias) && (event != "unregister"))
    {
        MessageEndpoint old = endpoint;
        old.m_alias = m_lastAlias;
        old.m_status = MessageEndpoint::Offline;
        publishRecord(MessageEndpoint::toRecord(old, "unregister"));
    }

    m_lastAlias = endpoint.m_alias;
    publishRecord(MessageEndpoint::toRecord(endpoint, event));
}

RadioPagingDemod::~RadioPagingDemod()
{
    qDebug("RadioPagingDemod::~RadioPagingDemod");
    m_announceTimer->stop();
    announce("unregister");
    QObject::disconnect(m_networkManager, &QNetworkAccessManager::finished, this, &RadioPagingDemod::networkManagerFinished);
    delete m_networkManager;
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

// The voice audio is labelled with the channel in the audio output dialog
void RadioPagingDemod::handleIndexInDeviceSetChanged(int index)
{
    if (index < 0) {
        return;
    }

    QString fifoLabel = QString("%1 [%2:%3]")
        .arg(m_channelId)
        .arg(m_deviceAPI->getDeviceSetIndex())
        .arg(index);
    m_basebandSink->setAudioFifoLabel(fifoLabel);
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
        announce("register");       // The frequency is known now (the alias is the frequency by default)
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
    record.m_channelAlias = m_settings.effectiveChannelAlias(record.m_frequency);
    record.m_deviceSetIndex = getDeviceSetIndex();
    record.m_channelIndex = getIndexInDeviceSet();
    record.m_centerFrequency = m_centerFrequency;
    record.m_channelOffset = m_settings.m_inputFrequencyOffset;

    // The table of the GUI shows the messages, not the announcements of the channel
    if (getMessageQueueToGUI() && (record.m_kind != MessageRecord::KindEvent)) {
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

    if (settings.m_useReverseAPI)
    {
        bool fullUpdate = (settingsKeys.contains("useReverseAPI") && (m_settings.m_useReverseAPI != settings.m_useReverseAPI)) ||
                settingsKeys.contains("reverseAPIAddress") ||
                settingsKeys.contains("reverseAPIPort") ||
                settingsKeys.contains("reverseAPIDeviceIndex") ||
                settingsKeys.contains("reverseAPIChannelIndex");
        webapiReverseSendSettings(settingsKeys, settings, fullUpdate || force);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    // A new alias or frequency offset is announced at once
    if (m_announceTimer && !force && (settingsKeys.contains("channelAlias") || settingsKeys.contains("inputFrequencyOffset"))) {
        announce("status");
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

int RadioPagingDemod::webapiSettingsGet(
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setRadioPagingDemodSettings(new SWGSDRangel::SWGRadioPagingDemodSettings());
    response.getRadioPagingDemodSettings()->init();
    webapiFormatChannelSettings(response, m_settings);
    return 200;
}

int RadioPagingDemod::webapiWorkspaceGet(
        SWGSDRangel::SWGWorkspaceInfo& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setIndex(m_settings.m_workspaceIndex);
    return 200;
}

int RadioPagingDemod::webapiSettingsPutPatch(
        bool force,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response,
        QString& errorMessage)
{
    RadioPagingDemodSettings settings = m_settings;
    webapiUpdateChannelSettings(settings, channelSettingsKeys, response);

    // The files are written where the host says: an absolute path without parent directory references
    if (channelSettingsKeys.contains("gscVoiceDirectory") && !settings.m_gscVoiceDirectory.isEmpty()
        && (!QDir::isAbsolutePath(settings.m_gscVoiceDirectory) || settings.m_gscVoiceDirectory.split('/').contains("..")))
    {
        errorMessage = "gscVoiceDirectory must be an absolute path without ..";
        return 400;
    }

    MsgConfigureRadioPagingDemod *msg = MsgConfigureRadioPagingDemod::create(channelSettingsKeys, settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureRadioPagingDemod *msgToGUI = MsgConfigureRadioPagingDemod::create(channelSettingsKeys, settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatChannelSettings(response, settings);

    return 200;
}

void RadioPagingDemod::webapiUpdateChannelSettings(
        RadioPagingDemodSettings& settings,
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings& response)
{
    SWGSDRangel::SWGRadioPagingDemodSettings *model = response.getRadioPagingDemodSettings();

    if (!model) {
        return;
    }

    if (channelSettingsKeys.contains("inputFrequencyOffset")) {
        settings.m_inputFrequencyOffset = model->getInputFrequencyOffset();
    }
    if (channelSettingsKeys.contains("rfBandwidth")) {
        settings.m_rfBandwidth = model->getRfBandwidth();
    }
    if (channelSettingsKeys.contains("pocsagEnabled")) {
        settings.m_pocsagEnabled = model->getPocsagEnabled() != 0;
    }
    if (channelSettingsKeys.contains("pocsagDeviation")) {
        settings.m_pocsagDeviation = model->getPocsagDeviation();
    }
    if (channelSettingsKeys.contains("flexEnabled")) {
        settings.m_flexEnabled = model->getFlexEnabled() != 0;
    }
    if (channelSettingsKeys.contains("flexDeviation")) {
        settings.m_flexDeviation = model->getFlexDeviation();
    }
    if (channelSettingsKeys.contains("gscEnabled")) {
        settings.m_gscEnabled = model->getGscEnabled() != 0;
    }
    if (channelSettingsKeys.contains("gscDeviation")) {
        settings.m_gscDeviation = model->getGscDeviation();
    }
    if (channelSettingsKeys.contains("gscVoiceTimeout")) {
        settings.m_gscVoiceTimeout = model->getGscVoiceTimeout();
    }
    if (channelSettingsKeys.contains("gscAudio")) {
        settings.m_gscAudio = model->getGscAudio() != 0;
    }
    if (channelSettingsKeys.contains("gscVoiceRecord")) {
        settings.m_gscVoiceRecord = model->getGscVoiceRecord() != 0;
    }
    if (channelSettingsKeys.contains("gscVoiceDirectory") && model->getGscVoiceDirectory()) {
        settings.m_gscVoiceDirectory = *model->getGscVoiceDirectory();
    }
    if (channelSettingsKeys.contains("gscVoiceKeepDays")) {
        settings.m_gscVoiceKeepDays = std::max(0, std::min(3650, model->getGscVoiceKeepDays()));
    }
    if (channelSettingsKeys.contains("gscVoiceKeepMin")) {
        settings.m_gscVoiceKeepMin = std::max(0, std::min(100000, model->getGscVoiceKeepMin()));
    }
    if (channelSettingsKeys.contains("channelAlias") && model->getChannelAlias()) {
        settings.m_channelAlias = *model->getChannelAlias();
    }
    if (channelSettingsKeys.contains("volume")) {
        settings.m_volume = model->getVolume();
    }
    if (channelSettingsKeys.contains("audioDeviceName") && model->getAudioDeviceName()) {
        settings.m_audioDeviceName = *model->getAudioDeviceName();
    }
    if (channelSettingsKeys.contains("rgbColor")) {
        settings.m_rgbColor = model->getRgbColor();
    }
    if (channelSettingsKeys.contains("title") && model->getTitle()) {
        settings.m_title = *model->getTitle();
    }
    if (channelSettingsKeys.contains("streamIndex")) {
        settings.m_streamIndex = model->getStreamIndex();
    }
    if (channelSettingsKeys.contains("useReverseAPI")) {
        settings.m_useReverseAPI = model->getUseReverseApi() != 0;
    }
    if (channelSettingsKeys.contains("reverseAPIAddress") && model->getReverseApiAddress()) {
        settings.m_reverseAPIAddress = *model->getReverseApiAddress();
    }
    if (channelSettingsKeys.contains("reverseAPIPort")) {
        settings.m_reverseAPIPort = model->getReverseApiPort();
    }
    if (channelSettingsKeys.contains("reverseAPIDeviceIndex")) {
        settings.m_reverseAPIDeviceIndex = model->getReverseApiDeviceIndex();
    }
    if (channelSettingsKeys.contains("reverseAPIChannelIndex")) {
        settings.m_reverseAPIChannelIndex = model->getReverseApiChannelIndex();
    }
    if (settings.m_channelMarker && channelSettingsKeys.contains("channelMarker") && model->getChannelMarker()) {
        settings.m_channelMarker->updateFrom(channelSettingsKeys, model->getChannelMarker());
    }
    if (settings.m_rollupState && channelSettingsKeys.contains("rollupState") && model->getRollupState()) {
        settings.m_rollupState->updateFrom(channelSettingsKeys, model->getRollupState());
    }
}

int RadioPagingDemod::webapiReportGet(
        SWGSDRangel::SWGChannelReport& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setRadioPagingDemodReport(new SWGSDRangel::SWGRadioPagingDemodReport());
    response.getRadioPagingDemodReport()->init();
    webapiFormatChannelReport(response);
    return 200;
}

void RadioPagingDemod::webapiFormatChannelSettings(SWGSDRangel::SWGChannelSettings& response, const RadioPagingDemodSettings& settings)
{
    SWGSDRangel::SWGRadioPagingDemodSettings *model = response.getRadioPagingDemodSettings();
    model->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    model->setRfBandwidth(settings.m_rfBandwidth);
    model->setPocsagEnabled(settings.m_pocsagEnabled ? 1 : 0);
    model->setPocsagDeviation(settings.m_pocsagDeviation);
    model->setFlexEnabled(settings.m_flexEnabled ? 1 : 0);
    model->setFlexDeviation(settings.m_flexDeviation);
    model->setGscEnabled(settings.m_gscEnabled ? 1 : 0);
    model->setGscDeviation(settings.m_gscDeviation);
    model->setGscVoiceTimeout(settings.m_gscVoiceTimeout);
    model->setGscAudio(settings.m_gscAudio ? 1 : 0);
    model->setGscVoiceRecord(settings.m_gscVoiceRecord ? 1 : 0);
    model->setVolume(settings.m_volume);

    model->setGscVoiceKeepDays(settings.m_gscVoiceKeepDays);
    model->setGscVoiceKeepMin(settings.m_gscVoiceKeepMin);

    if (model->getChannelAlias()) {
        *model->getChannelAlias() = settings.m_channelAlias;
    } else {
        model->setChannelAlias(new QString(settings.m_channelAlias));
    }

    if (model->getGscVoiceDirectory()) {
        *model->getGscVoiceDirectory() = settings.m_gscVoiceDirectory;
    } else {
        model->setGscVoiceDirectory(new QString(settings.m_gscVoiceDirectory));
    }

    if (model->getAudioDeviceName()) {
        *model->getAudioDeviceName() = settings.m_audioDeviceName;
    } else {
        model->setAudioDeviceName(new QString(settings.m_audioDeviceName));
    }

    model->setRgbColor(settings.m_rgbColor);
    model->setTitle(new QString(settings.m_title));
    model->setStreamIndex(settings.m_streamIndex);
    model->setUseReverseApi(settings.m_useReverseAPI ? 1 : 0);
    model->setReverseApiAddress(new QString(settings.m_reverseAPIAddress));
    model->setReverseApiPort(settings.m_reverseAPIPort);
    model->setReverseApiDeviceIndex(settings.m_reverseAPIDeviceIndex);
    model->setReverseApiChannelIndex(settings.m_reverseAPIChannelIndex);

    if (settings.m_channelMarker)
    {
        SWGSDRangel::SWGChannelMarker *swgChannelMarker = new SWGSDRangel::SWGChannelMarker();
        settings.m_channelMarker->formatTo(swgChannelMarker);
        model->setChannelMarker(swgChannelMarker);
    }

    if (settings.m_rollupState)
    {
        SWGSDRangel::SWGRollupState *swgRollupState = new SWGSDRangel::SWGRollupState();
        settings.m_rollupState->formatTo(swgRollupState);
        model->setRollupState(swgRollupState);
    }
}

void RadioPagingDemod::webapiFormatChannelReport(SWGSDRangel::SWGChannelReport& response)
{
    double magsqAvg, magsqPeak;
    int nbMagsqSamples;
    getMagSqLevels(magsqAvg, magsqPeak, nbMagsqSamples);
    RadioPagingDemodSink::Status status = getStatus();
    SWGSDRangel::SWGRadioPagingDemodReport *model = response.getRadioPagingDemodReport();

    // 0 not synchronised, 1 synchronised, 2 synchronised on inverted polarity
    auto sync = [](bool synchronised, bool inverted) { return synchronised ? (inverted ? 2 : 1) : 0; };

    model->setChannelPowerDb(CalcDb::dbPower(magsqAvg));
    model->setChannelSampleRate(RadioPagingDemodSettings::m_channelSampleRate);
    model->setPocsag512Sync(sync(status.m_pocsagSync[0], status.m_pocsagInverted[0]));
    model->setPocsag1200Sync(sync(status.m_pocsagSync[1], status.m_pocsagInverted[1]));
    model->setPocsag2400Sync(sync(status.m_pocsagSync[2], status.m_pocsagInverted[2]));
    model->setFlexSync(sync(status.m_flexSync, status.m_flexInverted));
    model->setFlexMode(new QString(status.m_flexMode));
    model->setFlexQuality(status.m_flexQuality);
    model->setFlexErs(status.m_flexErs ? 1 : 0);
    model->setFlexErsSeconds(status.m_flexErsSeconds);
    model->setFlexErsCount(status.m_flexErsCount);
    model->setGscSync(sync(status.m_gscSync, status.m_gscInverted));
    model->setGscVoice(status.m_gscVoice ? 1 : 0);
    model->setPocsagMessages(status.m_messages[0]);
    model->setFlexMessages(status.m_messages[1]);
    model->setGscMessages(status.m_messages[2]);
}

void RadioPagingDemod::webapiReverseSendSettings(const QStringList& channelSettingsKeys, const RadioPagingDemodSettings& settings, bool force)
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

void RadioPagingDemod::webapiFormatChannelSettings(
        const QStringList& channelSettingsKeys,
        SWGSDRangel::SWGChannelSettings *swgChannelSettings,
        const RadioPagingDemodSettings& settings,
        bool force)
{
    swgChannelSettings->setDirection(0); // Single sink (Rx)
    swgChannelSettings->setOriginatorChannelIndex(getIndexInDeviceSet());
    swgChannelSettings->setOriginatorDeviceSetIndex(getDeviceSetIndex());
    swgChannelSettings->setChannelType(new QString(m_channelId));
    swgChannelSettings->setRadioPagingDemodSettings(new SWGSDRangel::SWGRadioPagingDemodSettings());
    SWGSDRangel::SWGRadioPagingDemodSettings *model = swgChannelSettings->getRadioPagingDemodSettings();

    // Only the keys that changed, unless force
    if (channelSettingsKeys.contains("inputFrequencyOffset") || force) {
        model->setInputFrequencyOffset(settings.m_inputFrequencyOffset);
    }
    if (channelSettingsKeys.contains("rfBandwidth") || force) {
        model->setRfBandwidth(settings.m_rfBandwidth);
    }
    if (channelSettingsKeys.contains("pocsagEnabled") || force) {
        model->setPocsagEnabled(settings.m_pocsagEnabled ? 1 : 0);
    }
    if (channelSettingsKeys.contains("pocsagDeviation") || force) {
        model->setPocsagDeviation(settings.m_pocsagDeviation);
    }
    if (channelSettingsKeys.contains("flexEnabled") || force) {
        model->setFlexEnabled(settings.m_flexEnabled ? 1 : 0);
    }
    if (channelSettingsKeys.contains("flexDeviation") || force) {
        model->setFlexDeviation(settings.m_flexDeviation);
    }
    if (channelSettingsKeys.contains("gscEnabled") || force) {
        model->setGscEnabled(settings.m_gscEnabled ? 1 : 0);
    }
    if (channelSettingsKeys.contains("gscDeviation") || force) {
        model->setGscDeviation(settings.m_gscDeviation);
    }
    if (channelSettingsKeys.contains("gscVoiceTimeout") || force) {
        model->setGscVoiceTimeout(settings.m_gscVoiceTimeout);
    }
    if (channelSettingsKeys.contains("gscAudio") || force) {
        model->setGscAudio(settings.m_gscAudio ? 1 : 0);
    }
    if (channelSettingsKeys.contains("gscVoiceRecord") || force) {
        model->setGscVoiceRecord(settings.m_gscVoiceRecord ? 1 : 0);
    }
    if (channelSettingsKeys.contains("gscVoiceDirectory") || force) {
        model->setGscVoiceDirectory(new QString(settings.m_gscVoiceDirectory));
    }
    if (channelSettingsKeys.contains("gscVoiceKeepDays") || force) {
        model->setGscVoiceKeepDays(settings.m_gscVoiceKeepDays);
    }
    if (channelSettingsKeys.contains("gscVoiceKeepMin") || force) {
        model->setGscVoiceKeepMin(settings.m_gscVoiceKeepMin);
    }
    if (channelSettingsKeys.contains("channelAlias") || force) {
        model->setChannelAlias(new QString(settings.m_channelAlias));
    }
    if (channelSettingsKeys.contains("volume") || force) {
        model->setVolume(settings.m_volume);
    }
    if (channelSettingsKeys.contains("audioDeviceName") || force) {
        model->setAudioDeviceName(new QString(settings.m_audioDeviceName));
    }
    if (channelSettingsKeys.contains("rgbColor") || force) {
        model->setRgbColor(settings.m_rgbColor);
    }
    if (channelSettingsKeys.contains("title") || force) {
        model->setTitle(new QString(settings.m_title));
    }
    if (channelSettingsKeys.contains("streamIndex") || force) {
        model->setStreamIndex(settings.m_streamIndex);
    }

    if (settings.m_channelMarker && (channelSettingsKeys.contains("channelMarker") || force))
    {
        SWGSDRangel::SWGChannelMarker *swgChannelMarker = new SWGSDRangel::SWGChannelMarker();
        settings.m_channelMarker->formatTo(swgChannelMarker);
        model->setChannelMarker(swgChannelMarker);
    }

    if (settings.m_rollupState && (channelSettingsKeys.contains("rollupState") || force))
    {
        SWGSDRangel::SWGRollupState *swgRollupState = new SWGSDRangel::SWGRollupState();
        settings.m_rollupState->formatTo(swgRollupState);
        model->setRollupState(swgRollupState);
    }
}

void RadioPagingDemod::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "RadioPagingDemod::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
    }
    else
    {
        QString answer = reply->readAll();
        answer.chop(1); // remove last \n
        qDebug("RadioPagingDemod::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
    }

    reply->deleteLater();
}
