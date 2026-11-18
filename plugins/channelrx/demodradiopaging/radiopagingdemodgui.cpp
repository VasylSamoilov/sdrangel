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
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QPainter>
#include <QRegularExpression>
#include <QFontDatabase>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "radiopagingdemodgui.h"

#include "device/deviceuiset.h"
#include "dsp/dspcommands.h"
#include "ui_radiopagingdemodgui.h"
#include "plugin/pluginapi.h"
#include "util/db.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "gui/crightclickenabler.h"
#include "gui/audioselectdialog.h"
#include "gui/pagingcomposer.h"
#include "dsp/dspengine.h"
#include "dsp/scopevis.h"
#include "maincore.h"
#include "util/csv.h"
#include "util/units.h"
#include "gui/messagedialog.h"
#include "SWGMapItem.h"

#include "radiopagingdemod.h"
#include "radiopagingdemodnotificationdialog.h"
#include "radiopagingpagerformat.h"
#include "radiopagingvoicestore.h"

namespace {

// The fields of a status line, in characters of the fixed-pitch font
const int statusStateWidth = 5;     // VOICE
const int statusPolarityWidth = 4;  // NORM, INV
const int statusDetailWidth = 16;   // 512 1200 2400
const int statusQualityWidth = 10;  // BER 12.50%
const int statusOffsetWidth = 12;   // Df -12500 Hz
const int statusMessagesWidth = 8;  // (123456)
const int statusSpacing = 1;
const int statusNetworkWidth = 57;  // FLEX line 3: 4 phases A0.12%, LID 511 CZ 31, CC 1023 TMF 15
const int statusLines = 2;          // Of each polarity: the status and the memory
const int statusFlexLines = 4;      // FLEX: the status, the network of the last frame (2 lines) and the memory

} // namespace

const int RadioPagingDemodGUI::m_maxRows;

RadioPagingDemodGUI* RadioPagingDemodGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel)
{
    RadioPagingDemodGUI* gui = new RadioPagingDemodGUI(pluginAPI, deviceUISet, rxChannel);
    return gui;
}

void RadioPagingDemodGUI::destroy()
{
    delete this;
}

void RadioPagingDemodGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(QStringList(), true);
}

QByteArray RadioPagingDemodGUI::serialize() const
{
    return m_settings.serialize();
}

bool RadioPagingDemodGUI::deserialize(const QByteArray& data)
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

bool RadioPagingDemodGUI::handleMessage(const Message& message)
{
    if (RadioPagingDemod::MsgConfigureRadioPagingDemod::match(message))
    {
        qDebug("RadioPagingDemodGUI::handleMessage: RadioPagingDemod::MsgConfigureRadioPagingDemod");
        const RadioPagingDemod::MsgConfigureRadioPagingDemod& cfg = (const RadioPagingDemod::MsgConfigureRadioPagingDemod&) message;
        m_settings = cfg.getSettings();
        blockApplySettings(true);
        ui->scopeGUI->updateSettings();
        m_channelMarker.updateSettings(static_cast<const ChannelMarker*>(m_settings.m_channelMarker));
        displaySettings();
        blockApplySettings(false);
        return true;
    }
    else if (MainCore::MsgMessageRecord::match(message))
    {
        const MainCore::MsgMessageRecord& msg = (const MainCore::MsgMessageRecord&) message;
        showRecord(msg.getRecord());
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

    return false;
}

void RadioPagingDemodGUI::handleInputMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop()))
    {
        if (!handleMessage(*message)) {
            qDebug("%s: unhandled message: %s", Q_FUNC_INFO, message->getIdentifier());
        }
        delete message;
    }
}

void RadioPagingDemodGUI::channelMarkerChangedByCursor()
{
    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void RadioPagingDemodGUI::channelMarkerHighlightedByCursor()
{
    setHighlighted(m_channelMarker.getHighlighted());
}

void RadioPagingDemodGUI::on_deltaFrequency_changed(qint64 value)
{
    m_channelMarker.setCenterFrequency(value);
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    updateAbsoluteCenterFrequency();
    applySettings(QStringList("inputFrequencyOffset"));
}

void RadioPagingDemodGUI::on_rfBW_valueChanged(int value)
{
    float bw = value * 100.0f;
    ui->rfBWText->setText(QString("%1k").arg(value / 10.0, 0, 'f', 1));
    m_channelMarker.setBandwidth(bw);
    m_settings.m_rfBandwidth = bw;
    applySettings(QStringList("rfBandwidth"));
}

void RadioPagingDemodGUI::on_pocsagDeviation_valueChanged(int value)
{
    m_settings.m_pocsagDeviation = value;
    applySettings(QStringList("pocsagDeviation"));
}

void RadioPagingDemodGUI::on_flexMaxMessageNumber_valueChanged(int value)
{
    m_settings.m_flexMaxMessageNumber = value;
    applySettings(QStringList("flexMaxMessageNumber"));
}

void RadioPagingDemodGUI::on_flexDeviation_valueChanged(int value)
{
    m_settings.m_flexDeviation = value;
    applySettings(QStringList("flexDeviation"));
}

void RadioPagingDemodGUI::on_pocsagEnabled_clicked(bool checked)
{
    m_settings.m_pocsagEnabled = checked;
    applySettings(QStringList("pocsagEnabled"));
}

void RadioPagingDemodGUI::on_flexEnabled_clicked(bool checked)
{
    m_settings.m_flexEnabled = checked;
    applySettings(QStringList("flexEnabled"));
}

void RadioPagingDemodGUI::on_gscEnabled_clicked(bool checked)
{
    m_settings.m_gscEnabled = checked;
    applySettings(QStringList("gscEnabled"));
}

void RadioPagingDemodGUI::on_gscVoiceTimeout_valueChanged(int value)
{
    m_settings.m_gscVoiceTimeout = value;
    applySettings(QStringList("gscVoiceTimeout"));
}

void RadioPagingDemodGUI::on_gscDeviation_valueChanged(int value)
{
    m_settings.m_gscDeviation = value;
    applySettings(QStringList("gscDeviation"));
}

void RadioPagingDemodGUI::on_gscAudio_toggled(bool checked)
{
    m_settings.m_gscAudio = checked;
    applySettings(QStringList("gscAudio"));
}

void RadioPagingDemodGUI::on_gscVoiceRecord_toggled(bool checked)
{
    m_settings.m_gscVoiceRecord = checked;
    applySettings(QStringList("gscVoiceRecord"));
}

void RadioPagingDemodGUI::on_channelAlias_editingFinished()
{
    m_settings.m_channelAlias = ui->channelAlias->text().trimmed();
    applySettings(QStringList("channelAlias"));
}

namespace {

QString sizeText(qint64 bytes)
{
    return RadioPagingVoiceStore::sizeText(bytes);
}

} // namespace

// The voice files: the statistics of the directory, the files kept, the deletion of the old ones
void RadioPagingDemodGUI::on_gscVoiceFiles_clicked()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Voice files");
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
    days->setToolTip("Files older than this are deleted, a few at a time, when a new voice file is written.\n"
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

    auto refresh = [this, &dialog, days, minimum, stats, clean]()
    {
        RadioPagingVoiceStore::Stats s = RadioPagingVoiceStore::stats(m_settings.voiceDirectory(), days->value(), minimum->value());
        stats->setText(QString("Directory: %1\nFiles: %2, %3\nOldest: %4\nNewest: %5\nTo delete now: %6 files, %7\nFree space on the disk: %8")
            .arg(m_settings.voiceDirectory())
            .arg(s.m_files).arg(sizeText(s.m_bytes))
            .arg(s.m_oldest.isValid() ? s.m_oldest.toString("yyyy-MM-dd HH:mm") : QString("-"))
            .arg(s.m_newest.isValid() ? s.m_newest.toString("yyyy-MM-dd HH:mm") : QString("-"))
            .arg(s.m_expired).arg(sizeText(s.m_expiredBytes))
            .arg(sizeText(s.m_freeBytes)));
        clean->setEnabled(s.m_expired > 0);
        (void) dialog;
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
            QString("Delete %1 voice files (%2) from\n%3 ?\nThis cannot be undone.").arg(s.m_expired).arg(sizeText(s.m_expiredBytes))
            .arg(m_settings.voiceDirectory())) == QMessageBox::Yes)
        {
            RadioPagingVoiceStore::cleanup(m_settings.voiceDirectory(), m_settings.m_gscVoiceKeepDays, m_settings.m_gscVoiceKeepMin, -1);
        }

        refresh();
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    refresh();
    dialog.exec();
}

