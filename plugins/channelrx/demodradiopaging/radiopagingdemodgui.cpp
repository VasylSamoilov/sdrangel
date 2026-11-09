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
#include <QLabel>

#include "radiopagingdemodgui.h"

#include "device/deviceuiset.h"
#include "dsp/dspcommands.h"
#include "ui_radiopagingdemodgui.h"
#include "plugin/pluginapi.h"
#include "util/db.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "maincore.h"

#include "radiopagingdemod.h"

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

// Size the columns using dummy data
void RadioPagingDemodGUI::resizeTable()
{
    int row = ui->messages->rowCount();
    ui->messages->setRowCount(row + 1);
    ui->messages->setItem(row, COL_TIME, new QTableWidgetItem("00:00:00"));
    ui->messages->setItem(row, COL_PROTOCOL, new QTableWidgetItem("POCSAG"));
    ui->messages->setItem(row, COL_MODE, new QTableWidgetItem("3200/4"));
    ui->messages->setItem(row, COL_ADDRESS, new QTableWidgetItem("0001234567"));
    ui->messages->setItem(row, COL_POLARITY, new QTableWidgetItem("I"));
    ui->messages->setItem(row, COL_TYPE, new QTableWidgetItem("alphanumeric"));
    ui->messages->setItem(row, COL_MESSAGE, new QTableWidgetItem("ABCEDGHIJKLMNOPQRSTUVWXYZ"));
    ui->messages->setItem(row, COL_ERRORS, new QTableWidgetItem("99/99"));
    ui->messages->resizeColumnsToContents();
    ui->messages->removeRow(row);
}

bool RadioPagingDemodGUI::handleMessage(const Message& message)
{
    if (RadioPagingDemod::MsgConfigureRadioPagingDemod::match(message))
    {
        qDebug("RadioPagingDemodGUI::handleMessage: RadioPagingDemod::MsgConfigureRadioPagingDemod");
        const RadioPagingDemod::MsgConfigureRadioPagingDemod& cfg = (const RadioPagingDemod::MsgConfigureRadioPagingDemod&) message;
        m_settings = cfg.getSettings();
        blockApplySettings(true);
        m_channelMarker.updateSettings(static_cast<const ChannelMarker*>(m_settings.m_channelMarker));
        displaySettings();
        blockApplySettings(false);
        return true;
    }
    else if (MainCore::MsgMessageRecord::match(message))
    {
        const MainCore::MsgMessageRecord& msg = (const MainCore::MsgMessageRecord&) message;
        recordReceived(msg.getRecord());
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

// Newest message first. The end of a voice message updates the row of its voice page
void RadioPagingDemodGUI::recordReceived(const MessageRecord& record)
{
    QString errors = QString("%1/%2").arg(record.m_errorsCorrected).arg(record.m_errorsUncorrectable);
    QString text = record.m_text;

    if (record.m_protocolData.contains("voice_end"))
    {
        text = QString("voice ended: %1 after %2 s")
            .arg(record.m_protocolData.value("voice_end").toString())
            .arg(record.m_protocolData.value("voice_seconds").toDouble(), 0, 'f', 1);

        for (int row = 0; row < ui->messages->rowCount(); row++)
        {
            QTableWidgetItem *item = ui->messages->item(row, COL_TIME);

            if (item && (item->data(Qt::UserRole).toString() == record.m_id))
            {
                ui->messages->item(row, COL_MESSAGE)->setText(text);
                return;
            }
        }
    }

    ui->messages->insertRow(0);

    QTableWidgetItem *timeItem = new QTableWidgetItem(QDateTime::fromMSecsSinceEpoch(record.m_timestamp).toString("HH:mm:ss"));
    timeItem->setData(Qt::UserRole, record.m_id);
    timeItem->setToolTip(QDateTime::fromMSecsSinceEpoch(record.m_timestamp).toString(Qt::ISODate));
    ui->messages->setItem(0, COL_TIME, timeItem);
    ui->messages->setItem(0, COL_PROTOCOL, new QTableWidgetItem(record.m_protocol.toUpper()));
    ui->messages->setItem(0, COL_MODE, new QTableWidgetItem(record.m_mode));
    ui->messages->setItem(0, COL_ADDRESS, new QTableWidgetItem(record.m_address));
    ui->messages->setItem(0, COL_POLARITY, new QTableWidgetItem(record.m_polarity == MessageRecord::PolarityInverted ? "I" : "N"));
    ui->messages->setItem(0, COL_TYPE, new QTableWidgetItem(record.m_msgType));
    QTableWidgetItem *textItem = new QTableWidgetItem(text);
    textItem->setToolTip(text);
    ui->messages->setItem(0, COL_MESSAGE, textItem);
    ui->messages->setItem(0, COL_ERRORS, new QTableWidgetItem(errors));

    while (ui->messages->rowCount() > m_maxRows) {
        ui->messages->removeRow(ui->messages->rowCount() - 1);
    }
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

void RadioPagingDemodGUI::on_flexDeviation_valueChanged(int value)
{
    m_settings.m_flexDeviation = value;
    applySettings(QStringList("flexDeviation"));
}

void RadioPagingDemodGUI::on_gscNonBatterySaver_clicked(bool checked)
{
    m_settings.m_gscNonBatterySaver = checked;
    applySettings(QStringList("gscNonBatterySaver"));
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

void RadioPagingDemodGUI::on_clearTable_clicked()
{
    ui->messages->setRowCount(0);
}

void RadioPagingDemodGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;

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
    m_tickCount(0)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channelrx/demodradiopaging/readme.md";
    RollupContents *rollupContents = getRollupContents();
    ui->setupUi(rollupContents);
    setSizePolicy(rollupContents->sizePolicy());
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));

    m_radioPagingDemod = reinterpret_cast<RadioPagingDemod*>(rxChannel);
    m_radioPagingDemod->setMessageQueueToGUI(getInputMessageQueue());

    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick())); // 50 ms

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
    m_settings.setRollupState(&m_rollupState);

    m_deviceUISet->addChannelMarker(&m_channelMarker);

    connect(&m_channelMarker, SIGNAL(changedByCursor()), this, SLOT(channelMarkerChangedByCursor()));
    connect(&m_channelMarker, SIGNAL(highlightedByCursor()), this, SLOT(channelMarkerHighlightedByCursor()));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    resizeTable();

    displaySettings();
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
    ui->gscNonBatterySaver->setChecked(m_settings.m_gscNonBatterySaver);
    ui->gscVoiceTimeout->setValue(m_settings.m_gscVoiceTimeout);
    ui->gscDeviation->setValue(m_settings.m_gscDeviation);

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

