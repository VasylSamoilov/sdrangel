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
#include <QFileDialog>
#include <QAction>
#include <QDesktopServices>
#include <QDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <QStyle>
#include <QSignalBlocker>
#include <QStandardItemModel>

#include "dsp/dspcommands.h"
#include "device/deviceuiset.h"
#include "plugin/pluginapi.h"
#include "util/db.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialpopup.h"
#include "gui/dialogpositioner.h"
#include "gui/audioselectdialog.h"
#include "gui/crightclickenabler.h"
#include "dsp/dspengine.h"
#include "audio/audiodevicemanager.h"
#include "radiopagingwav.h"
#include "radiopagingvoicestore.h"
#include "maincore.h"

#include "ui_radiopagingmodgui.h"
#include "radiopagingmodgui.h"

namespace {

int pocsagSpeedIndex(int baud)
{
    for (int i = 0; i < RadioPagingModSettings::m_nbPocsagSpeeds; i++)
    {
        if (RadioPagingModSettings::m_pocsagSpeeds[i] == baud) {
            return i;
        }
    }

    return 1;
}

QString stateName(RadioPagingModSource::State state)
{
    switch (state)
    {
    case RadioPagingModSource::Idle:
        return "Idle";
    case RadioPagingModSource::RampUp:
        return "Ramp up";
    case RadioPagingModSource::Tx:
        return "Transmitting";
    case RadioPagingModSource::RampDown:
        return "Ramp down";
    }

    return "";
}

// A combo box item that cannot be selected
void setItemEnabled(QComboBox *combo, int index, bool enabled)
{
    QStandardItemModel *model = qobject_cast<QStandardItemModel*>(combo->model());

    if (model && model->item(index)) {
        model->item(index)->setEnabled(enabled);
    }
}

} // namespace

RadioPagingModGUI* RadioPagingModGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *channelTx)
{
    RadioPagingModGUI* gui = new RadioPagingModGUI(pluginAPI, deviceUISet, channelTx);
    return gui;
}

void RadioPagingModGUI::destroy()
{
    delete this;
}

void RadioPagingModGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(QStringList(), true);
}

QByteArray RadioPagingModGUI::serialize() const
{
    return m_settings.serialize();
}

bool RadioPagingModGUI::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        displaySettings();
        applySettings(QStringList(), true);
        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

bool RadioPagingModGUI::handleMessage(const Message& message)
{
    if (RadioPagingMod::MsgConfigureRadioPagingMod::match(message))
    {
        const RadioPagingMod::MsgConfigureRadioPagingMod& cfg = (RadioPagingMod::MsgConfigureRadioPagingMod&) message;

        if (cfg.getForce()) {
            m_settings = cfg.getSettings();
        } else {
            m_settings.applySettings(cfg.getSettingsKeys(), cfg.getSettings());
        }

        blockApplySettings(true);
        m_channelMarker.updateSettings(static_cast<const ChannelMarker*>(m_settings.m_channelMarker));
        displaySettings();
        blockApplySettings(false);
        return true;
    }
    else if (RadioPagingMod::MsgReportTx::match(message))
    {
        const RadioPagingMod::MsgReportTx& report = (RadioPagingMod::MsgReportTx&) message;

        if (m_liveWaiting)
        {
            // The live voice page is queued: the button becomes End, or the request was refused
            m_liveWaiting = false;
            ui->send->setEnabled(true);

            if (report.getId() != 0)
            {
                m_liveId = report.getId();
                m_liveSeen = false;
                ui->send->setText("End (alert time)");
            }
            else
            {
                displayMessageType();
            }
        }

        if (report.getId() != 0)
        {
            log(QString("#%1 queued").arg(report.getId()));

            if (!report.getWarning().isEmpty()) {
                log(QString("Warning: %1").arg(report.getWarning()));
            }
        }
        else {
            log(QString("Refused: %1").arg(report.getError()));
        }
        return true;
    }
    else if (RadioPagingMod::MsgTxLog::match(message))
    {
        const RadioPagingMod::MsgTxLog& entry = (const RadioPagingMod::MsgTxLog&) message;
        showTx(entry.getRecord(), entry.getStatus());

        // A live voice page that is dropped (aborted, cleared) is over
        if ((entry.getStatus() == "dropped") && (m_liveId != 0)
         && (entry.getRecord().m_protocolData.value("tx_id").toULongLong() == m_liveId))
        {
            log("Voice page dropped");
            m_liveId = 0;
            stopMic();
            m_liveSeen = false;
            ui->send->setEnabled(true);
            displayMessageType();
        }

        return true;
    }
    else if (DSPSignalNotification::match(message))
    {
        const DSPSignalNotification& notif = (const DSPSignalNotification&) message;
        m_deviceCenterFrequency = notif.getCenterFrequency();
        m_basebandSampleRate = notif.getSampleRate();
        ui->deltaFrequency->setValueRange(false, 7, -m_basebandSampleRate/2, m_basebandSampleRate/2);
        ui->deltaFrequencyLabel->setToolTip(tr("Range %1 %L2 Hz").arg(QChar(0xB1)).arg(m_basebandSampleRate/2));
        updateAbsoluteCenterFrequency();
        return true;
    }
    else
    {
        return false;
    }
}

void RadioPagingModGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void RadioPagingModGUI::handleSourceMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop()) != nullptr)
    {
        if (handleMessage(*message)) {
            delete message;
        }
    }
}