void RadioPagingDemodGUI::voiceDirectorySelect(const QPoint& p)
{
    (void) p;
    QString directory = QFileDialog::getExistingDirectory(this, "Directory of the voice files", m_settings.voiceDirectory());

    if (!directory.isEmpty())
    {
        m_settings.m_gscVoiceDirectory = directory;
        displayVoiceRecordToolTip();
        applySettings(QStringList("gscVoiceDirectory"));
    }
}

void RadioPagingDemodGUI::displayVoiceRecordToolTip()
{
    ui->gscVoiceRecord->setToolTip(QString("Left: record the voice message of GSC voice pages to a WAV file;\n"
        "its path is in the message record (voice_file).\n"
        "Right: select the directory of the files, used from the next voice message. Now: %1")
        .arg(m_settings.voiceDirectory()));
}

void RadioPagingDemodGUI::on_volume_valueChanged(int value)
{
    ui->volumeText->setText(QString::number(value));
    m_settings.m_volume = value / 100.0f;
    applySettings(QStringList("volume"));
}

void RadioPagingDemodGUI::audioSelect(const QPoint& p)
{
    AudioSelectDialog audioSelect(DSPEngine::instance()->getAudioDeviceManager(), m_settings.m_audioDeviceName);
    audioSelect.move(p);
    new DialogPositioner(&audioSelect, false);
    audioSelect.exec();

    if (audioSelect.m_selected)
    {
        m_settings.m_audioDeviceName = audioSelect.m_audioDeviceName;
        ui->messages->setAudioDevice(m_settings.m_audioDeviceName);
        applySettings(QStringList("audioDeviceName"));
    }
}

void RadioPagingDemodGUI::on_clearTable_clicked()
{
    ui->messages->setRowCount(0);
    clearFromMap();
}

void RadioPagingDemodGUI::on_udpEnabled_clicked(bool checked)
{
    m_settings.m_udpEnabled = checked;
    applySettings(QStringList("udpEnabled"));
}

void RadioPagingDemodGUI::on_udpAddress_editingFinished()
{
    m_settings.m_udpAddress = ui->udpAddress->text();
    applySettings(QStringList("udpAddress"));
}

void RadioPagingDemodGUI::on_udpPort_valueChanged(int value)
{
    m_settings.m_udpPort = value;
    applySettings(QStringList("udpPort"));
}

void RadioPagingDemodGUI::on_udpFormat_currentIndexChanged(int index)
{
    m_settings.m_udpFormat = index;
    applySettings(QStringList("udpFormat"));
}

void RadioPagingDemodGUI::on_channel1_currentIndexChanged(int index)
{
    m_settings.m_scopeCh1 = index;
    applySettings(QStringList("scopeCh1"));
}

void RadioPagingDemodGUI::on_channel2_currentIndexChanged(int index)
{
    m_settings.m_scopeCh2 = index;
    applySettings(QStringList("scopeCh2"));
}

void RadioPagingDemodGUI::on_notifications_clicked()
{
    RadioPagingDemodNotificationDialog dialog(&m_settings);
    new DialogPositioner(&dialog, true);

    if (dialog.exec() == QDialog::Accepted)
    {
        enableSpeechIfNeeded();
        applySettings(QStringList("notificationSettings"));
    }
}

void RadioPagingDemodGUI::on_logEnable_clicked(bool checked)
{
    m_settings.m_logEnabled = checked;
    applySettings(QStringList("logEnabled"));
}

void RadioPagingDemodGUI::on_logFilename_clicked()
{
    QFileDialog fileDialog(nullptr, tr("Select the file to log the messages received to"), "", "*.csv");
    fileDialog.setDefaultSuffix("csv");
    fileDialog.setAcceptMode(QFileDialog::AcceptSave);

    if (fileDialog.exec())
    {
        QStringList fileNames = fileDialog.selectedFiles();

        if (fileNames.size() > 0)
        {
            m_settings.m_logFilename = fileNames[0];
            ui->logFilename->setToolTip(tr(".csv log file: %1").arg(m_settings.m_logFilename));
            applySettings(QStringList("logFilename"));
        }
    }
}

// The messages of a log written by this demodulator, shown in the table. They are not notified again: they were
// when they were received, and running the commands of a long log again could be a lot
void RadioPagingDemodGUI::on_logOpen_clicked()
{
    QFileDialog fileDialog(nullptr, tr("Select the .csv log file to read"), "", "*.csv");

    if (!fileDialog.exec()) {
        return;
    }

    QStringList fileNames = fileDialog.selectedFiles();

    if (fileNames.isEmpty()) {
        return;
    }

    QFile file(fileNames[0]);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        MessageDialog::critical(this, "Radio Paging Demod", tr("Failed to open file %1").arg(fileNames[0]));
        return;
    }

    QTextStream in(&file);
    QString error;
    QHash<QString, int> columns = CSV::readHeader(in, {"id", "timestamp", "protocol", "kind"}, error);

    if (!error.isEmpty())
    {
        MessageDialog::critical(this, "Radio Paging Demod", error);
        return;
    }

    QMessageBox dialog(this);
    dialog.setText(tr("Reading messages"));
    dialog.addButton(QMessageBox::Cancel);
    dialog.show();
    QApplication::processEvents();

    int count = 0;
    bool cancelled = false;
    QStringList row;
    m_loadingData = true;

    while (!cancelled && CSV::readRow(in, &row))
    {
        MessageRecord record;

        if (MessageRecord::fromCSV(columns, row, record) && (record.m_kind == MessageRecord::KindMessage)) {
            showRecord(record);
        }

        if ((++count % 500) == 0)
        {
            QApplication::processEvents();
            cancelled = (dialog.clickedButton() != nullptr);
        }
    }

    m_loadingData = false;
    dialog.close();
}

bool RadioPagingDemodGUI::addressMatchesFilter(const QString& address) const
{
    QString pattern = ui->filterAddress->text().trimmed();

    if (pattern.isEmpty()) {
        return true;
    }

    // As the address filter of the Pager demodulator: the whole address must match
    QRegularExpression regExp(QRegularExpression::anchoredPattern(pattern));
    return !regExp.isValid() || regExp.match(address).hasMatch();
}

void RadioPagingDemodGUI::applyAddressFilter()
{
    for (int row = 0; row < ui->messages->rowCount(); row++)
    {
        QTableWidgetItem *item = ui->messages->item(row, PagingMessageTable::ColAddress);
        ui->messages->setRowHidden(row, item && !addressMatchesFilter(item->text()));
    }
}

void RadioPagingDemodGUI::on_filterAddress_editingFinished()
{
    // An expression that is not valid filters nothing, shown in red with its error
    QRegularExpression regExp(QRegularExpression::anchoredPattern(ui->filterAddress->text().trimmed()));
    bool valid = ui->filterAddress->text().trimmed().isEmpty() || regExp.isValid();
    ui->filterAddress->setStyleSheet(valid ? QString() : QString("QLineEdit { color: red; }"));
    ui->filterAddress->setToolTip(valid ? tr("Show only the messages whose whole address matches this regular expression,\n"
        "e.g. 1234567A, 12345.., 001001/. Empty: all") : regExp.errorString());
    applyAddressFilter();
}

// A message goes in the table; a new one is given to the notifications too
void RadioPagingDemodGUI::showRecord(const MessageRecord& record)
{
    ui->messages->addRecord(record);

    int row = ui->messages->findRow(record.m_id);

    if ((row >= 0) && !addressMatchesFilter(record.m_address)) {
        ui->messages->setRowHidden(row, true);
    }

    if (!m_loadingData) {
        checkNotifications(record);
    }
}

QString RadioPagingDemodGUI::matchText(const MessageRecord& record, int field)
{
    typedef RadioPagingDemodSettings::NotificationSettings Notification;

    switch (field)
    {
    case Notification::MatchAddress:
        return record.m_address;
    case Notification::MatchMessage:
        return record.m_text;
    case Notification::MatchProtocol:
        return record.m_protocol;
    case Notification::MatchType:
        return record.m_msgType;
    case Notification::MatchChannel:
        return record.m_channelAlias;
    case Notification::MatchAny:
        return QStringList({record.m_address, record.m_text, record.m_protocol, record.m_msgType, record.m_channelAlias}).join(' ');
    default:
        return QString();
    }
}