// "<PROTOCOL> HUNT" / "SYNC ..." per protocol, with the number of messages received
void RadioPagingDemodGUI::updateStatus()
{
    RadioPagingDemodSink::Status status = m_radioPagingDemod->getStatus();
    QStringList pocsag;
    bool pocsagSync = false;

    for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++)
    {
        if (status.m_pocsagSync[i])
        {
            pocsagSync = true;
            pocsag.append(QString::number(RadioPagingDemodSettings::m_pocsagSpeeds[i]) + (status.m_pocsagInverted[i] ? " inv" : ""));
        }
    }

    setStatus(ui->pocsagStatus, pocsagSync, "SYNC " + pocsag.join(", "), status.m_messages[0]);
    setStatus(ui->flexStatus, status.m_flexSync,
        QString("SYNC %1%2").arg(status.m_flexMode).arg(status.m_flexInverted ? " inv" : ""), status.m_messages[1]);
    setStatus(ui->gscStatus, status.m_gscSync,
        QString(status.m_gscVoice ? "VOICE" : "SYNC") + (status.m_gscInverted ? " inv" : ""), status.m_messages[2]);
}

void RadioPagingDemodGUI::setStatus(QLabel *label, bool sync, const QString& text, quint32 messages)
{
    label->setText(QString("%1 (%2)").arg(sync ? text : QString("HUNT")).arg(messages));
    label->setStyleSheet(sync ? "QLabel { color: rgb(0, 200, 0); }" : "");
}

void RadioPagingDemodGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &RadioPagingDemodGUI::on_deltaFrequency_changed);
    QObject::connect(ui->rfBW, &QSlider::valueChanged, this, &RadioPagingDemodGUI::on_rfBW_valueChanged);
    QObject::connect(ui->pocsagDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_pocsagDeviation_valueChanged);
    QObject::connect(ui->flexDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_flexDeviation_valueChanged);
    QObject::connect(ui->gscNonBatterySaver, &QCheckBox::clicked, this, &RadioPagingDemodGUI::on_gscNonBatterySaver_clicked);
    QObject::connect(ui->gscVoiceTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_gscVoiceTimeout_valueChanged);
    QObject::connect(ui->gscDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_gscDeviation_valueChanged);
    QObject::connect(ui->clearTable, &QToolButton::clicked, this, &RadioPagingDemodGUI::on_clearTable_clicked);
}

void RadioPagingDemodGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