void RadioPagingModGUI::on_deltaFrequency_changed(qint64 value)
{
    m_channelMarker.setCenterFrequency(value);
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void RadioPagingModGUI::on_channelMute_toggled(bool checked)
{
    m_settings.m_channelMute = checked;
    applySettings(QStringList("channelMute"));
}

// A change by the user: the deviation goes to the usual one of the protocol. While the
// settings are displayed (preset, WebAPI) they are kept as they are
void RadioPagingModGUI::on_protocol_currentIndexChanged(int index)
{
    if (!m_doApplySettings) {
        return;
    }

    m_settings.m_protocol = (RadioPagingModSettings::Protocol) index;
    m_settings.m_deviation = RadioPagingModSettings::defaultDeviation(m_settings.m_protocol);
    blockApplySettings(true);
    ui->deviation->setValue((int) m_settings.m_deviation);
    displayProtocol();
    blockApplySettings(false);
    m_channelMarker.setBandwidth(channelBandwidth());
    applySettings(QStringList({"protocol", "deviation"}));
}

void RadioPagingModGUI::on_mode_currentIndexChanged(int index)
{
    if (!m_doApplySettings) {
        return;
    }

    m_settings.m_mode = (RadioPagingModSettings::Mode) index;
    blockApplySettings(true);
    displayProtocol();
    blockApplySettings(false);
    applySettings(QStringList("mode"));
}

void RadioPagingModGUI::on_deviation_valueChanged(int value)
{
    m_settings.m_deviation = value;
    m_channelMarker.setBandwidth(channelBandwidth());
    applySettings(QStringList("deviation"));
}

void RadioPagingModGUI::on_gain_valueChanged(int value)
{
    ui->gainText->setText(QString("%1dB").arg(value));
    m_settings.m_gain = value;
    applySettings(QStringList("gain"));
}

// POCSAG speed or FLEX mode
void RadioPagingModGUI::on_speed_currentIndexChanged(int index)
{
    if (index < 0) {
        return;
    }

    if (m_settings.m_protocol == RadioPagingModSettings::Gsc)
    {
        if (index <= (int) RadioPagingGsc::NonBatterySaver)
        {
            m_settings.m_gscMode = index;
            applySettings(QStringList("gscMode"));
        }
    }
    else if (m_settings.m_protocol == RadioPagingModSettings::Flex)
    {
        if (index <= (int) RadioPagingFlex::Mode6400_4)
        {
            m_settings.m_flexMode = index;
            m_channelMarker.setBandwidth(channelBandwidth());
            applySettings(QStringList("flexMode"));
        }
    }
    else if (index < RadioPagingModSettings::m_nbPocsagSpeeds)
    {
        m_settings.m_pocsagSpeed = RadioPagingModSettings::m_pocsagSpeeds[index];
        m_channelMarker.setBandwidth(channelBandwidth());
        applySettings(QStringList("pocsagSpeed"));
    }
}

void RadioPagingModGUI::on_polarity_currentIndexChanged(int index)
{
    if (m_settings.m_protocol == RadioPagingModSettings::Gsc)
    {
        m_settings.m_gscInverted = (index == 1);
        applySettings(QStringList("gscInverted"));
    }
    else if (m_settings.m_protocol == RadioPagingModSettings::Flex)
    {
        m_settings.m_flexInverted = (index == 1);
        applySettings(QStringList("flexInverted"));
    }
    else
    {
        m_settings.m_pocsagInverted = (index == 1);
        applySettings(QStringList("pocsagInverted"));
    }
}

void RadioPagingModGUI::on_collapse_valueChanged(int value)
{
    m_settings.m_flexCollapse = value;
    applySettings(QStringList("flexCollapse"));
}

void RadioPagingModGUI::on_roaming_toggled(bool checked)
{
    m_settings.m_flexRoaming = checked;
    applySettings(QStringList("flexRoaming"));
    displaySettings();
}

void RadioPagingModGUI::on_ssidZone_valueChanged(int value)
{
    m_settings.m_ssidCoverageZone = value;
    applySettings(QStringList("ssidCoverageZone"));
}

void RadioPagingModGUI::on_ssidLocalId_valueChanged(int value)
{
    m_settings.m_ssidLocalId = value;
    applySettings(QStringList("ssidLocalId"));
}

void RadioPagingModGUI::on_ssidCountry_valueChanged(int value)
{
    m_settings.m_ssidCountryCode = value;
    applySettings(QStringList("ssidCountryCode"));
}

void RadioPagingModGUI::on_ssidFlags_valueChanged(int value)
{
    m_settings.m_ssidTrafficFlags = value;
    applySettings(QStringList("ssidTrafficFlags"));
}

void RadioPagingModGUI::on_voiceSeconds_valueChanged(int value)
{
    m_settings.m_gscVoiceSeconds = value;
    applySettings(QStringList("gscVoiceSeconds"));
}

void RadioPagingModGUI::on_rampUp_valueChanged(double value)
{
    m_settings.m_rampUpMs = value;
    applySettings(QStringList("rampUpMs"));
}

void RadioPagingModGUI::on_rampDown_valueChanged(double value)
{
    m_settings.m_rampDownMs = value;
    applySettings(QStringList("rampDownMs"));
}

void RadioPagingModGUI::on_rampRange_valueChanged(int value)
{
    m_settings.m_rampRange = value;
    applySettings(QStringList("rampRange"));
}

// The message of the protocol of the channel; the channel checks it and reports the result
void RadioPagingModGUI::send()
{
    static const RadioPagingPocsag::MessageType pocsagTypes[3] = {
        RadioPagingPocsag::Alphanumeric, RadioPagingPocsag::Numeric, RadioPagingPocsag::Tone
    };
    static const RadioPagingFlex::MessageType flexTypes[3] = {
        RadioPagingFlex::Alphanumeric, RadioPagingFlex::Numeric, RadioPagingFlex::Tone
    };
    // The button is End while a live voice page is going on
    if (m_liveId != 0)
    {
        log("Voice: ended");
        endLive();
        return;
    }
    if (m_liveWaiting) {
        return;
    }

    RadioPagingMod::TxRequest request;
    bool gscMessage = (m_settings.m_protocol == RadioPagingModSettings::Gsc);
    int typeIndex = std::max(0, std::min(gscMessage ? 3 : 2, ui->messageType->currentIndex()));
    bool tone = (typeIndex == 2);
    bool voice = (typeIndex == 3);
    std::string text = (tone || voice) ? std::string() : ui->text->text().toStdString();
    int overrideIndex = ui->messageSpeed->currentIndex();     // POCSAG speed

    bool flexMessage = messageIsFlex();

    // A mixed FLEX channel also sends POCSAG messages
    request.m_protocol = (m_settings.m_protocol == RadioPagingModSettings::Flex)
        ? (int) (flexMessage ? RadioPagingModSettings::Flex : RadioPagingModSettings::Pocsag) : (int) m_settings.m_protocol;
    request.m_polarity = ui->messagePolarity->currentIndex() - 1;

    if (m_settings.m_protocol == RadioPagingModSettings::Gsc)
    {
        // Digits only, zero padded to the 6 digits of the address (as the WebAPI integer)
        QString gscAddress = ui->capcode->text().trimmed();

        if (!gscAddress.isEmpty() && (gscAddress.size() < 6)) {
            gscAddress = gscAddress.rightJustified(6, QChar('0'));
        }

        request.m_gsc.m_address = gscAddress.toStdString();
        request.m_gsc.m_addressNumber = ui->function->currentIndex() + 1;
        request.m_gsc.m_type = voice ? RadioPagingGsc::Voice : (typeIndex == 2) ? RadioPagingGsc::Tone
            : (typeIndex == 1) ? RadioPagingGsc::Numeric : RadioPagingGsc::Alphanumeric;
        request.m_gsc.m_text = text;
        bool microphone = voice && (ui->voiceSource->currentIndex() == 1);
        request.m_voiceFile = (voice && !microphone) ? ui->text->text().trimmed() : QString();
        request.m_live = microphone;
        m_liveWaiting = microphone;

        if (microphone)
        {
            // The page goes out, the microphone is taken after the alert time
            ui->send->setEnabled(false);
        }

        log(QString("%1/%2 %3: %4")
            .arg(gscAddress)
            .arg(request.m_gsc.m_addressNumber)
            .arg(ui->messageType->currentText())
            .arg(microphone ? QString("microphone") : voice ? request.m_voiceFile : QString::fromStdString(text)));
    }
    else if (flexMessage)
    {
        bool ok;
        qulonglong capcode = ui->capcode->text().trimmed().toULongLong(&ok);

        request.m_flex.m_capcode = ok ? capcode : 0;
        request.m_flex.m_type = flexTypes[typeIndex];
        request.m_flex.m_text = text;
        request.m_flex.m_priority = ui->priority->isChecked();

        log(QString("%1%2 %3: %4")
            .arg(ui->capcode->text().trimmed())
            .arg(request.m_flex.m_priority ? " priority" : "")
            .arg(ui->messageType->currentText())
            .arg(QString::fromStdString(text)));
    }
    else
    {
        request.m_pocsag.m_address = (uint32_t) ui->address->value();
        request.m_pocsag.m_function = ui->function->currentIndex();
        request.m_pocsag.m_type = pocsagTypes[typeIndex];
        request.m_pocsag.m_text = text;
        request.m_speed = (overrideIndex > 0) ? RadioPagingModSettings::m_pocsagSpeeds[overrideIndex - 1] : 0;

        log(QString("%1 %2 %3: %4")
            .arg(ui->address->value(), 7, 10, QChar('0'))
            .arg(QChar('A' + request.m_pocsag.m_function))
            .arg(ui->messageType->currentText())
            .arg(QString::fromStdString(text)));
    }

    m_radioPagingMod->getInputMessageQueue()->push(RadioPagingMod::MsgTx::create(request));
}

void RadioPagingModGUI::on_send_clicked()
{
    send();
}

void RadioPagingModGUI::on_text_returnPressed()
{
    send();
}

void RadioPagingModGUI::on_messageType_currentIndexChanged(int index)
{
    (void) index;
    displayMessageType();
}

void RadioPagingModGUI::displayMessageType()
{
    bool voice = (m_settings.m_protocol == RadioPagingModSettings::Gsc) && (ui->messageType->currentIndex() == 3);

    if (m_browseAction) {
        m_browseAction->setVisible(voice);
    }

    // A voice page: the voice comes from a file, or from the microphone after the alert time
    bool microphone = voice && (ui->voiceSource->currentIndex() == 1);
    ui->voiceSource->setVisible(voice);
    ui->micInput->setVisible(microphone);
    ui->text->setVisible(!microphone);
    ui->textLabel->setVisible(!microphone);

    if (m_browseAction) {
        m_browseAction->setVisible(voice && !microphone);
    }

    if (!microphone) {
        ui->send->setText("Send");
    } else if (m_liveId == 0) {
        ui->send->setText("Send voice");
    }

    ui->textLabel->setText(voice ? "File" : "Text");
    ui->text->setPlaceholderText(voice ? "WAV file" : QString());
    ui->text->setToolTip(voice
        ? "Path of a WAV file (8 to 32 bit PCM or float, up to 76.8 kHz). Sent after an alert of 2 s, "
          "cut at the voice length. Enter to send"
        : "Message text. Numeric: 0-9, space, U, -, ], [. Alphanumeric: 7-bit characters. Enter to send");
}

void RadioPagingModGUI::micAudioSelect(const QPoint& p)
{
    AudioSelectDialog audioSelect(DSPEngine::instance()->getAudioDeviceManager(), m_settings.m_audioInputDeviceName, true); // true for input
    audioSelect.move(p);
    new DialogPositioner(&audioSelect, false);
    audioSelect.exec();

    if (audioSelect.m_selected)
    {
        m_settings.m_audioInputDeviceName = audioSelect.m_audioDeviceName;
        applySettings(QStringList("audioInputDeviceName"));
    }
}

void RadioPagingModGUI::on_micInput_clicked()
{
    micAudioSelect(QCursor::pos());
}

void RadioPagingModGUI::on_voiceSource_currentIndexChanged(int index)
{
    (void) index;
    displayMessageType();
}

// The microphone is taken when the alert time of the live voice page is over (tick), and
// released when the page ends: End, or the voice length
void RadioPagingModGUI::startMic()
{
    AudioDeviceManager *audioDeviceManager = DSPEngine::instance()->getAudioDeviceManager();
    int index = audioDeviceManager->getInputDeviceIndex(m_settings.m_audioInputDeviceName);
    m_micRate = audioDeviceManager->getInputSampleRate(index);

    if (!m_micResampler.setup(m_micRate))
    {
        log(QString("Microphone: the audio input rate %1 Hz is not supported (1 to 76.8 kHz)").arg(m_micRate));
        endLive();
        return;
    }

    m_micSent = 0;
    m_micFifo.setSize(m_micRate);
    audioDeviceManager->addAudioSource(&m_micFifo, m_radioPagingMod->getInputMessageQueue(), index);
    m_micRecording = true;
    log("Microphone: on the air");
}

void RadioPagingModGUI::stopMic()
{
    if (!m_micRecording) {
        return;
    }

    DSPEngine::instance()->getAudioDeviceManager()->removeAudioSource(&m_micFifo);
    readMic();              // What the fifo still holds
    m_micRecording = false;
}

// The voice from the audio input, the channels averaged, resampled to the rate of the modulator
// as it comes and passed to the transmitter. It stops at the voice length
void RadioPagingModGUI::readMic()
{
    AudioVector buffer(4096);
    std::vector<float> mono;
    std::vector<float> out;
    const quint64 limit = (quint64) m_settings.m_gscVoiceSeconds * RadioPagingWav::m_sampleRate;
    uint32_t n;

    while ((n = m_micFifo.read((quint8*) &buffer[0], buffer.size())) > 0)
    {
        mono.clear();

        for (uint32_t i = 0; i < n; i++) {
            mono.push_back((buffer[i].l + buffer[i].r) / (2.0f * 32768.0f));
        }

        out.clear();
        m_micResampler.process(mono.data(), mono.size(), out);

        if (m_micSent + out.size() > limit) {
            out.resize((size_t) (limit - std::min(limit, m_micSent)));
        }

        m_micSent += out.size();

        if (!out.empty()) {
            m_radioPagingMod->getInputMessageQueue()->push(RadioPagingMod::MsgLiveVoice::create(m_liveId, out));
        }
    }
}

void RadioPagingModGUI::micDataReady()
{
    if (m_micRecording) {
        readMic();
    }
}

// End of the live voice page: End, or the page ended by itself
void RadioPagingModGUI::endLive()
{
    stopMic();

    if (m_liveId != 0)
    {
        if (m_liveSeen) {
            m_radioPagingMod->getInputMessageQueue()->push(RadioPagingMod::MsgLiveEnd::create(m_liveId));
        } else {
            m_radioPagingMod->getInputMessageQueue()->push(RadioPagingMod::MsgClear::create(true));     // Not started: dropped
        }
    }

    m_liveId = 0;
    m_liveSeen = false;
    m_liveWaiting = false;
    ui->send->setEnabled(true);
    displayMessageType();
}

// The live voice page: the request, the End button and the counter while it goes on
void RadioPagingModGUI::updateLive(const RadioPagingModSource::Status& status)
{
    if (m_liveId == 0) {
        return;
    }

    bool mine = status.m_liveActive && (status.m_liveId == m_liveId);

    if (mine) {
        m_liveSeen = true;
    }

    if (mine && status.m_liveOpen && !m_micRecording) {
        startMic();
    }

    if (m_liveSeen && !mine)
    {
        // Ended by the voice length (or the page was cut)
        log("Voice page ended");
        endLive();
        return;
    }

    if (m_micRecording)
    {
        ui->send->setText(QString("End %1 / %2 s").arg((double) m_micSent / RadioPagingWav::m_sampleRate, 0, 'f', 1)
            .arg(m_settings.m_gscVoiceSeconds));
    }
    else
    {
        ui->send->setText("End (alert time)");
    }
}

void RadioPagingModGUI::on_channelAlias_editingFinished()
{
    m_settings.m_channelAlias = ui->channelAlias->text().trimmed();
    applySettings(QStringList("channelAlias"));
}

// The last messages transmitted, the newest first: a row per message, its state updated as it is
// sent or dropped
void RadioPagingModGUI::showTx(const MessageRecord& record, const QString& status)
{
    const int maxRows = 100;
    QString id = record.m_protocolData.value("tx_id").toString();
    int row = -1;

    for (int i = 0; i < ui->txLog->rowCount(); i++)
    {
        if (ui->txLog->item(i, 0) && (ui->txLog->item(i, 0)->data(Qt::UserRole).toString() == id)) {
            row = i;
            break;
        }
    }

    if (row < 0)
    {
        ui->txLog->insertRow(0);
        row = 0;

        while (ui->txLog->rowCount() > maxRows) {
            ui->txLog->removeRow(ui->txLog->rowCount() - 1);
        }

        for (int column = 0; column < 6; column++) {
            ui->txLog->setItem(row, column, new QTableWidgetItem());
        }
    }

    QString text = (record.m_msgType == "voice") ? QString("(voice)") : record.m_text;
    QString file = record.m_protocolData.value("voice_file").toString();

    if (!file.isEmpty()) {
        text = QString("(voice %1 s) %2").arg(record.m_protocolData.value("voice_seconds").toDouble(), 0, 'f', 1).arg(QFileInfo(file).fileName());
    }

    ui->txLog->item(row, 0)->setText(QDateTime::fromMSecsSinceEpoch(record.m_timestamp).toString("HH:mm:ss"));
    ui->txLog->item(row, 0)->setData(Qt::UserRole, id);
    ui->txLog->item(row, 0)->setData(Qt::UserRole + 1, file);
    ui->txLog->item(row, 1)->setText(QString("%1 %2").arg(record.m_protocol).arg(record.m_mode));
    ui->txLog->item(row, 2)->setText(record.m_address);
    ui->txLog->item(row, 3)->setText(record.m_msgType);
    ui->txLog->item(row, 4)->setText(text);
    ui->txLog->item(row, 5)->setText(status);
    ui->txLog->resizeColumnsToContents();
}

// A voice message is played from its file
void RadioPagingModGUI::on_txLog_cellDoubleClicked(int row, int column)
{
    (void) column;
    QString file = ui->txLog->item(row, 0) ? ui->txLog->item(row, 0)->data(Qt::UserRole + 1).toString() : QString();

    if (!file.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(file));
    }
}