// Only a message received for the first time is notified: a duplicate was, and the end of a voice message is not one
void RadioPagingDemodGUI::checkNotifications(const MessageRecord& record)
{
    if ((record.m_kind != MessageRecord::KindMessage) || (record.m_duplicate != MessageRecord::DuplicateNone)
        || RadioPagingPagerFormat::isCompletion(record)) {
        return;
    }

    for (const auto& notification : m_settings.m_notificationSettings)
    {
        QString match = matchText(record, notification->m_matchField);

        if (match.isEmpty() || !notification->m_regularExpression.isValid()) {
            continue;
        }

        QRegularExpressionMatch matchResult = notification->m_regularExpression.match(match);

        if (!matchResult.hasMatch()) {
            continue;
        }

        if (notification->m_highlight)
        {
            QColor color = QColor::fromRgba(notification->m_highlightColor);
            color.setAlpha(110);
            ui->messages->setRowBackground(record.m_id, color);
        }
        if (!notification->m_speech.isEmpty()) {
            speechNotification(subStrings(record, matchResult, notification->m_speech));
        }
        if (!notification->m_command.isEmpty()) {
            commandNotification(subStrings(record, matchResult, notification->m_command));
        }
        if (notification->m_plotOnMap)
        {
            float latitude;
            float longitude;

            if (Units::stringToLatitudeAndLongitude(record.m_text, latitude, longitude, false)) {
                sendToMap(record, latitude, longitude);
            }
        }
    }
}

QString RadioPagingDemodGUI::subStrings(const MessageRecord& record, const QRegularExpressionMatch& match, const QString& string)
{
    QString s = string;

    s.replace("${address}", record.m_address);
    s.replace("${message}", record.m_text);
    s.replace("${protocol}", record.m_protocol);
    s.replace("${type}", record.m_msgType);
    s.replace("${channel}", record.m_channelAlias);

    for (int i = 0; i < match.capturedTexts().size(); i++) {
        s.replace(QString("${%1}").arg(i), match.capturedTexts()[i]);
    }

    return s;
}

// The text to speech engine takes 10 seconds to start on some versions of Linux: only started if a
// notification speaks
void RadioPagingDemodGUI::enableSpeechIfNeeded()
{
#ifdef QT_TEXTTOSPEECH_FOUND
    if (m_speech) {
        return;
    }

    for (const auto& notification : m_settings.m_notificationSettings)
    {
        if (!notification->m_speech.isEmpty())
        {
            qDebug() << "RadioPagingDemodGUI: enabling text to speech";
            m_speech = new QTextToSpeech(this);
            return;
        }
    }
#endif
}

void RadioPagingDemodGUI::speechNotification(const QString& speech)
{
#ifdef QT_TEXTTOSPEECH_FOUND
    if (m_speech) {
        m_speech->say(speech);
    } else {
        qWarning() << "RadioPagingDemodGUI::speechNotification: unable to say" << speech;
    }
#else
    qWarning() << "RadioPagingDemodGUI::speechNotification: text to speech not supported, unable to say" << speech;
#endif
}

void RadioPagingDemodGUI::commandNotification(const QString& command)
{
#if QT_CONFIG(process)
    QStringList allArgs = QProcess::splitCommand(command);

    if (allArgs.size() > 0)
    {
        QString program = allArgs[0];
        allArgs.pop_front();
        QProcess::startDetached(program, allArgs);
    }
#else
    qWarning() << "RadioPagingDemodGUI::commandNotification: QProcess not supported, can't run" << command;
#endif
}

void RadioPagingDemodGUI::sendToMap(const MessageRecord& record, float latitude, float longitude)
{
    QList<ObjectPipe*> mapPipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(m_radioPagingDemod, "mapitems", mapPipes);
    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(record.m_timestamp);

    for (const auto& pipe : mapPipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
        SWGSDRangel::SWGMapItem *swgMapItem = new SWGSDRangel::SWGMapItem();
        swgMapItem->setName(new QString(record.m_address));
        swgMapItem->setLatitude(latitude);
        swgMapItem->setLongitude(longitude);
        swgMapItem->setAltitude(0);
        swgMapItem->setAltitudeReference(1); // CLAMP_TO_GROUND
        swgMapItem->setFixedPosition(false);
        swgMapItem->setPositionDateTime(new QString(dateTime.toUTC().toString(Qt::ISODateWithMs)));
        swgMapItem->setImageRotation(0);
        swgMapItem->setText(new QString(record.m_text));
        swgMapItem->setImage(new QString("pager.png"));

        MainCore::MsgMapItem *msg = MainCore::MsgMapItem::create(m_radioPagingDemod, swgMapItem);
        messageQueue->push(msg);
    }

    m_mapItems.insert(record.m_address);
}

void RadioPagingDemodGUI::clearFromMap()
{
    for (const auto& address : m_mapItems)
    {
        QList<ObjectPipe*> mapPipes;
        MainCore::instance()->getMessagePipes().getMessagePipes(m_radioPagingDemod, "mapitems", mapPipes);

        for (const auto& pipe : mapPipes)
        {
            MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);
            SWGSDRangel::SWGMapItem *swgMapItem = new SWGSDRangel::SWGMapItem();
            swgMapItem->setName(new QString(address));
            swgMapItem->setImage(new QString(""));

            MainCore::MsgMapItem *msg = MainCore::MsgMapItem::create(m_radioPagingDemod, swgMapItem);
            messageQueue->push(msg);
        }
    }

    m_mapItems.clear();
}

void RadioPagingDemodGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    // The waveforms are computed only while they are shown
    if (widget == ui->scopeContainer) {
        m_radioPagingDemod->setScopeEnabled(rollDown);
    }

    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList());
}

void RadioPagingDemodGUI::onMenuDialogCalled(const QPoint &p)
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
            dialog.setNumberOfStreams(m_radioPagingDemod->getNumberOfDeviceStreams());
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

        applySettings(QStringList({"title", "rgbColor", "useReverseAPI", "reverseAPIAddress",
            "reverseAPIPort", "reverseAPIDeviceIndex", "reverseAPIChannelIndex", "streamIndex"}));
    }

    resetContextMenuType();
}

