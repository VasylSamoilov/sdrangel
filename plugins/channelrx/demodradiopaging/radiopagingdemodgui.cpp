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
#include <QSpinBox>
#include <QVBoxLayout>
#include <QPainter>
#include <QRegularExpression>
#include <QFontDatabase>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTextDocument>

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
#include "maincore.h"

#include "radiopagingdemod.h"
#include "radiopagingvoicestore.h"

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
        m_channelMarker.updateSettings(static_cast<const ChannelMarker*>(m_settings.m_channelMarker));
        displaySettings();
        blockApplySettings(false);
        return true;
    }
    else if (MainCore::MsgMessageRecord::match(message))
    {
        const MainCore::MsgMessageRecord& msg = (const MainCore::MsgMessageRecord&) message;
        ui->messages->addRecord(msg.getRecord());
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
    // FLEX has the network of the last frame on two more lines: always three lines high
    ui->flexStatus->setFixedHeight(flexStatusHeight(ui->flexStatus));
    ui->flexStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_pocsagToolTip = ui->pocsagStatus->toolTip();
    m_gscToolTip = ui->gscStatus->toolTip();
    m_statusClock.start();

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
    QStringList pocsag;
    bool pocsagSync = false;
    int pocsagSpeeds = 0;

    for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++) {
        pocsagSpeeds += status.m_pocsagSync[i] ? 1 : 0;
    }

    for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++)
    {
        if (status.m_pocsagSync[i])
        {
            pocsagSync = true;
            // Several speeds at once: N or I only, so that they fit the field
            QString polarity = polarityHtml(status.m_pocsagInverted[i]);
            if (pocsagSpeeds > 1) {
                polarity = status.m_pocsagInverted[i] ? QString("<span style=\"color:#ffcc44\">I</span>")
                    : QString("<span style=\"color:#88aacc\">N</span>");
            }
            pocsag.append(QString::number(RadioPagingDemodSettings::m_pocsagSpeeds[i]) + (pocsagSpeeds > 1 ? "" : " ") + polarity);
        }
    }

    ui->pocsagStatus->setText(statusLine(status, 0, pocsagSync, "SYNC", pocsag.join(" ")));

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

    // The re-synchronization has no mode or polarity
    if (status.m_flexErs && !status.m_flexSync)
    {
        ui->flexStatus->setText(statusLine(status, 1, true, "ERS",
            QString("%1 s").arg(status.m_flexErsSeconds, 0, 'f', 2)));
    }
    else
    {
        ui->flexStatus->setText(statusLine(status, 1, status.m_flexSync || flexHeld, "SYNC",
            status.m_flexMode + " " + polarityHtml(status.m_flexInverted)));
    }

    if (status.m_enabled[1]) {
        ui->flexStatus->setText(ui->flexStatus->text() + "<br>" + flexNetworkLines(status));
    }
    ui->gscStatus->setText(statusLine(status, 2, status.m_gscSync, status.m_gscVoice ? "VOICE" : "SYNC",
        polarityHtml(status.m_gscInverted)));

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
const int statusStateWidth = 5;     // VOICE
const int statusDetailWidth = 16;   // 512N 1200I 2400N
const int statusQualityWidth = 10;  // BER 12.50%
const int statusOffsetWidth = 12;   // Df -12500 Hz
const int statusMessagesWidth = 8;  // (123456)
const int statusSpacing = 1;
const int statusNetworkWidth = 57;  // FLEX line 3: 4 phases A0.12%, LID 511 CZ 31, CC 1023 TMF 15
const int statusFlexLines = 3;

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

QString RadioPagingDemodGUI::statusLine(const RadioPagingDemodSink::Status& status, int protocol, bool sync,
    const QString& state, const QString& detail)
{
    if (!status.m_enabled[protocol]) {
        return "<span style=\"color:#888888\">OFF</span>";
    }

    QString stateHtml = sync
        ? QString("<b style=\"color:#44cc44\">%1</b>").arg(state)
        : QString("<b style=\"color:#888888\">HUNT</b>");
    QString text = statusField(stateHtml, statusStateWidth)
        + statusField(sync ? detail : QString(), statusDetailWidth)
        + statusField(qualityHtml(status.m_ber[protocol], status.m_berLive[protocol]), statusQualityWidth)
        + statusField(status.m_offsetValid[protocol] ? offsetHtml(status.m_offset[protocol], status.m_berLive[protocol]) : QString(),
            statusOffsetWidth);

    return text + QString("(%1)").arg(status.m_messages[protocol]);
}

// FLEX: two more lines of fixed fields, the network of the last frame. Line 2: cycle and frame, collapse, roaming
// bit and repeats of the frame, the network date, time and time zone (block information words). Line 3: the bit
// error rate of each phase of a frame of several phases, the system ID (local id, coverage zone, country code,
// traffic management flags). A field not received yet is blank; grey once the reception ended
QString RadioPagingDemodGUI::flexNetworkLines(const RadioPagingDemodSink::Status& status)
{
    const RadioPagingFlexDecoder::FrameReport& frame = status.m_flexFrame;
    const RadioPagingFlexDecoder::NetworkTime& t = status.m_flexTime;
    const bool live = status.m_berLive[1];
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
            const RadioPagingBerWindow& ber = status.m_flexPhaseBer[(block.m_phase - 'A') & 3];

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

    return line2 + "<br>" + line3;
}

// The width of the longest status line in the font of the label
int RadioPagingDemodGUI::statusWidth(const QLabel *label)
{
    int chars = statusStateWidth + statusDetailWidth + statusQualityWidth + statusOffsetWidth + statusMessagesWidth
        + 4 * statusSpacing;
    chars = std::max(chars, statusNetworkWidth);
    return label->fontMetrics().horizontalAdvance(QString(chars, QChar('0'))) + 2 * label->margin() + 4;
}

int RadioPagingDemodGUI::flexStatusHeight(const QLabel *label)
{
    return statusFlexLines * label->fontMetrics().lineSpacing() + 2 * label->margin() + 4;
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
}

void RadioPagingDemodGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