// The files of the voice sent: the statistics of the directory, the files kept, the deletion of the old ones
void RadioPagingModGUI::on_voiceFiles_clicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Files of the voice sent");
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QFormLayout *form = new QFormLayout();
    QSpinBox *days = new QSpinBox(&dialog);
    QSpinBox *minimum = new QSpinBox(&dialog);
    QLabel *stats = new QLabel(&dialog);
    QPushButton *clean = new QPushButton("Delete the old files...", &dialog);
    QPushButton *close = new QPushButton("Close", &dialog);

    days->setRange(0, 3650);
    days->setValue(m_settings.m_gscVoiceKeepDays);
    days->setSpecialValueText("Keep all");
    days->setSuffix(" days");
    days->setToolTip("Files older than this are deleted, a few at a time, when a new file of the voice sent is written. "
        "Keep all: no file is deleted for its age");
    minimum->setRange(0, 100000);
    minimum->setValue(m_settings.m_gscVoiceKeepMin);
    minimum->setToolTip("The newest files kept whatever their age");
    form->addRow("Delete after", days);
    form->addRow("Always keep the newest", minimum);
    layout->addLayout(form);
    layout->addWidget(stats);
    layout->addWidget(clean);
    layout->addWidget(close);

    auto refresh = [this, days, minimum, stats, clean]()
    {
        RadioPagingVoiceStore::Stats s = RadioPagingVoiceStore::stats(m_settings.voiceDirectory(), days->value(), minimum->value());
        stats->setText(QString("Directory: %1\nFiles: %2, %3\nOldest: %4\nNewest: %5\nTo delete now: %6 files, %7\nFree space on the disk: %8")
            .arg(m_settings.voiceDirectory())
            .arg(s.m_files).arg(RadioPagingVoiceStore::sizeText(s.m_bytes))
            .arg(s.m_oldest.isValid() ? s.m_oldest.toString("yyyy-MM-dd HH:mm") : QString("-"))
            .arg(s.m_newest.isValid() ? s.m_newest.toString("yyyy-MM-dd HH:mm") : QString("-"))
            .arg(s.m_expired).arg(RadioPagingVoiceStore::sizeText(s.m_expiredBytes))
            .arg(RadioPagingVoiceStore::sizeText(s.m_freeBytes)));
        clean->setEnabled(s.m_expired > 0);
    };

    QObject::connect(days, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, [this, refresh](int value) {
        m_settings.m_gscVoiceKeepDays = value;
        applySettings(QStringList("gscVoiceKeepDays"));
        refresh();
    });
    QObject::connect(minimum, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, [this, refresh](int value) {
        m_settings.m_gscVoiceKeepMin = value;
        applySettings(QStringList("gscVoiceKeepMin"));
        refresh();
    });
    QObject::connect(clean, &QPushButton::clicked, &dialog, [this, &dialog, refresh]()
    {
        RadioPagingVoiceStore::Stats s = RadioPagingVoiceStore::stats(m_settings.voiceDirectory(),
            m_settings.m_gscVoiceKeepDays, m_settings.m_gscVoiceKeepMin);

        if (QMessageBox::question(&dialog, "Delete the old files",
            QString("Delete %1 files of the voice sent (%2) from\n%3 ?\nThis cannot be undone.").arg(s.m_expired)
            .arg(RadioPagingVoiceStore::sizeText(s.m_expiredBytes)).arg(m_settings.voiceDirectory())) == QMessageBox::Yes)
        {
            RadioPagingVoiceStore::cleanup(m_settings.voiceDirectory(), m_settings.m_gscVoiceKeepDays, m_settings.m_gscVoiceKeepMin, -1);
        }

        refresh();
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    refresh();
    dialog.exec();
}

void RadioPagingModGUI::on_browse_triggered()
{
    QString file = QFileDialog::getOpenFileName(this, "Sound file", ui->text->text().trimmed(), "WAV files (*.wav);;All files (*)");

    if (!file.isEmpty()) {
        ui->text->setText(file);
    }
}

void RadioPagingModGUI::on_clear_clicked()
{
    m_radioPagingMod->getInputMessageQueue()->push(RadioPagingMod::MsgClear::create(false));
    log("Queue cleared");
}

void RadioPagingModGUI::on_abort_clicked()
{
    m_radioPagingMod->getInputMessageQueue()->push(RadioPagingMod::MsgClear::create(true));
    log("Aborted");
}

void RadioPagingModGUI::log(const QString& text)
{
    ui->log->appendPlainText(QString("%1 %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(text));
}

void RadioPagingModGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList("rollupState"));
}

void RadioPagingModGUI::onMenuDialogCalled(const QPoint &p)
{
    if (m_contextMenuType == ContextMenuType::ContextMenuChannelSettings)
    {
        BasicChannelSettingsDialog dialog(&m_channelMarker, this);
        dialog.setUseReverseAPI(m_settings.m_useReverseAPI);
        dialog.setReverseAPIAddress(m_settings.m_reverseAPIAddress);
        dialog.setReverseAPIPort(m_settings.m_reverseAPIPort);
        dialog.setReverseAPIDeviceIndex(m_settings.m_reverseAPIDeviceIndex);
        dialog.setReverseAPIChannelIndex(m_settings.m_reverseAPIChannelIndex);
        dialog.setDefaultTitle(m_displayedName);

        if (m_deviceUISet->m_deviceMIMOEngine)
        {
            dialog.setNumberOfStreams(m_radioPagingMod->getNumberOfDeviceStreams());
            dialog.setStreamIndex(m_settings.m_streamIndex);
        }

        dialog.move(p);
        new DialogPositioner(&dialog, false);
        dialog.exec();

        m_settings.m_rgbColor = m_channelMarker.getColor().rgb();
        m_settings.m_title = m_channelMarker.getTitle();
        m_settings.m_useReverseAPI = dialog.useReverseAPI();
        m_settings.m_reverseAPIAddress = dialog.getReverseAPIAddress();
        m_settings.m_reverseAPIPort = dialog.getReverseAPIPort();
        m_settings.m_reverseAPIDeviceIndex = dialog.getReverseAPIDeviceIndex();
        m_settings.m_reverseAPIChannelIndex = dialog.getReverseAPIChannelIndex();

        setWindowTitle(m_settings.m_title);
        setTitle(m_channelMarker.getTitle());
        setTitleColor(m_settings.m_rgbColor);

        if (m_deviceUISet->m_deviceMIMOEngine)
        {
            m_settings.m_streamIndex = dialog.getSelectedStreamIndex();
            m_channelMarker.clearStreamIndexes();
            m_channelMarker.addStreamIndex(m_settings.m_streamIndex);
            updateIndexLabel();
        }

        applySettings(QStringList({"useReverseAPI", "reverseAPIAddress", "reverseAPIPort", "reverseAPIDeviceIndex", "reverseAPIChannelIndex", "title", "rgbColor", "streamIndex"}));
    }

    resetContextMenuType();
}

RadioPagingModGUI::RadioPagingModGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSource *channelTx, QWidget* parent) :
    ChannelGUI(parent),
    ui(new Ui::RadioPagingModGUI),
    m_pluginAPI(pluginAPI),
    m_deviceUISet(deviceUISet),
    m_channelMarker(this),
    m_deviceCenterFrequency(0),
    m_basebandSampleRate(1),
    m_doApplySettings(true),
    m_lastLoggedSent(0)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channeltx/modradiopaging/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    setSizePolicy(rollupContents->sizePolicy());
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_radioPagingMod = (RadioPagingMod*) channelTx;
    m_radioPagingMod->setMessageQueueToGUI(getInputMessageQueue());
    m_lastLoggedSent = m_radioPagingMod->getStatus().m_sent;   // Only what is sent from now on is logged

    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));

    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);

    m_channelMarker.blockSignals(true);
    m_channelMarker.setColor(m_settings.m_rgbColor);
    m_channelMarker.setBandwidth(channelBandwidth());
    m_channelMarker.setCenterFrequency(0);
    m_channelMarker.setTitle("Radio Paging Modulator");
    m_channelMarker.setSourceOrSinkStream(false);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(true); // activate signal on the last setting only

    m_deviceUISet->addChannelMarker(&m_channelMarker);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));

    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleSourceMessages()));
    m_radioPagingMod->setLevelMeter(ui->volumeMeter);

    m_settings.setChannelMarker(&m_channelMarker);
    m_settings.setRollupState(&m_rollupState);

    displayProtocolAvailability();
    displaySettings();
    makeUIConnections();
    applySettings(QStringList(), true);
    DialPopup::addPopupsToChildDials(this);
    m_resizer.enableChildMouseTracking();
}