RadioPagingDemodGUI::RadioPagingDemodGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget* parent) :
    ChannelGUI(parent),
    ui(new Ui::RadioPagingDemodGUI),
    m_pluginAPI(pluginAPI),
    m_deviceUISet(deviceUISet),
    m_channelMarker(this),
    m_deviceCenterFrequency(0),
    m_basebandSampleRate(1),
    m_doApplySettings(true),
    m_tickCount(0),
    m_scopeVis(nullptr)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channelrx/demodradiopaging/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    setSizePolicy(rollupContents->sizePolicy());
    // Output and waveforms are rolled up until the user opens them (a state saved before wins)
    ui->outputContainer->hide();
    ui->scopeContainer->hide();
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_radioPagingDemod = reinterpret_cast<RadioPagingDemod*>(rxChannel);
    m_radioPagingDemod->setMessageQueueToGUI(getInputMessageQueue());

    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick())); // 50 ms

    m_scopeVis = m_radioPagingDemod->getScopeSink();
    m_scopeVis->setGLScope(ui->glScope);
    m_scopeVis->setLiveRate(RadioPagingDemodSettings::m_channelSampleRate);
    ui->glScope->connectTimer(MainCore::instance()->getMasterTimer());
    ui->scopeGUI->setBuddies(m_scopeVis->getInputMessageQueue(), m_scopeVis, ui->glScope);
    ui->scopeGUI->setSampleRate(RadioPagingDemodSettings::m_channelSampleRate);

    for (int signal = 0; signal < RadioPagingDemodSink::ScopeSignalCount; signal++)
    {
        ui->channel1->addItem(RadioPagingDemodSink::scopeSignalName(signal));
        ui->channel2->addItem(RadioPagingDemodSink::scopeSignalName(signal));
    }

    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);
    ui->channelPowerMeter->setColorTheme(LevelMeterSignalDB::ColorGreenAndBlue);

    m_channelMarker.blockSignals(true);
    m_channelMarker.setColor(QColor(255, 170, 0));
    m_channelMarker.setBandwidth(m_settings.m_rfBandwidth);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle("Radio Paging Demodulator");
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(true); // activate signal on the last setting only

    setTitleColor(m_channelMarker.getColor());
    m_settings.setChannelMarker(&m_channelMarker);
    m_settings.setScopeGUI(ui->scopeGUI);
    m_settings.setRollupState(&m_rollupState);

    m_deviceUISet->addChannelMarker(&m_channelMarker);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));
    connect(&m_channelMarker, SIGNAL(highlightedByCursor()), this, SLOT(channelMarkerHighlightedByCursor()));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    ui->messages->setMaxRows(m_maxRows);
    ui->messages->setDefaultColumns({PagingMessageTable::ColTime, PagingMessageTable::ColProtocol, PagingMessageTable::ColRate,
        PagingMessageTable::ColPolarity, PagingMessageTable::ColAddress, PagingMessageTable::ColType, PagingMessageTable::ColMessage,
        PagingMessageTable::ColFragment, PagingMessageTable::ColInfo, PagingMessageTable::ColErrors});
    connect(ui->messages, &PagingMessageTable::columnsChanged, this, [this]() {
        m_settings.m_messageColumns = ui->messages->columns();
        applySettings(QStringList("messageColumns"));
    });
    // The voice files are played on the output of the voice audio
    connect(ui->messages, &PagingMessageTable::audioDeviceChanged, this, [this](const QString& deviceName) {
        m_settings.m_audioDeviceName = deviceName;
        applySettings(QStringList("audioDeviceName"));
    });

    CRightClickEnabler *audioRightClickEnabler = new CRightClickEnabler(ui->gscAudio);
    connect(audioRightClickEnabler, SIGNAL(rightClick(const QPoint &)), this, SLOT(audioSelect(const QPoint &)));
    CRightClickEnabler *recordRightClickEnabler = new CRightClickEnabler(ui->gscVoiceRecord);
    connect(recordRightClickEnabler, SIGNAL(rightClick(const QPoint &)), this, SLOT(voiceDirectorySelect(const QPoint &)));
    // The list of the custom POCSAG fill codes
    CRightClickEnabler *fillRightClickEnabler = new CRightClickEnabler(ui->pocsagFill);
    connect(fillRightClickEnabler, &CRightClickEnabler::rightClick, this, [this](const QPoint&) {
        bool ok;
        QString codes = QInputDialog::getText(this, tr("POCSAG fill codes"),
            tr("Hex codes 00 to 7F, separated by spaces or commas"), QLineEdit::Normal, m_settings.m_pocsagFillCodes, &ok);
        std::vector<uint8_t> parsed;

        if (!ok) {
            return;
        }
        if (!RadioPagingDemodSettings::parseFillCodes(codes, parsed))
        {
            QMessageBox::warning(this, tr("POCSAG fill codes"), tr("%1 is not a list of hex codes 00 to 7F").arg(codes));
            return;
        }

        m_settings.m_pocsagFillCodes = codes.trimmed();
        m_settings.m_pocsagFill = RadioPagingDemodSettings::FillCustom;
        displaySettings();
        applySettings(QStringList({"pocsagFill", "pocsagFillCodes"}));
    });

    PagingComposer::fillCodepages(ui->pocsagCharset, "pocsag", false);
    PagingComposer::fillCodepages(ui->flexCharset, "flex", false);
    PagingComposer::fillCodepages(ui->gscCharset, "gsc", false);
    // A protocol with a single codepage has no choice
    ui->pocsagCharset->setVisible(RadioPagingCharset::codepages("pocsag").size() > 1);
    ui->flexCharset->setVisible(RadioPagingCharset::codepages("flex").size() > 1);
    ui->gscCharset->setVisible(RadioPagingCharset::codepages("gsc").size() > 1);

    // The status lines: fields of fixed width in a fixed-pitch font, so that nothing moves
    // when the status changes
    for (QLabel *label : {ui->pocsagStatus, ui->flexStatus, ui->gscStatus})
    {
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (label->font().pointSizeF() > 0) {
            font.setPointSizeF(label->font().pointSizeF());
        }
        label->setFont(font);
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(false);
        label->setFixedWidth(statusWidth(label));
    }
    // Each polarity shown has its lines: the status, for FLEX the network of the last frame on two more lines,
    // and the memory of the receivers. The height follows the polarities shown
    ui->flexStatus->setFixedHeight(statusHeight(ui->flexStatus, statusFlexLines));
    ui->flexStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->pocsagStatus->setFixedHeight(statusHeight(ui->pocsagStatus, statusLines));
    ui->gscStatus->setFixedHeight(statusHeight(ui->gscStatus, statusLines));

    // FLEX: under its status, the 16 group slots (temporary addresses) of each polarity shown. The tooltip of a slot
    // gives its members and the frame of its group message
    QWidget *flexColumn = new QWidget(this);
    QVBoxLayout *flexColumnLayout = new QVBoxLayout(flexColumn);
    flexColumnLayout->setContentsMargins(0, 0, 0, 0);
    flexColumnLayout->setSpacing(1);
    delete ui->protocolLayout->replaceWidget(ui->flexStatus, flexColumn);
    flexColumnLayout->addWidget(ui->flexStatus);

    for (int pol = 0; pol < 2; pol++)
    {
        m_flexGroupBar[pol] = new QWidget(flexColumn);
        QHBoxLayout *bar = new QHBoxLayout(m_flexGroupBar[pol]);
        bar->setContentsMargins(0, 0, 0, 0);
        bar->setSpacing(1);
        QLabel *title = new QLabel(QString("%1 GRP").arg(pol == 1 ? "INV " : "NORM"), m_flexGroupBar[pol]);
        title->setFont(ui->flexStatus->font());
        title->setToolTip(tr("Group slots of the %1 network: the 16 temporary addresses (2029568 to 2029583) "
            "a short instruction assigns to pagers for a group message").arg(pol == 1 ? tr("inverted") : tr("normal")));
        bar->addWidget(title);

        for (int s = 0; s < 16; s++)
        {
            QLabel *cell = new QLabel(QString::number(s, 16).toUpper(), m_flexGroupBar[pol]);
            cell->setFont(ui->flexStatus->font());
            cell->setAlignment(Qt::AlignCenter);
            cell->setFixedWidth(cell->fontMetrics().horizontalAdvance("00") + 2);
            m_flexGroupCell[pol][s] = cell;
            bar->addWidget(cell);
        }

        bar->addStretch(1);
        flexColumnLayout->addWidget(m_flexGroupBar[pol]);
        m_flexGroupBar[pol]->setVisible(false);
    }

    // A button for each protocol that makes its receivers forget what they remember
    const char *protocolNames[3] = {"POCSAG", "FLEX", "GSC"};
    for (int p = 0; p < 3; p++)
    {
        QToolButton *reset = new QToolButton(this);
        reset->setIcon(QIcon(":/recycle.png"));
        reset->setToolTip(tr("Forget what the %1 receivers remember: the messages for duplicate detection%2.\n"
            "Done when the device starts too, so that a file played again is not taken as duplicates")
            .arg(protocolNames[p]).arg(p == 1 ? tr(", fragments, groups, message numbers and network time") : QString()));
        ui->protocolLayout->addWidget(reset, p, 7);
        connect(reset, &QToolButton::clicked, this, [this, p]() { m_radioPagingDemod->resetProtocol(p); });
    }
    m_pocsagToolTip = ui->pocsagStatus->toolTip();
    m_gscToolTip = ui->gscStatus->toolTip();
    m_statusClock.start();

    displaySettings();
    m_radioPagingDemod->setScopeEnabled(!ui->scopeContainer->isHidden());
    makeUIConnections();
    applySettings(QStringList(), true);
    m_resizer.enableChildMouseTracking();
}

RadioPagingDemodGUI::~RadioPagingDemodGUI()
{
    delete ui;
}

void RadioPagingDemodGUI::blockApplySettings(bool block)
{
    m_doApplySettings = !block;
}

void RadioPagingDemodGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    if (m_doApplySettings)
    {
        RadioPagingDemod::MsgConfigureRadioPagingDemod* message =
            RadioPagingDemod::MsgConfigureRadioPagingDemod::create(settingsKeys, m_settings, force);
        m_radioPagingDemod->getInputMessageQueue()->push(message);
    }
}

void RadioPagingDemodGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setBandwidth(m_settings.m_rfBandwidth);
    m_channelMarker.setCenterFrequency(m_settings.m_inputFrequencyOffset);
    m_channelMarker.setTitle(m_settings.m_title);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(m_settings.m_rgbColor); // activate signal on the last setting only

    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_channelMarker.getTitle());
    setTitle(m_channelMarker.getTitle());

    blockApplySettings(true);

    ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());
    ui->rfBWText->setText(QString("%1k").arg(m_settings.m_rfBandwidth / 1000.0, 0, 'f', 1));
    ui->rfBW->setValue(m_settings.m_rfBandwidth / 100.0);
    ui->pocsagDeviation->setValue(m_settings.m_pocsagDeviation);
    ui->flexDeviation->setValue(m_settings.m_flexDeviation);
    ui->flexMaxMessageNumber->setValue(m_settings.m_flexMaxMessageNumber);
    PagingComposer::selectCodepage(ui->pocsagCharset, m_settings.m_pocsagCharset);
    ui->pocsagFill->setCurrentIndex(m_settings.m_pocsagFill);
    ui->pocsagFill->setItemData(RadioPagingDemodSettings::FillCustom,
        tr("Custom fill codes: %1 (right-click to edit)").arg(m_settings.m_pocsagFillCodes), Qt::ToolTipRole);
    PagingComposer::selectCodepage(ui->flexCharset, m_settings.m_flexCharset);
    PagingComposer::selectCodepage(ui->gscCharset, m_settings.m_gscCharset);
    ui->pocsagEnabled->setChecked(m_settings.m_pocsagEnabled);
    ui->flexEnabled->setChecked(m_settings.m_flexEnabled);
    ui->gscEnabled->setChecked(m_settings.m_gscEnabled);
    ui->pocsagDcCorrection->setChecked(m_settings.m_pocsagDcCorrection);
    ui->flexDcCorrection->setChecked(m_settings.m_flexDcCorrection);
    ui->gscDcCorrection->setChecked(m_settings.m_gscDcCorrection);
    ui->gscVoiceTimeout->setValue(m_settings.m_gscVoiceTimeout);
    ui->gscDeviation->setValue(m_settings.m_gscDeviation);
    ui->gscAudio->setChecked(m_settings.m_gscAudio);
    ui->gscVoiceRecord->setChecked(m_settings.m_gscVoiceRecord);
    ui->channelAlias->setText(m_settings.m_channelAlias);
    displayVoiceRecordToolTip();
    ui->volume->setValue(std::round(m_settings.m_volume * 100.0f));
    ui->volumeText->setText(QString::number(ui->volume->value()));

    // The columns the user chose; the first time the usual ones
    if (!ui->messages->setColumns(m_settings.m_messageColumns)) {
        ui->messages->sizeColumns();
    }

    ui->messages->setAudioDevice(m_settings.m_audioDeviceName);
    ui->udpEnabled->setChecked(m_settings.m_udpEnabled);
    ui->udpAddress->setText(m_settings.m_udpAddress);
    ui->udpPort->setValue(m_settings.m_udpPort);
    ui->udpFormat->setCurrentIndex(m_settings.m_udpFormat);
    ui->logEnable->setChecked(m_settings.m_logEnabled);
    ui->logFilename->setToolTip(tr(".csv log file: %1").arg(m_settings.m_logFilename));
    ui->channel1->setCurrentIndex(m_settings.m_scopeCh1);
    ui->channel2->setCurrentIndex(m_settings.m_scopeCh2);
    enableSpeechIfNeeded();
    updateIndexLabel();

    getRollupContents()->restoreState(m_rollupState);
    updateAbsoluteCenterFrequency();
    blockApplySettings(false);
}

void RadioPagingDemodGUI::leaveEvent(QEvent* event)
{
    m_channelMarker.setHighlighted(false);
    ChannelGUI::leaveEvent(event);
}

void RadioPagingDemodGUI::enterEvent(EnterEventType* event)
{
    m_channelMarker.setHighlighted(true);
    ChannelGUI::enterEvent(event);
}

void RadioPagingDemodGUI::tick()
{
    double magsqAvg, magsqPeak;
    int nbMagsqSamples;
    m_radioPagingDemod->getMagSqLevels(magsqAvg, magsqPeak, nbMagsqSamples);
    double powDbAvg = CalcDb::dbPower(magsqAvg);
    double powDbPeak = CalcDb::dbPower(magsqPeak);

    ui->channelPowerMeter->levelChanged(
            (100.0f + powDbAvg) / 100.0f,
            (100.0f + powDbPeak) / 100.0f,
            nbMagsqSamples);

    if (m_tickCount % 4 == 0)
    {
        ui->channelPower->setText(QString::number(powDbAvg, 'f', 1));
        updateStatus();
    }

    m_tickCount++;
}

// Per protocol one line of fixed fields: SYNC (green) or HUNT (grey), what is received and its
// polarity, the reception quality, the carrier offset and the number of messages. The
// details of the last reception are in the tooltip
void RadioPagingDemodGUI::updateStatus()
{
    RadioPagingDemodSink::Status status = m_radioPagingDemod->getStatus();
    bool shown[3][2];
    QLabel *labels[3] = {ui->pocsagStatus, ui->flexStatus, ui->gscStatus};
    const int lines[3] = {statusLines, statusFlexLines, statusLines};

    for (int p = 0; p < 3; p++)
    {
        shownPolarities(status, p, shown[p]);
        int count = (shown[p][0] ? 1 : 0) + (shown[p][1] ? 1 : 0);

        if (count != m_statusPolarities[p])
        {
            m_statusPolarities[p] = count;
            labels[p]->setFixedHeight(statusHeight(labels[p], count * lines[p]));
        }
    }

    // POCSAG: the speeds being received on the polarity
    QStringList blocks;

    for (int pol = 0; pol < 2; pol++)
    {
        if (!shown[0][pol]) {
            continue;
        }

        QStringList speeds;

        for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++)
        {
            if (status.m_pocsagSpeedSync[pol][i]) {
                speeds.append(QString::number(RadioPagingDemodSettings::m_pocsagSpeeds[i]));
            }
        }

        blocks.append(statusLine(status, 0, pol, status.m_pol[0][pol].m_sync, "SYNC", speeds.join(" "))
            + "<br>" + memoryLine(status, 0, pol));
    }

    ui->pocsagStatus->setText(blocks.join("<br>"));

    // FLEX: between the frames the decoder hunts for the next sync 1; SYNC stays shown until
    // that sync is due, then HUNT (the decoder state is not changed)
    qint64 now = m_statusClock.elapsed();

    if (status.m_flexSync && !m_flexWasSync) {
        m_flexFrameStart = now;
    } else if (status.m_flexSync && (now - m_flexFrameStart >= m_flexFrameMs)) {
        m_flexFrameStart += m_flexFrameMs;  // Frames back to back: the next one started
    }
    if (!status.m_enabled[1]) {
        m_flexFrameStart = -1;
    }
    m_flexWasSync = status.m_flexSync;

    bool flexHeld = !status.m_flexSync && (m_flexFrameStart >= 0)
        && (now - m_flexFrameStart < m_flexFrameMs + m_statusPeriodMs);
    // The polarity of the frame being received, or of the last one
    int flexPol = status.m_flexInverted ? 1 : 0;
    blocks.clear();

    for (int pol = 0; pol < 2; pol++)
    {
        if (!shown[1][pol]) {
            continue;
        }

        QString line;

        // The re-synchronization has no mode or polarity: it is shown on the line of the last frame
        if ((pol == flexPol) && status.m_flexErs && !status.m_flexSync) {
            line = statusLine(status, 1, pol, true, "ERS", QString("%1 s").arg(status.m_flexErsSeconds, 0, 'f', 2));
        } else {
            line = statusLine(status, 1, pol, status.m_pol[1][pol].m_sync || ((pol == flexPol) && flexHeld), "SYNC",
                status.m_flexModePol[pol]);
        }

        if (status.m_enabled[1]) {
            line += "<br>" + flexNetworkLines(status, pol) + "<br>" + memoryLine(status, 1, pol);
        }

        blocks.append(line);
    }

    ui->flexStatus->setText(blocks.join("<br>"));
    updateGroupBars(status, shown[1]);

    blocks.clear();

    for (int pol = 0; pol < 2; pol++)
    {
        if (shown[2][pol])
        {
            blocks.append(statusLine(status, 2, pol, status.m_pol[2][pol].m_sync, status.m_gscVoicePol[pol] ? "VOICE" : "SYNC",
                QString()) + "<br>" + memoryLine(status, 2, pol));
        }
    }

    ui->gscStatus->setText(blocks.join("<br>"));

    QString pocsagTip = m_pocsagToolTip;
    if (status.m_enabled[0] && (status.m_pocsagBatches > 0)) {
        pocsagTip += tr("\n\nBatches of the last reception: %1").arg(status.m_pocsagBatches);
    }
    ui->pocsagStatus->setToolTip(pocsagTip);
    ui->flexStatus->setToolTip(flexToolTip(status));

    QString gscTip = m_gscToolTip;
    if (status.m_enabled[2] && (status.m_messages[2] > 0))
    {
        QString last = status.m_gscNbs ? QString("non battery saver")
            : QString("preamble %1%2").arg(status.m_gscPreamble).arg(status.m_gscBatch ? ", batch" : "");
        gscTip += tr("\n\nLast page: %1, slot %2").arg(last).arg(status.m_gscSlot);
    }
    ui->gscStatus->setToolTip(gscTip);

    // Green while the voice is played
    bool playing = status.m_enabled[2] && status.m_gscAudio;
    ui->gscAudio->setStyleSheet(playing ? "QToolButton { background-color : green; }" : "");
}