RadioPagingModGUI::~RadioPagingModGUI()
{
    if (m_micRecording) {
        DSPEngine::instance()->getAudioDeviceManager()->removeAudioSource(&m_micFifo);
    }

    delete ui;
}

// GSC has no network mode (it is sent one-shot): the network item is not selectable for it
void RadioPagingModGUI::displayProtocolAvailability()
{
    setItemEnabled(ui->mode, (int) RadioPagingModSettings::Network, m_settings.m_protocol != RadioPagingModSettings::Gsc);
}

// Marker bandwidth: twice the deviation plus the symbol rate
int RadioPagingModGUI::channelBandwidth() const
{
    int rate = m_settings.m_pocsagSpeed;

    int bandwidth;

    if (m_settings.m_protocol == RadioPagingModSettings::Gsc) {
        rate = RadioPagingGscTxStream::m_baud;
    }

    if (m_settings.m_protocol == RadioPagingModSettings::Flex)
    {
        rate = RadioPagingFlex::baud((RadioPagingFlex::Mode) m_settings.m_flexMode);
        bandwidth = 2 * ((int) m_settings.m_deviation + rate);

        // Mixed with POCSAG: the wider of the two
        if (m_settings.m_flexMixPocsag && (m_settings.m_mode == RadioPagingModSettings::Network)) {
            bandwidth = std::max(bandwidth, 2 * ((int) m_settings.m_pocsagDeviation + m_settings.m_pocsagSpeed));
        }

        return bandwidth;
    }

    return 2 * ((int) m_settings.m_deviation + rate);
}