// Extended FLEX status of the network of the last frame: frame, network time and
// identification (once received several times the same), groups and reassemblies
QStringList RadioPagingDemodGUI::flexDetails(const RadioPagingDemodSink::Status& status)
{
    const RadioPagingFlexDecoder::FrameReport& frame = status.m_flexFrame;
    const RadioPagingFlexDecoder::NetworkTime& t = status.m_flexTime;
    QStringList lines;

    if (frame.m_fiwValid)
    {
        QString text = QString("C%1/F%2").arg(frame.m_cycle, 2, 10, QChar('0')).arg(frame.m_frame, 3, 10, QChar('0'));
        if (!frame.m_blocks.empty()) {
            text += QString(" collapse %1").arg(frame.m_blocks.front().m_collapse);
        }
        if (frame.m_roaming) {
            text += " roaming";
        }
        if (frame.m_repeat) {
            text += QString(" repeat x%1").arg(frame.m_transmissions);
        }
        lines.append(text);
    }

    QStringList network;

    if (t.m_dateValid) {
        network.append(QString("%1-%2-%3").arg(t.m_year).arg(t.m_month, 2, 10, QChar('0')).arg(t.m_day, 2, 10, QChar('0')));
    }
    if (t.m_timeValid)
    {
        network.append(QString("%1:%2:%3").arg(t.m_currentHour, 2, 10, QChar('0')).arg(t.m_currentMinute, 2, 10, QChar('0'))
            .arg((int) t.m_currentSecond, 2, 10, QChar('0')));
    }
    if (t.m_zoneValid)
    {
        // L0 = 0: the time is daylight saving time (STD-43A 3.7.2)
        QString saving = (t.m_dst == 0) ? "dst" : "std";

        if (t.m_zoneDefined)
        {
            int minutes = t.m_zoneMinutes;
            network.append(QString("UTC%1%2:%3 %4").arg(minutes < 0 ? "-" : "+").arg(std::abs(minutes) / 60)
                .arg(std::abs(minutes) % 60, 2, 10, QChar('0')).arg(saving));
        }
        else
        {
            network.append(QString("zone %1 (not defined) %2").arg(t.m_zone).arg(saving));
        }
    }
    if (t.m_ssid1Valid) {
        network.append(QString("LID %1 coverage %2").arg(t.m_localId).arg(t.m_coverageZone));
    }
    if (t.m_ssid2Valid) {
        network.append(QString("country %1 TMF %2").arg(t.m_country).arg(t.m_tmf));
    }
    if (!network.isEmpty()) {
        lines.append(network.join("  "));
    }

    QStringList groups;

    for (const auto& g : status.m_flexGroups) {
        groups.append(QString("G%1[%2]>F%3").arg(g.m_slot).arg(g.m_members.size()).arg(g.m_targetFrame, 3, 10, QChar('0')));
    }
    if (!groups.isEmpty() || (status.m_flexReassemblies > 0))
    {
        lines.append(QString("Groups %1/16 %2  reasm %3/%4").arg(status.m_flexGroups.size()).arg(groups.join(" "))
            .arg(status.m_flexReassemblies).arg(RadioPagingFlexDecoder::m_reassemblySlots));
    }
    if ((status.m_flexErsCount > 0) && !status.m_flexErs)
    {
        lines.append(QString("Last ERS %1 s (%2)").arg(status.m_flexErsSeconds, 0, 'f', 2).arg(status.m_flexErsCount));
    }

    return lines;
}

QString RadioPagingDemodGUI::flexToolTip(const RadioPagingDemodSink::Status& status)
{
    QString tip = tr("FLEX state: SYNC or HUNT, mode and polarity of the frame,\n"
        "BER the bit error rate of the recent codewords (green no errors, yellow errors corrected,\n"
        "red errors that could not be corrected, grey after the reception),\n"
        "%1f the carrier frequency offset (DC offset of the discriminator; the levels of sync 1 are kept through\n"
        "4-level frames) and messages received.\n"
        "Line 2: cycle and frame, collapse cN, R roaming, xN repeated frame, network date, time and zone.\n"
        "Line 3: BER of each phase of a frame of several phases; LID local id, CZ coverage zone,\n"
        "CC country code, TMF traffic management flags.\n"
        "Between the frames of a transmission SYNC stays shown until the sync of the next frame is due\n"
        "(1.875 s after the start of the last one).\n"
        "ERS: emergency re-synchronization being received (no mode or polarity), with its length so far;\n"
        "last ERS: length of the last one and the number received.\n"
        "Cxx/Fxxx: cycle and frame of the last frame. Collapse n: pagers wake up every 2^n frames.\n"
        "Roaming: roaming bit of the frame. Repeat xN: the frame content is sent N times.\n"
        "Date, time and zone: network time from the block information words.\n"
        "LID: local id, coverage: coverage zone, country: country code,\n"
        "TMF: traffic management flags (which frames this transmitter sends).\n"
        "Groups: temporary addresses in use, Gs[members]>F target frame of the group message.\n"
        "Reasm: fragmented messages being reassembled.").arg(QChar(0x394));

    if (status.m_enabled[1])
    {
        QStringList details = flexDetails(status);
        if (!details.isEmpty()) {
            tip += "\n\n" + details.join("\n");
        }
    }

    for (const auto& g : status.m_flexGroups)
    {
        QStringList members;
        for (uint64_t capcode : g.m_members) {
            members.append(QString("%1").arg((qulonglong) capcode, 10, 10, QChar('0')));
        }
        tip += QString("\nG%1: %2 (set up C%3/F%4)").arg(g.m_slot).arg(members.join(", "))
            .arg(g.m_setupCycle, 2, 10, QChar('0')).arg(g.m_setupFrame, 3, 10, QChar('0'));
    }

    return tip;
}

QString RadioPagingDemodGUI::polarityHtml(bool inverted)
{
    return inverted ? QString("<span style=\"color:#ffcc44\">INV</span>") : QString("<span style=\"color:#88aacc\">NORM</span>");
}

// Carrier frequency offset measured as the DC offset of the discriminator. Grey once the
// reception ended (the last value is kept)
QString RadioPagingDemodGUI::offsetHtml(float offset, bool live)
{
    QString text = QString("%1f %2%3 Hz").arg(QChar(0x394)).arg(offset < 0.0f ? "-" : "+").arg((int) std::round(std::fabs(offset)));
    return live ? text : QString("<span style=\"color:#888888\">%1</span>").arg(text);
}

// Bit error rate of the reception. Green: no bit errors, yellow: errors the code corrected, light red: errors the
// code could not correct. Grey once the reception ended
QString RadioPagingDemodGUI::qualityHtml(const RadioPagingBerWindow& ber, bool live)
{
    if (!ber.valid()) {
        return QString();
    }

    QString colour = !live ? "#888888" : (ber.m_uncorrectable > 0) ? "#ff6644" : (ber.m_errorBits > 0) ? "#ffcc44" : "#44cc44";

    return QString("<span style=\"color:%1\">BER %2%</span>").arg(colour).arg(100.0 * ber.ber(), 0, 'f', 2);
}

namespace {

// The fields of a status line, in characters of the fixed-pitch font
// The text of a field padded to its width (characters beyond it are cut)
QString statusField(const QString& html, int width)
{
    static const QRegularExpression tags("<[^>]*>");
    QString visible = QString(html).remove(tags);
    QString text = html;

    if (visible.size() > width)
    {
        // Only the plain text of a field too long is cut
        text = visible.left(width - 1) + "~";
        visible = text;
    }

    QString padding;
    for (int i = (int) visible.size(); i < width + statusSpacing; i++) {
        padding += "&nbsp;";
    }

    return text + padding;
}

} // namespace

QString RadioPagingDemodGUI::statusLine(const RadioPagingDemodSink::Status& status, int protocol, int pol, bool sync,
    const QString& state, const QString& detail)
{
    if (!status.m_enabled[protocol]) {
        return "<span style=\"color:#888888\">OFF</span>";
    }

    const RadioPagingDemodSink::Status::Polarity& s = status.m_pol[protocol][pol];
    QString stateHtml = sync
        ? QString("<b style=\"color:#44cc44\">%1</b>").arg(state)
        : QString("<b style=\"color:#888888\">HUNT</b>");
    QString text = statusField(polarityHtml(pol == 1), statusPolarityWidth)
        + statusField(stateHtml, statusStateWidth)
        + statusField(sync ? detail : QString(), statusDetailWidth)
        + statusField(qualityHtml(s.m_ber, s.m_berLive), statusQualityWidth)
        + statusField(s.m_offsetValid ? offsetHtml(s.m_offset, s.m_berLive) : QString(), statusOffsetWidth);

    return text + QString("(%1)").arg(s.m_messages);
}

// The polarities received since the reset; the normal one while none was
void RadioPagingDemodGUI::shownPolarities(const RadioPagingDemodSink::Status& status, int protocol, bool shown[2])
{
    shown[0] = status.m_enabled[protocol] && status.m_pol[protocol][0].m_seen;
    shown[1] = status.m_enabled[protocol] && status.m_pol[protocol][1].m_seen;

    if (!shown[0] && !shown[1]) {
        shown[0] = true;
    }
}

// MEM 12, FLEX also the fragments being reassembled, and the time of the last reset. Grey: it is not received
QString RadioPagingDemodGUI::memoryLine(const RadioPagingDemodSink::Status& status, int protocol, int pol)
{
    if (!status.m_enabled[protocol]) {
        return QString();
    }

    QString text = statusField(QString(), statusPolarityWidth) + QString("MEM %1").arg(status.m_pol[protocol][pol].m_remembered);

    if (protocol == 1) {
        text += QString(" FRAG %1").arg(status.m_flexReassembliesPol[pol]);
    }
    if (status.m_resetTime[protocol] > 0) {
        text += " RESET " + QDateTime::fromMSecsSinceEpoch(status.m_resetTime[protocol]).toString("HH:mm:ss");
    }

    return QString("<span style=\"color:#888888\">%1</span>").arg(text);
}

// FLEX: the group slots of each polarity shown. A slot in use is green; its tooltip gives the members (their number,
// the first of them), the frame of the group message and the frame of the assignment
void RadioPagingDemodGUI::updateGroupBars(const RadioPagingDemodSink::Status& status, const bool shown[2])
{
    for (int pol = 0; pol < 2; pol++)
    {
        bool visible = status.m_enabled[1] && shown[pol];
        m_flexGroupBar[pol]->setVisible(visible);

        if (!visible) {
            continue;
        }

        const RadioPagingFlexDecoder::GroupSlot *inUse[16] = {};

        for (const RadioPagingFlexDecoder::GroupSlot& slot : status.m_flexGroupsPol[pol])
        {
            if ((slot.m_slot >= 0) && (slot.m_slot < 16)) {
                inUse[slot.m_slot] = &slot;
            }
        }

        for (int s = 0; s < 16; s++)
        {
            QLabel *cell = m_flexGroupCell[pol][s];

            if (inUse[s])
            {
                cell->setStyleSheet("QLabel { background-color : #2e7d32; color : white; }");
                cell->setToolTip(groupSlotToolTip(*inUse[s], pol, status));
            }
            else
            {
                cell->setStyleSheet("QLabel { color : #666666; }");
                cell->setToolTip(tr("Group slot %1 (temporary address %2): free").arg(s).arg(2029568 + s));
            }
        }
    }
}

QString RadioPagingDemodGUI::groupSlotToolTip(const RadioPagingFlexDecoder::GroupSlot& slot, int pol,
    const RadioPagingDemodSink::Status& status)
{
    static const int shownMembers = 8;
    QStringList members;

    for (size_t i = 0; (i < slot.m_members.size()) && (i < (size_t) shownMembers); i++) {
        members.append(QString::fromStdString(RadioPagingFlex::addressText(slot.m_members[i])));
    }
    if (slot.m_members.size() > (size_t) shownMembers) {
        members.append(tr("and %1 more").arg(slot.m_members.size() - shownMembers));
    }

    QString text = tr("Group slot %1 (temporary address %2), %3 network\nMembers: %4")
        .arg(slot.m_slot).arg(2029568 + slot.m_slot).arg(pol == 1 ? tr("inverted") : tr("normal"))
        .arg(slot.m_members.size());

    if (!members.isEmpty()) {
        text += "\n" + members.join("\n");
    }

    text += tr("\nGroup message in frame %1").arg(slot.m_targetFrame, 3, 10, QChar('0'));
    text += tr("\nAssigned in cycle %1 frame %2").arg(slot.m_setupCycle, 2, 10, QChar('0')).arg(slot.m_setupFrame, 3, 10, QChar('0'));

    const RadioPagingFlexDecoder::FrameReport& frame = status.m_flexFramePol[pol];

    if (frame.m_fiwValid)
    {
        int framesToGo = ((int) slot.m_targetFrame - (int) frame.m_frame + 128) % 128;
        text += tr("\nLast frame received: %1, the group message is %2 frames on").arg(frame.m_frame, 3, 10, QChar('0')).arg(framesToGo);
    }

    return text;
}