// The widgets of the protocol: speeds or modes, polarity of the protocol, the address of its
// messages. Signals are blocked while the combos are refilled
void RadioPagingModGUI::displayProtocol()
{
    bool flex = (m_settings.m_protocol == RadioPagingModSettings::Flex);
    QSignalBlocker speedBlocker(ui->speed);
    QSignalBlocker messageSpeedBlocker(ui->messageSpeed);
    QSignalBlocker polarityBlocker(ui->polarity);
    QSignalBlocker collapseBlocker(ui->collapse);
    QSignalBlocker roamingBlocker(ui->roaming);
    QSignalBlocker ssidZoneBlocker(ui->ssidZone);
    QSignalBlocker ssidLocalIdBlocker(ui->ssidLocalId);
    QSignalBlocker ssidCountryBlocker(ui->ssidCountry);
    QSignalBlocker ssidFlagsBlocker(ui->ssidFlags);
    QSignalBlocker voiceSecondsBlocker(ui->voiceSeconds);
    QSignalBlocker mixPocsagBlocker(ui->mixPocsag);
    QSignalBlocker mixSharedBlocker(ui->mixShared);
    QSignalBlocker mixSpeedBlocker(ui->mixSpeed);
    QSignalBlocker mixPolarityBlocker(ui->mixPolarity);
    QSignalBlocker mixDeviationBlocker(ui->mixDeviation);
    QSignalBlocker mixFramesBlocker(ui->mixFrames);
    QSignalBlocker mixSwitch512Blocker(ui->mixSwitch512);
    QSignalBlocker mixSwitch1200Blocker(ui->mixSwitch1200);
    QSignalBlocker mixSwitch2400Blocker(ui->mixSwitch2400);

    int messageSpeedIndex = ui->messageSpeed->currentIndex();

    ui->speed->clear();
    ui->messageSpeed->clear();
    ui->messageSpeed->addItem("Default");

    // The POCSAG speed of a message: a POCSAG channel, or a mixed FLEX channel
    for (int i = 0; i < RadioPagingModSettings::m_nbPocsagSpeeds; i++) {
        ui->messageSpeed->addItem(QString::number(RadioPagingModSettings::m_pocsagSpeeds[i]));
    }

    ui->messageSpeed->setCurrentIndex(std::max(0, messageSpeedIndex));
    ui->messageSpeed->setToolTip("POCSAG speed of this message; Default is the speed set for the channel");

    bool gsc = (m_settings.m_protocol == RadioPagingModSettings::Gsc);

    // The function of a message: the function A to D of POCSAG, the address number 1 to 4 of GSC
    {
        QSignalBlocker functionBlocker(ui->function);
        int functionIndex = std::max(0, ui->function->currentIndex());
        ui->function->clear();

        if (gsc) {
            ui->function->addItems(QStringList({"1", "2", "3", "4"}));
        } else {
            ui->function->addItems(QStringList({"A (0)", "B (1)", "C (2)", "D (3)"}));
        }

        ui->function->setCurrentIndex(functionIndex);
        ui->function->setToolTip(gsc ? "GSC address number 1 to 4" : "POCSAG function (sub address)");
    }

    if (gsc)
    {
        ui->speed->addItems(QStringList({"Auto", "Single", "Batch", "Non battery saver"}));
        ui->speedLabel->setText("Mode");
        ui->speed->setToolTip("GSC transmissions. Auto: a batch of the messages of the same preamble value, a message "
            "alone after its own preamble. Single: one message per transmission. Batch: a batch even for one message. "
            "Non battery saver: all queued messages of a polarity after one 1100 pattern");
        ui->speed->setCurrentIndex(m_settings.m_gscMode);
        ui->polarity->setCurrentIndex(m_settings.m_gscInverted ? 1 : 0);
        ui->polarity->setToolTip("Default polarity of the messages sent. Normal: binary 1 is a positive deviation");
    }
    else if (flex)
    {
        for (int mode = (int) RadioPagingFlex::Mode1600_2; mode <= (int) RadioPagingFlex::Mode6400_4; mode++) {
            ui->speed->addItem(RadioPagingFlex::modeName((RadioPagingFlex::Mode) mode));
        }

        ui->speedLabel->setText("Mode");
        ui->speed->setToolTip("FLEX mode (bit rate / levels) of the network: every frame is sent in it");
        ui->speed->setCurrentIndex(m_settings.m_flexMode);
        ui->polarity->setCurrentIndex(m_settings.m_flexInverted ? 1 : 0);
        ui->polarity->setToolTip("Polarity of the network. Normal: symbol 3 is the highest frequency. Messages of the "
            "other polarity are sent in the frames the network has nothing mandatory in");
    }
    else
    {
        for (int i = 0; i < RadioPagingModSettings::m_nbPocsagSpeeds; i++) {
            ui->speed->addItem(QString::number(RadioPagingModSettings::m_pocsagSpeeds[i]));
        }

        ui->speedLabel->setText("Speed");
        ui->speed->setToolTip("Default POCSAG speed (baud) of the messages sent");
        ui->speed->setCurrentIndex(pocsagSpeedIndex(m_settings.m_pocsagSpeed));
        ui->polarity->setCurrentIndex(m_settings.m_pocsagInverted ? 1 : 0);
        ui->polarity->setToolTip("Default polarity of the messages sent. Normal: binary 1 is a negative deviation");
    }

    ui->voiceSeconds->setValue(m_settings.m_gscVoiceSeconds);
    ui->channelAlias->setText(m_settings.m_channelAlias);
    ui->voiceSecondsLabel->setVisible(gsc);
    ui->voiceSeconds->setVisible(gsc);

    // The Voice message type is GSC's: its text is the path of a sound file
    {
        QSignalBlocker typeBlocker(ui->messageType);

        if (gsc && (ui->messageType->count() == 3)) {
            ui->messageType->addItem("Voice");
        } else if (!gsc && (ui->messageType->count() == 4))
        {
            if (ui->messageType->currentIndex() == 3) {
                ui->messageType->setCurrentIndex(0);
            }

            ui->messageType->removeItem(3);
        }
    }

    ui->roaming->setVisible(flex);
    ui->ssidZone->setVisible(flex);
    ui->ssidLocalId->setVisible(flex);
    ui->ssidCountry->setVisible(flex);
    ui->ssidFlags->setVisible(flex);
    ui->roaming->setChecked(m_settings.m_flexRoaming);
    ui->ssidZone->setValue(m_settings.m_ssidCoverageZone);
    ui->ssidLocalId->setValue(m_settings.m_ssidLocalId);
    ui->ssidCountry->setValue(m_settings.m_ssidCountryCode);
    ui->ssidFlags->setValue(m_settings.m_ssidTrafficFlags);

    for (QWidget *w : QList<QWidget*>({ui->ssidZone, ui->ssidLocalId, ui->ssidCountry, ui->ssidFlags})) {
        w->setEnabled(m_settings.m_flexRoaming);
    }

    ui->collapse->setValue(m_settings.m_flexCollapse);
    // Collapse: the re-synchronization length; network: also the schedule of the frames
    bool network = (m_settings.m_mode == RadioPagingModSettings::Network);
    ui->collapseLabel->setVisible(flex);
    ui->collapse->setVisible(flex);
    ui->collapse->setToolTip(network
        ? "FLEX collapse: pagers listen to one frame in 2^collapse; a message waits for a frame of its capcode. "
          "The re-synchronization at the start of the network lasts at least 2^collapse frames"
        : "FLEX collapse of the pagers to reach: the re-synchronization lasts at least 2^collapse frames. "
          "The frames after it are sent with collapse 0: the message goes in the first frame");
    // Mixing with POCSAG: a FLEX network option
    bool mixOptions = flex && network;
    ui->mixPocsag->setVisible(mixOptions);
    ui->mixShared->setVisible(mixOptions);
    ui->mixSpeedLabel->setVisible(mixOptions);
    ui->mixSpeed->setVisible(mixOptions);
    ui->mixPolarity->setVisible(mixOptions);
    ui->mixDeviation->setVisible(mixOptions);
    ui->mixFramesLabel->setVisible(mixOptions);
    ui->mixFrames->setVisible(mixOptions);
    ui->mixSwitchLabel->setVisible(mixOptions);
    ui->mixSwitch512->setVisible(mixOptions);
    ui->mixSwitch1200->setVisible(mixOptions);
    ui->mixSwitch2400->setVisible(mixOptions);
    ui->mixPocsag->setChecked(m_settings.m_flexMixPocsag);
    ui->mixShared->setChecked(m_settings.m_mixShared);
    ui->mixSpeed->setCurrentIndex(pocsagSpeedIndex(m_settings.m_pocsagSpeed));
    ui->mixPolarity->setCurrentIndex(m_settings.m_pocsagInverted ? 1 : 0);
    ui->mixDeviation->setValue((int) m_settings.m_pocsagDeviation);
    ui->mixFrames->setMaximum(m_settings.m_mixShared ? 128 : 32);
    ui->mixFrames->setValue(m_settings.m_mixFramesPerMinute);
    ui->mixSwitch512->setValue(m_settings.m_mixSwitchMs[0]);
    ui->mixSwitch1200->setValue(m_settings.m_mixSwitchMs[1]);
    ui->mixSwitch2400->setValue(m_settings.m_mixSwitchMs[2]);

    for (QWidget *w : QList<QWidget*>({ui->mixShared, ui->mixSpeed, ui->mixPolarity, ui->mixDeviation, ui->mixFrames,
        ui->mixSwitch512, ui->mixSwitch1200, ui->mixSwitch2400})) {
        w->setEnabled(m_settings.m_flexMixPocsag);
    }

    // The message section: the protocol selector with the mixing option, the fields of the protocol
    bool mixActive = mixOptions && m_settings.m_flexMixPocsag;
    ui->messageProtocolLabel->setVisible(mixActive);
    ui->messageProtocol->setVisible(mixActive);

    if (!mixActive) {
        QSignalBlocker blocker(ui->messageProtocol);
        ui->messageProtocol->setCurrentIndex(0);
    }

    // POCSAG: address, function and speed; FLEX: capcode and priority (the mode is the network's);
    // GSC: the address of 6 digits (as the capcode) and the address number
    bool flexMessage = messageIsFlex();
    ui->messageSpeed->setVisible(!flexMessage && !gsc);
    ui->address->setVisible(!flexMessage && !gsc);

    // GSC has no network mode: the combo shows one-shot, the setting is kept for the other protocols
    {
        QSignalBlocker modeBlocker(ui->mode);
        ui->mode->setCurrentIndex(gsc ? (int) RadioPagingModSettings::OneShot : (int) m_settings.m_mode);
    }

    ui->function->setVisible(!flexMessage);
    ui->capcode->setVisible(flexMessage || gsc);
    ui->priority->setVisible(flexMessage);
    ui->addressLabel->setText(gsc ? "Address" : flexMessage ? "Capcode" : "Address");
    ui->capcode->setToolTip(gsc ? "GSC address: 6 digits (index, group, 3 address digits)"
        : "FLEX capcode: short 1 to 1933312, long 2101249 to 4297068542");
    displayProtocolAvailability();
    displayMessageType();
}

// The message composed is FLEX: a FLEX channel, unless it mixes POCSAG and the message is POCSAG
bool RadioPagingModGUI::messageIsFlex() const
{
    if (m_settings.m_protocol != RadioPagingModSettings::Flex) {
        return false;
    }

    bool mixActive = (m_settings.m_mode == RadioPagingModSettings::Network) && m_settings.m_flexMixPocsag;
    return !(mixActive && (ui->messageProtocol->currentIndex() == 1));
}

void RadioPagingModGUI::on_mixPocsag_toggled(bool checked)
{
    m_settings.m_flexMixPocsag = checked;
    m_channelMarker.setBandwidth(channelBandwidth());
    blockApplySettings(true);
    displayProtocol();
    blockApplySettings(false);
    applySettings(QStringList("mixPocsag"));
}

void RadioPagingModGUI::on_mixShared_toggled(bool checked)
{
    m_settings.m_mixShared = checked;
    m_settings.m_mixFramesPerMinute = std::min(m_settings.m_mixFramesPerMinute, checked ? 128 : 32);
    blockApplySettings(true);
    displayProtocol();
    blockApplySettings(false);
    applySettings(QStringList({"mixShared", "mixFramesPerMinute"}));
}

void RadioPagingModGUI::on_mixSpeed_currentIndexChanged(int index)
{
    if ((index >= 0) && (index < RadioPagingModSettings::m_nbPocsagSpeeds))
    {
        m_settings.m_pocsagSpeed = RadioPagingModSettings::m_pocsagSpeeds[index];
        m_channelMarker.setBandwidth(channelBandwidth());
        applySettings(QStringList("pocsagSpeed"));
    }
}