// FLEX: two more lines of fixed fields, the network of the last frame of the polarity. Line 2: cycle and frame,
// collapse, roaming bit and repeats of the frame, the network date, time and time zone (block information words).
// Line 3: the bit error rate of each phase of a frame of several phases, the system ID (local id, coverage zone,
// country code, traffic management flags). A field not received yet is blank; grey once the reception ended
QString RadioPagingDemodGUI::flexNetworkLines(const RadioPagingDemodSink::Status& status, int pol)
{
    const RadioPagingFlexDecoder::FrameReport& frame = status.m_flexFramePol[pol];
    const RadioPagingFlexDecoder::NetworkTime& t = status.m_flexTimePol[pol];
    const bool live = status.m_pol[1][pol].m_berLive;
    auto grey = [live](const QString& text) {
        return (live || text.isEmpty()) ? text : QString("<span style=\"color:#888888\">%1</span>").arg(text);
    };

    QString cycleFrame, flags, date, time, zone;

    if (frame.m_fiwValid)
    {
        cycleFrame = QString("C%1/F%2").arg(frame.m_cycle, 2, 10, QChar('0')).arg(frame.m_frame, 3, 10, QChar('0'));
        flags = frame.m_blocks.empty() ? QString() : QString("c%1").arg(frame.m_blocks.front().m_collapse);
        flags += frame.m_roaming ? " R" : "";
        flags += frame.m_repeat ? QString(" x%1").arg(frame.m_transmissions) : QString();
    }
    if (t.m_dateValid) {
        date = QString("%1-%2-%3").arg(t.m_year).arg(t.m_month, 2, 10, QChar('0')).arg(t.m_day, 2, 10, QChar('0'));
    }
    if (t.m_timeValid)
    {
        time = QString("%1:%2:%3").arg(t.m_currentHour, 2, 10, QChar('0')).arg(t.m_currentMinute, 2, 10, QChar('0'))
            .arg((int) t.m_currentSecond, 2, 10, QChar('0'));
    }
    if (t.m_zoneValid)
    {
        // L0 = 0: daylight saving time (STD-43A 3.7.2)
        int minutes = t.m_zoneMinutes;
        zone = t.m_zoneDefined
            ? QString("UTC%1%2:%3").arg(minutes < 0 ? "-" : "+").arg(std::abs(minutes) / 60, 2, 10, QChar('0'))
                .arg(std::abs(minutes) % 60, 2, 10, QChar('0'))
            : QString("zone %1").arg(t.m_zone);
        zone += (t.m_dst == 0) ? " DST" : "";
    }

    QString line2 = statusField(grey(cycleFrame), 8) + statusField(grey(flags), 8) + statusField(grey(date), 10)
        + statusField(grey(time), 8) + grey(zone);

    // The phases of the last frame: only for a frame of several phases (one phase: the BER of line 1)
    QString phases;
    int phaseFields = 0;

    if (frame.m_blocks.size() > 1)
    {
        for (const auto& block : frame.m_blocks)
        {
            const RadioPagingBerWindow& ber = status.m_flexPhaseBerPol[pol][(block.m_phase - 'A') & 3];

            if (ber.valid())
            {
                QString colour = !live ? "#888888" : (ber.m_uncorrectable > 0) ? "#ff6644" : (ber.m_errorBits > 0) ? "#ffcc44" : "#44cc44";
                phases += statusField(QString("<span style=\"color:%1\">%2%3%</span>").arg(colour).arg(block.m_phase)
                    .arg(100.0 * ber.ber(), 0, 'f', 2), 6);
                phaseFields++;
            }
        }
    }
    for (; phaseFields < 4; phaseFields++) {
        phases += statusField(QString(), 6);
    }

    QString ssid1 = t.m_ssid1Valid ? QString("LID %1 CZ %2").arg(t.m_localId).arg(t.m_coverageZone) : QString();
    QString ssid2 = t.m_ssid2Valid ? QString("CC %1 TMF %2").arg(t.m_country).arg(t.m_tmf) : QString();
    QString line3 = phases + statusField(grey(ssid1), 13) + grey(ssid2);
    // Under the fields of line 1, after the polarity
    QString indent = statusField(QString(), statusPolarityWidth);

    return indent + line2 + "<br>" + indent + line3;
}

// The width of the longest status line in the font of the label
int RadioPagingDemodGUI::statusWidth(const QLabel *label)
{
    int chars = statusPolarityWidth + statusStateWidth + statusDetailWidth + statusQualityWidth + statusOffsetWidth
        + statusMessagesWidth + 5 * statusSpacing;
    chars = std::max(chars, statusPolarityWidth + statusSpacing + statusNetworkWidth);
    return label->fontMetrics().horizontalAdvance(QString(chars, QChar('0'))) + 2 * label->margin() + 4;
}

int RadioPagingDemodGUI::statusHeight(const QLabel *label, int lines)
{
    return lines * label->fontMetrics().lineSpacing() + 2 * label->margin() + 4;
}

void RadioPagingDemodGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &RadioPagingDemodGUI::on_deltaFrequency_changed);
    QObject::connect(ui->rfBW, &QSlider::valueChanged, this, &RadioPagingDemodGUI::on_rfBW_valueChanged);
    QObject::connect(ui->pocsagDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_pocsagDeviation_valueChanged);
    QObject::connect(ui->flexDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_flexDeviation_valueChanged);
    QObject::connect(ui->flexMaxMessageNumber, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_flexMaxMessageNumber_valueChanged);
    QObject::connect(ui->pocsagFill, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_settings.m_pocsagFill = index;
        applySettings(QStringList("pocsagFill"));
    });
    QObject::connect(ui->pocsagCharset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_settings.m_pocsagCharset = PagingComposer::codepageKey(ui->pocsagCharset);
        applySettings(QStringList("pocsagCharset"));
    });
    QObject::connect(ui->flexCharset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_settings.m_flexCharset = PagingComposer::codepageKey(ui->flexCharset);
        applySettings(QStringList("flexCharset"));
    });
    QObject::connect(ui->gscCharset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_settings.m_gscCharset = PagingComposer::codepageKey(ui->gscCharset);
        applySettings(QStringList("gscCharset"));
    });
    QObject::connect(ui->pocsagEnabled, &QAbstractButton::clicked, this, &RadioPagingDemodGUI::on_pocsagEnabled_clicked);
    QObject::connect(ui->flexEnabled, &QAbstractButton::clicked, this, &RadioPagingDemodGUI::on_flexEnabled_clicked);
    QObject::connect(ui->gscEnabled, &QAbstractButton::clicked, this, &RadioPagingDemodGUI::on_gscEnabled_clicked);
    QObject::connect(ui->pocsagDcCorrection, &QAbstractButton::clicked, this, [this](bool checked) {
        m_settings.m_pocsagDcCorrection = checked;
        applySettings(QStringList("pocsagDcCorrection"));
    });
    QObject::connect(ui->flexDcCorrection, &QAbstractButton::clicked, this, [this](bool checked) {
        m_settings.m_flexDcCorrection = checked;
        applySettings(QStringList("flexDcCorrection"));
    });
    QObject::connect(ui->gscDcCorrection, &QAbstractButton::clicked, this, [this](bool checked) {
        m_settings.m_gscDcCorrection = checked;
        applySettings(QStringList("gscDcCorrection"));
    });
    QObject::connect(ui->gscVoiceTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_gscVoiceTimeout_valueChanged);
    QObject::connect(ui->gscDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_gscDeviation_valueChanged);
    QObject::connect(ui->gscAudio, &QToolButton::toggled, this, &RadioPagingDemodGUI::on_gscAudio_toggled);
    QObject::connect(ui->gscVoiceRecord, &QToolButton::toggled, this, &RadioPagingDemodGUI::on_gscVoiceRecord_toggled);
    QObject::connect(ui->gscVoiceFiles, &QToolButton::clicked, this, &RadioPagingDemodGUI::on_gscVoiceFiles_clicked);
    QObject::connect(ui->channelAlias, &QLineEdit::editingFinished, this, &RadioPagingDemodGUI::on_channelAlias_editingFinished);
    QObject::connect(ui->volume, &QDial::valueChanged, this, &RadioPagingDemodGUI::on_volume_valueChanged);
    QObject::connect(ui->clearTable, &QToolButton::clicked, this, &RadioPagingDemodGUI::on_clearTable_clicked);
    QObject::connect(ui->openMessageCenter, &QPushButton::clicked, this, []() { PagingMessageTable::openMessageCenter(); });
    QObject::connect(ui->udpEnabled, &QCheckBox::clicked, this, &RadioPagingDemodGUI::on_udpEnabled_clicked);
    QObject::connect(ui->udpAddress, &QLineEdit::editingFinished, this, &RadioPagingDemodGUI::on_udpAddress_editingFinished);
    QObject::connect(ui->udpPort, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_udpPort_valueChanged);
    QObject::connect(ui->udpFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingDemodGUI::on_udpFormat_currentIndexChanged);
    QObject::connect(ui->notifications, &QToolButton::clicked, this, &RadioPagingDemodGUI::on_notifications_clicked);
    QObject::connect(ui->logEnable, &ButtonSwitch::clicked, this, &RadioPagingDemodGUI::on_logEnable_clicked);
    QObject::connect(ui->logFilename, &QToolButton::clicked, this, &RadioPagingDemodGUI::on_logFilename_clicked);
    QObject::connect(ui->logOpen, &QToolButton::clicked, this, &RadioPagingDemodGUI::on_logOpen_clicked);
    // The rows follow the filter as it is typed
    QObject::connect(ui->filterAddress, &QLineEdit::textChanged, this, &RadioPagingDemodGUI::on_filterAddress_editingFinished);
    QObject::connect(ui->channel1, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingDemodGUI::on_channel1_currentIndexChanged);
    QObject::connect(ui->channel2, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingDemodGUI::on_channel2_currentIndexChanged);
}

void RadioPagingDemodGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