void RadioPagingModGUI::on_mixPolarity_currentIndexChanged(int index)
{
    m_settings.m_pocsagInverted = (index == 1);
    applySettings(QStringList("pocsagInverted"));
}

void RadioPagingModGUI::on_mixDeviation_valueChanged(int value)
{
    m_settings.m_pocsagDeviation = value;
    m_channelMarker.setBandwidth(channelBandwidth());
    applySettings(QStringList("pocsagDeviation"));
}

void RadioPagingModGUI::on_mixFrames_valueChanged(int value)
{
    m_settings.m_mixFramesPerMinute = value;
    applySettings(QStringList("mixFramesPerMinute"));
}

void RadioPagingModGUI::on_mixSwitch512_valueChanged(int value)
{
    m_settings.m_mixSwitchMs[0] = value;
    applySettings(QStringList("mixSwitchMs512"));
}

void RadioPagingModGUI::on_mixSwitch1200_valueChanged(int value)
{
    m_settings.m_mixSwitchMs[1] = value;
    applySettings(QStringList("mixSwitchMs1200"));
}

void RadioPagingModGUI::on_mixSwitch2400_valueChanged(int value)
{
    m_settings.m_mixSwitchMs[2] = value;
    applySettings(QStringList("mixSwitchMs2400"));
}

void RadioPagingModGUI::on_messageProtocol_currentIndexChanged(int index)
{
    (void) index;
    blockApplySettings(true);
    displayProtocol();
    blockApplySettings(false);
}

void RadioPagingModGUI::blockApplySettings(bool block)
{
    m_doApplySettings = !block;
}

void RadioPagingModGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    if (m_doApplySettings)
    {
        RadioPagingMod::MsgConfigureRadioPagingMod *msg = RadioPagingMod::MsgConfigureRadioPagingMod::create(settingsKeys, m_settings, force);
        m_radioPagingMod->getInputMessageQueue()->push(msg);
    }
}

void RadioPagingModGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle(m_settings.m_title);
    m_channelMarker.setBandwidth(channelBandwidth());
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(m_settings.m_rgbColor); // activate signal on the last setting only

    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_channelMarker.getTitle());
    setTitle(m_channelMarker.getTitle());
    updateIndexLabel();

    blockApplySettings(true);

    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    ui->channelMute->setChecked(m_settings.m_channelMute);
    ui->protocol->setCurrentIndex((int) m_settings.m_protocol);
    ui->mode->setCurrentIndex((int) m_settings.m_mode);
    ui->deviation->setValue((int) m_settings.m_deviation);
    ui->gain->setValue((int) m_settings.m_gain);
    ui->gainText->setText(QString("%1dB").arg((int) m_settings.m_gain));
    displayProtocol();
    ui->rampUp->setValue(m_settings.m_rampUpMs);
    ui->rampDown->setValue(m_settings.m_rampDownMs);
    ui->rampRange->setValue(m_settings.m_rampRange);

    getRollupContents()->restoreState(m_rollupState);
    updateAbsoluteCenterFrequency();
    blockApplySettings(false);
}

void RadioPagingModGUI::leaveEvent(QEvent* event)
{
    m_channelMarker.setHighlighted(false);
    ChannelGUI::leaveEvent(event);
}

void RadioPagingModGUI::enterEvent(EnterEventType* event)
{
    m_channelMarker.setHighlighted(true);
    ChannelGUI::enterEvent(event);
}

void RadioPagingModGUI::tick()
{
    double powDb = CalcDb::dbPower(m_radioPagingMod->getMagSq());
    m_channelPowerDbAvg(powDb);
    ui->channelPower->setText(tr("%1 dB").arg(m_channelPowerDbAvg.asDouble(), 0, 'f', 1));

    RadioPagingModSource::Status status = m_radioPagingMod->getStatus();
    QString text = stateName(status.m_state);

    if (status.m_ers) {
        text += QString(" ERS %1 s").arg(status.m_ersMs / 1000.0, 0, 'f', 3);
    }
    if (status.m_currentId != 0) {
        text += QString(" #%1").arg(status.m_currentId);
    }

    text += QString(", %1 waiting, %2 sent").arg(status.m_queued).arg(status.m_sent);
    ui->status->setText(text);
    updateLive(status);

    // Several transmissions may end between two ticks
    if (status.m_sent > m_lastLoggedSent)
    {
        quint64 count = status.m_sent - m_lastLoggedSent;

        if (count == 1) {
            log(QString("#%1 sent").arg(status.m_lastSentId));
        } else {
            log(QString("%1 sent, the last #%2").arg(count).arg(status.m_lastSentId));
        }

        m_lastLoggedSent = status.m_sent;
    }
}

void RadioPagingModGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &RadioPagingModGUI::on_deltaFrequency_changed);
    QObject::connect(ui->channelMute, &QToolButton::toggled, this, &RadioPagingModGUI::on_channelMute_toggled);
    QObject::connect(ui->protocol, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_protocol_currentIndexChanged);
    QObject::connect(ui->mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_mode_currentIndexChanged);
    QObject::connect(ui->deviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_deviation_valueChanged);
    QObject::connect(ui->gain, &QDial::valueChanged, this, &RadioPagingModGUI::on_gain_valueChanged);
    QObject::connect(ui->speed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_speed_currentIndexChanged);
    QObject::connect(ui->polarity, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_polarity_currentIndexChanged);
    QObject::connect(ui->rampUp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &RadioPagingModGUI::on_rampUp_valueChanged);
    QObject::connect(ui->rampDown, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &RadioPagingModGUI::on_rampDown_valueChanged);
    QObject::connect(ui->rampRange, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_rampRange_valueChanged);
    QObject::connect(ui->collapse, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_collapse_valueChanged);
    QObject::connect(ui->roaming, &QCheckBox::toggled, this, &RadioPagingModGUI::on_roaming_toggled);
    QObject::connect(ui->ssidZone, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_ssidZone_valueChanged);
    QObject::connect(ui->ssidLocalId, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_ssidLocalId_valueChanged);
    QObject::connect(ui->ssidCountry, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_ssidCountry_valueChanged);
    QObject::connect(ui->ssidFlags, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_ssidFlags_valueChanged);
    QObject::connect(ui->voiceSeconds, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_voiceSeconds_valueChanged);
    QObject::connect(ui->mixPocsag, &QCheckBox::toggled, this, &RadioPagingModGUI::on_mixPocsag_toggled);
    QObject::connect(ui->mixShared, &QCheckBox::toggled, this, &RadioPagingModGUI::on_mixShared_toggled);
    QObject::connect(ui->mixSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_mixSpeed_currentIndexChanged);
    QObject::connect(ui->mixPolarity, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_mixPolarity_currentIndexChanged);
    QObject::connect(ui->mixDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_mixDeviation_valueChanged);
    QObject::connect(ui->mixFrames, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_mixFrames_valueChanged);
    QObject::connect(ui->mixSwitch512, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_mixSwitch512_valueChanged);
    QObject::connect(ui->mixSwitch1200, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_mixSwitch1200_valueChanged);
    QObject::connect(ui->mixSwitch2400, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingModGUI::on_mixSwitch2400_valueChanged);
    QObject::connect(ui->messageProtocol, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_messageProtocol_currentIndexChanged);
    QObject::connect(ui->send, &QPushButton::clicked, this, &RadioPagingModGUI::on_send_clicked);
    QObject::connect(ui->text, &QLineEdit::returnPressed, this, &RadioPagingModGUI::on_text_returnPressed);
    QObject::connect(ui->micInput, &QToolButton::clicked, this, &RadioPagingModGUI::on_micInput_clicked);
    QObject::connect(ui->voiceSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_voiceSource_currentIndexChanged);
    QObject::connect(&m_micFifo, &AudioFifo::dataReady, this, &RadioPagingModGUI::micDataReady);
    QObject::connect(ui->channelAlias, &QLineEdit::editingFinished, this, &RadioPagingModGUI::on_channelAlias_editingFinished);
    QObject::connect(ui->voiceFiles, &QToolButton::clicked, this, &RadioPagingModGUI::on_voiceFiles_clicked);
    QObject::connect(ui->txLog, &QTableWidget::cellDoubleClicked, this, &RadioPagingModGUI::on_txLog_cellDoubleClicked);
    m_browseAction = ui->text->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), QLineEdit::TrailingPosition);
    m_browseAction->setToolTip("Choose the sound file");
    m_browseAction->setVisible(false);
    QObject::connect(m_browseAction, &QAction::triggered, this, &RadioPagingModGUI::on_browse_triggered);
    QObject::connect(ui->messageType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingModGUI::on_messageType_currentIndexChanged);
    QObject::connect(ui->clear, &QPushButton::clicked, this, &RadioPagingModGUI::on_clear_clicked);
    QObject::connect(ui->abort, &QPushButton::clicked, this, &RadioPagingModGUI::on_abort_clicked);
}

void RadioPagingModGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
