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
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QCheckBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QPainter>
#include <QRegularExpression>
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

// Between the fields of a status line: a gap the line may wrap at (a field itself does not wrap)
const char *statusGap = "&nbsp;&nbsp; ";

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
        // The dial clamps its value to a new range and signals it as a change: the range of a sample rate that is
        // not yet the device's (a configuration being loaded, a file not yet opened) would replace the offset. The
        // offset stays in the settings; the dial shows it within the range
        ui->deltaFrequency->blockSignals(true);
        ui->deltaFrequency->setValueRange(false, 7, -m_basebandSampleRate/2, m_basebandSampleRate/2);
        ui->deltaFrequency->setValue(m_settings.m_inputFrequencyOffset);
        ui->deltaFrequency->blockSignals(false);
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

void RadioPagingDemodGUI::fillCodepageLists()
{
    QComboBox *combos[3] = {ui->pocsagCharset, ui->flexCharset, ui->gscCharset};
    const char *protocols[3] = {"pocsag", "flex", "gsc"};
    const RadioPagingCharset::Key keys[3] = {m_settings.m_pocsagCharset, m_settings.m_flexCharset, m_settings.m_gscCharset};
    const RadioPagingCharset::Key custom = RadioPagingCustomCodepage::key();

    for (int p = 0; p < 3; p++)
    {
        PagingComposer::fillCodepages(combos[p], protocols[p], false);
        QSignalBlocker blocker(combos[p]);
        combos[p]->addItem(tr("Custom"), QStringList({QString::fromStdString(custom.m_vendor), QString::fromStdString(custom.m_model),
            QString::fromStdString(custom.m_codepage)}));
        PagingComposer::selectCodepage(combos[p], keys[p]);
    }
}

// The custom codepage of a protocol: each code of the default table of the protocol with its character, one for one
void RadioPagingDemodGUI::editCustomCodepage(int protocol)
{
    const char *protocols[3] = {"pocsag", "flex", "gsc"};
    const char *names[3] = {"POCSAG", "FLEX", "GSC"};
    const char *codesKeys[3] = {"pocsagCustomCodes", "flexCustomCodes", "gscCustomCodes"};
    const char *charsetKeys[3] = {"pocsagCharset", "flexCharset", "gscCharset"};
    QString *codes[3] = {&m_settings.m_pocsagCustomCodes, &m_settings.m_flexCustomCodes, &m_settings.m_gscCustomCodes};
    RadioPagingCharset::Key *charsets[3] = {&m_settings.m_pocsagCharset, &m_settings.m_flexCharset, &m_settings.m_gscCharset};
    const RadioPagingCharset::Codepage *base = RadioPagingCharset::defaultFor(protocols[protocol]);

    if (!base) {
        return;
    }

    const int size = base->codes();
    RadioPagingCustomCodepage current(protocols[protocol], codes[protocol]->toStdString());
    QDialog dialog(this);
    dialog.setWindowTitle(tr("%1 custom codepage").arg(names[protocol]));
    dialog.resize(420, 560);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *intro = new QLabel(tr("The character a pager shows for each code. ASCII is the start: change the character "
        "(or its Unicode code point in hex) of the codes the pager shows differently. A code without a character is shown as <0xNN>."),
        &dialog);
    intro->setWordWrap(true);
    QTableWidget *table = new QTableWidget(size, 3, &dialog);
    table->setHorizontalHeaderLabels({tr("Code"), tr("Character"), tr("Unicode")});
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    QCheckBox *rtl = new QCheckBox(tr("Right-to-left: the text is shown in the reverse order of the codes"), &dialog);
    QPushButton *ascii = new QPushButton(tr("ASCII"), &dialog);
    ascii->setToolTip(tr("Every code back to its ASCII character"));
    QPushButton *exportButton = new QPushButton(tr("Export..."), &dialog);
    QPushButton *importButton = new QPushButton(tr("Import..."), &dialog);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QHBoxLayout *tools = new QHBoxLayout();
    tools->addWidget(ascii);
    tools->addWidget(exportButton);
    tools->addWidget(importButton);
    tools->addStretch(1);
    layout->addWidget(intro);
    layout->addWidget(table, 1);
    layout->addWidget(rtl);
    layout->addLayout(tools);
    layout->addWidget(buttons);

    // A row: the character and its code point, from one or the other
    auto setRow = [table](int code, const QString& character) {
        QSignalBlocker blocker(table);
        const QVector<uint> points = character.toUcs4();

        if (!table->item(code, 0))
        {
            QTableWidgetItem *codeItem = new QTableWidgetItem(QString("%1").arg(code, 2, 16, QChar('0')).toUpper());
            codeItem->setFlags(codeItem->flags() & ~Qt::ItemIsEditable);
            table->setItem(code, 0, codeItem);
            table->setItem(code, 1, new QTableWidgetItem());
            table->setItem(code, 2, new QTableWidgetItem());
        }

        table->item(code, 1)->setText(character);
        table->item(code, 2)->setText(points.size() == 1 ? QString("%1").arg(points[0], 4, 16, QChar('0')).toUpper() : QString());
    };
    auto show = [&](const RadioPagingCharset::Codepage& codepage) {
        for (int code = 0; code < size; code++)
        {
            const char *glyph = codepage.glyph(code);
            setRow(code, glyph ? QString::fromUtf8(glyph) : QString());
        }

        rtl->setChecked(codepage.m_rtl);
    };

    show(current.codepage());
    table->resizeColumnsToContents();

    connect(table, &QTableWidget::cellChanged, &dialog, [table, setRow](int row, int column) {
        if (column == 1)
        {
            setRow(row, table->item(row, 1)->text());
        }
        else if (column == 2)
        {
            bool ok;
            const uint point = table->item(row, 2)->text().trimmed().toUInt(&ok, 16);
            setRow(row, (ok && (point > 0) && (point <= 0x10FFFF) && ((point < 0xD800) || (point > 0xDFFF)))
                ? QString::fromUcs4(&point, 1) : QString());
        }
    });
    connect(ascii, &QPushButton::clicked, &dialog, [&]() {
        show(*base);
        rtl->setChecked(false);
    });

    // The codes that are not those of ASCII, and the codes that are not one character
    auto collect = [&](RadioPagingCustomCodepage::Codes& mapped, QStringList& wrong) {
        for (int code = 0; code < size; code++)
        {
            const QString character = table->item(code, 1)->text();
            const char *glyph = base->glyph(code);
            const QVector<uint> points = character.toUcs4();

            if (character == (glyph ? QString::fromUtf8(glyph) : QString())) {
                continue;
            }
            if (points.size() != 1) {
                wrong.append(QString("%1").arg(code, 2, 16, QChar('0')).toUpper());
            } else {
                mapped.push_back(std::make_pair(code, (uint32_t) points[0]));
            }
        }
    };

    // The file: every code with its character (none: empty), the protocol and the direction
    connect(exportButton, &QPushButton::clicked, &dialog, [&]() {
        QString fileName = QFileDialog::getSaveFileName(&dialog, tr("Export the custom codepage"), QString("%1_codepage.json").arg(protocols[protocol]),
            tr("JSON (*.json)"));

        if (fileName.isEmpty()) {
            return;
        }

        QJsonObject map;

        for (int code = 0; code < size; code++) {
            map.insert(table->item(code, 0)->text(), table->item(code, 2)->text());
        }

        QJsonObject json;
        json.insert("protocol", protocols[protocol]);
        json.insert("rtl", rtl->isChecked());
        json.insert("codes", map);
        QFile file(fileName);

        if (!file.open(QIODevice::WriteOnly) || (file.write(QJsonDocument(json).toJson()) < 0)) {
            QMessageBox::warning(&dialog, tr("Export"), tr("%1 cannot be written").arg(fileName));
        }
    });
    connect(importButton, &QPushButton::clicked, &dialog, [&]() {
        QString fileName = QFileDialog::getOpenFileName(&dialog, tr("Import a custom codepage"), QString(), tr("JSON (*.json)"));

        if (fileName.isEmpty()) {
            return;
        }

        QFile file(fileName);
        const QJsonObject json = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();

        if (!json.value("codes").isObject())
        {
            QMessageBox::warning(&dialog, tr("Import"), tr("%1 is not a custom codepage").arg(fileName));
            return;
        }
        if (json.value("protocol").toString() != protocols[protocol]) {
            QMessageBox::information(&dialog, tr("Import"), tr("The codepage is of %1: its codes of %2 are taken")
                .arg(json.value("protocol").toString().toUpper(), names[protocol]));
        }

        const QJsonObject map = json.value("codes").toObject();
        show(*base);

        for (int code = 0; code < size; code++)
        {
            const QString key = table->item(code, 0)->text();

            if (map.contains(key))
            {
                bool ok;
                const uint point = map.value(key).toString().toUInt(&ok, 16);
                setRow(code, (ok && (point > 0) && (point <= 0x10FFFF)) ? QString::fromUcs4(&point, 1) : QString());
            }
        }

        rtl->setChecked(json.value("rtl").toBool());
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        RadioPagingCustomCodepage::Codes mapped;
        QStringList wrong;
        collect(mapped, wrong);

        if (!wrong.isEmpty())
        {
            QMessageBox::warning(&dialog, dialog.windowTitle(), tr("A code is one character: %1").arg(wrong.join(" ")));
            return;
        }

        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Saved, and chosen: the codepage of the protocol is its custom codepage
    RadioPagingCustomCodepage::Codes mapped;
    QStringList wrong;
    collect(mapped, wrong);
    *codes[protocol] = QString::fromStdString(RadioPagingCustomCodepage::text(mapped, rtl->isChecked()));
    *charsets[protocol] = RadioPagingCustomCodepage::key();
    displaySettings();
    applySettings(QStringList({codesKeys[protocol], charsetKeys[protocol]}));
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

// The address, or the address of a member of the group of a group message, matches the filter
bool RadioPagingDemodGUI::addressMatchesFilter(const QStringList& addresses) const
{
    QString pattern = ui->filterAddress->text().trimmed();

    if (pattern.isEmpty()) {
        return true;
    }

    // As the address filter of the Pager demodulator: the whole address must match
    QRegularExpression regExp(QRegularExpression::anchoredPattern(pattern));

    if (!regExp.isValid()) {
        return true;
    }

    for (const QString& address : addresses)
    {
        if (regExp.match(address).hasMatch()) {
            return true;
        }
    }

    return false;
}

void RadioPagingDemodGUI::applyAddressFilter()
{
    for (int row = 0; row < ui->messages->rowCount(); row++) {
        ui->messages->setRowHidden(row, !addressMatchesFilter(ui->messages->rowAddresses(row)));
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

    if ((row >= 0) && !addressMatchesFilter(record.addresses())) {
        ui->messages->setRowHidden(row, true);
    }

    if (!m_loadingData) {
        checkNotifications(record);
    }
}

QStringList RadioPagingDemodGUI::matchTexts(const MessageRecord& record, int field)
{
    typedef RadioPagingDemodSettings::NotificationSettings Notification;

    switch (field)
    {
    case Notification::MatchAddress:
        return record.addresses();
    case Notification::MatchMessage:
        return QStringList(record.m_text);
    case Notification::MatchProtocol:
        return QStringList(record.m_protocol);
    case Notification::MatchType:
        return QStringList(record.m_msgType);
    case Notification::MatchChannel:
        return QStringList(record.m_channelAlias);
    case Notification::MatchAny:
        return QStringList((record.addresses() + QStringList({record.m_text, record.m_protocol, record.m_msgType, record.m_channelAlias})).join(' '));
    default:
        return QStringList();
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
        if (!notification->m_regularExpression.isValid()) {
            continue;
        }

        // The first text of the field that matches: on the address, a member of a group message too
        QRegularExpressionMatch matchResult;

        for (const QString& match : matchTexts(record, notification->m_matchField))
        {
            if (!match.isEmpty() && (matchResult = notification->m_regularExpression.match(match)).hasMatch()) {
                break;
            }
        }

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
    ui->messages->setDefaultColumns({PagingMessageTable::ColDate, PagingMessageTable::ColTime, PagingMessageTable::ColProtocol, PagingMessageTable::ColRate,
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

    // The codepages of each protocol and its custom codepage
    fillCodepageLists();

    layoutProtocols();
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

    // Shown without signalling a change. Before the device has given its sample rate (a configuration being loaded)
    // the range may be narrower than the offset: it is widened to show the offset, and set by the next sample rate
    ui->deltaFrequency->blockSignals(true);
    const qint64 halfRange = std::max<qint64>(m_basebandSampleRate / 2, std::abs((qint64) m_settings.m_inputFrequencyOffset));
    ui->deltaFrequency->setValueRange(false, 7, -halfRange, halfRange);
    ui->deltaFrequency->setValue(m_settings.m_inputFrequencyOffset);
    ui->deltaFrequency->blockSignals(false);
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

// A field of a status line kept in one piece: the spaces of its text (not those of its tags) do not break
QString statusField(const QString& html)
{
    static const QRegularExpression parts("(<[^>]*>)|([^<]+)");
    QString out;
    QRegularExpressionMatchIterator it = parts.globalMatch(html);

    while (it.hasNext())
    {
        QRegularExpressionMatch m = it.next();
        out += m.captured(1).isEmpty() ? QString(m.captured(2)).replace(' ', "&nbsp;") : m.captured(1);
    }

    return out;
}

// The fields of a line, those not empty, with the gap between them
QString statusFields(const QStringList& fields)
{
    QStringList shown;

    for (const QString& f : fields)
    {
        if (!f.isEmpty()) {
            shown.append(statusField(f));
        }
    }

    return shown.join(statusGap);
}

} // namespace

QString RadioPagingDemodGUI::groupSlotToolTip(const RadioPagingFlexDecoder::GroupSlot& slot, int pol,
    const RadioPagingDemodSink::Status& status)
{
    static const int shownMembers = 20;
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

// The colours of the status. Grey is what is not active: a reception that ended, a slot that is free
namespace {

const char *colourInactive = "#888888";
const char *colourLive = "#e8e8e8";         // Values of a reception in progress
const char *colourFrame = "#9cc3e6";        // FLEX cycle and frame
const char *colourFlags = "#c7a6e6";        // FLEX collapse, roaming, repeats
const char *colourSystem = "#e6cf9c";       // FLEX system ID
const char *colourMemory = "#9cd6cc";       // What the receivers remember
const char *colourPage = "#e6b89c";         // GSC page: preamble, batch, slot

QString coloured(const QString& text, const QString& colour)
{
    return text.isEmpty() ? text : QString("<span style=\"color:%1\">%2</span>").arg(colour, text.toHtmlEscaped());
}

// A value in its colour while the reception is in progress, grey once it ended
QString value(const QString& text, const char *colour, bool live)
{
    return coloured(text, live ? QString(colour) : QString(colourInactive));
}

QString berColour(const RadioPagingBerWindow& ber, bool live)
{
    return !live ? QString(colourInactive) : (ber.m_uncorrectable > 0) ? QString("#e07a66")
        : (ber.m_errorBits > 0) ? QString("#d9c26a") : QString("#7fc47f");
}

// The colours of the group slots: their state, a white digit on a muted background (free: grey digit)
struct SlotStyle
{
    const char *m_background;
    const char *m_text;
};

SlotStyle slotStyle(RadioPagingFlexDecoder::GroupState state)
{
    switch (state)
    {
    case RadioPagingFlexDecoder::GroupAssigning: return {"#2f6fa8", "#ffffff"};
    case RadioPagingFlexDecoder::GroupWaiting: return {"#7a6522", "#ffffff"};
    case RadioPagingFlexDecoder::GroupDelivering: return {"#1f736e", "#ffffff"};
    case RadioPagingFlexDecoder::GroupDelivered: return {"#2e6e33", "#ffffff"};
    case RadioPagingFlexDecoder::GroupMissed: return {"#87392d", "#ffffff"};
    case RadioPagingFlexDecoder::GroupDiscarded: return {"#5a4785", "#ffffff"};
    case RadioPagingFlexDecoder::GroupTimeout: return {"#8f5523", "#ffffff"};
    default: return {nullptr, "#666666"};
    }
}

} // namespace

// The protocol rows: for each protocol, the live line (button, deviation, DC correction and the status of the network
// being received), the line of its options (labels before their fields, the codepage, the reset button) and the
// network lines of each polarity received, under the button. FLEX adds the group slots of each polarity
void RadioPagingDemodGUI::layoutProtocols()
{
    // The font of the window for the live lines, one point smaller for the network lines
    QFont normal = font();
    double size = normal.pointSizeF();
    QFont small = normal;

    if (size > 1) {
        small.setPointSizeF(size - 1.0);
    }

    // The widgets leave the grid of the form
    QGridLayout *grid = ui->protocolLayout;

    while (QLayoutItem *item = grid->takeAt(0))
    {
        if (item->layout()) {
            item->layout()->setParent(nullptr);
        } else {
            delete item;
        }
    }

    const char *names[3] = {"POCSAG", "FLEX", "GSC"};
    QWidget *enabled[3] = {ui->pocsagEnabled, ui->flexEnabled, ui->gscEnabled};
    QWidget *deviationLabel[3] = {ui->pocsagDeviationLabel, ui->flexDeviationLabel, ui->gscDeviationLabel};
    QWidget *deviation[3] = {ui->pocsagDeviation, ui->flexDeviation, ui->gscDeviation};
    QWidget *dc[3] = {ui->pocsagDcCorrection, ui->flexDcCorrection, ui->gscDcCorrection};
    QLabel *status[3] = {ui->pocsagStatus, ui->flexStatus, ui->gscStatus};
    QComboBox *charset[3] = {ui->pocsagCharset, ui->flexCharset, ui->gscCharset};
    // The network lines are under the fields, after the protocol button
    int indent = 0;

    for (QWidget *button : enabled) {
        indent = std::max(indent, button->sizeHint().width());
    }

    indent += grid->horizontalSpacing() > 0 ? grid->horizontalSpacing() : 4;
    ui->flexMaxMessageNumber->hide();     // 63, as the networks are; the WebAPI sets another

    for (int p = 0; p < 3; p++)
    {
        QWidget *block = new QWidget(ui->settingsContainer);
        QVBoxLayout *column = new QVBoxLayout(block);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(1);

        QHBoxLayout *live = new QHBoxLayout();
        enabled[p]->setFixedWidth(indent - 4);
        live->addWidget(enabled[p]);
        live->addWidget(deviationLabel[p]);
        live->addWidget(deviation[p]);
        live->addWidget(dc[p]);
        status[p]->setFont(normal);
        status[p]->setTextFormat(Qt::RichText);
        // A narrow window wraps the line between its fields; nothing is cut
        status[p]->setWordWrap(true);
        status[p]->setMinimumHeight(0);
        status[p]->setMaximumHeight(QWIDGETSIZE_MAX);
        status[p]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        live->addWidget(status[p], 1);
        column->addLayout(live);

        QHBoxLayout *options = new QHBoxLayout();
        options->addSpacing(indent);

        if (p == 0)
        {
            options->addWidget(new QLabel(tr("Strip"), block));
            options->addWidget(ui->pocsagFill);
        }
        else if (p == 2)
        {
            options->addWidget(new QLabel(tr("Voice"), block));
            options->addLayout(ui->gscVoiceLayout);
        }

        // GSC: its voice controls fill the line, the codepage goes on a line of its own
        QHBoxLayout *codepageLine = options;

        if (p == 2)
        {
            options->addStretch(1);
            codepageLine = new QHBoxLayout();
            codepageLine->addSpacing(indent);
            // Its two codepages, shown whole
            charset[p]->setMaximumWidth(QWIDGETSIZE_MAX);
            charset[p]->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        }

        m_codepageLabel[p] = new QLabel(tr("Codepage"), block);
        codepageLine->addWidget(m_codepageLabel[p]);
        codepageLine->addWidget(charset[p]);

        // The custom codepage of the protocol: its editor
        QToolButton *custom = new QToolButton(block);
        custom->setText(tr("Custom..."));
        custom->setToolTip(tr("Edit the custom codepage of %1: the character of each code and the direction of the text.\n"
            "Choose Custom in the codepages to decode with it").arg(names[p]));
        connect(custom, &QToolButton::clicked, this, [this, p]() { editCustomCodepage(p); });
        codepageLine->addWidget(custom);

        if (p != 2) {
            options->addStretch(1);
        }

        QToolButton *reset = new QToolButton(block);
        reset->setIcon(QIcon(":/recycle.png"));
        reset->setToolTip(tr("Forget what the %1 receivers remember: the messages for duplicate detection%2.\n"
            "Done when the device starts too, so that a file played again is not taken as duplicates")
            .arg(names[p]).arg(p == 1 ? tr(", fragments, groups, message numbers and network time") : QString()));
        connect(reset, &QToolButton::clicked, this, [this, p]() { m_radioPagingDemod->resetProtocol(p); });
        options->addWidget(reset);
        column->addLayout(options);

        if (p == 2)
        {
            codepageLine->addStretch(1);
            column->addLayout(codepageLine);
        }

        for (int pol = 0; pol < 2; pol++)
        {
            m_networkLabel[p][pol] = new QLabel(block);
            m_networkLabel[p][pol]->setFont(small);
            m_networkLabel[p][pol]->setTextFormat(Qt::RichText);
            m_networkLabel[p][pol]->setWordWrap(true);
            m_networkLabel[p][pol]->setContentsMargins(indent, 0, 0, 0);
            m_networkLabel[p][pol]->setVisible(false);
            column->addWidget(m_networkLabel[p][pol]);

            if (p != 1) {
                continue;
            }

            // FLEX: the 16 group slots (temporary addresses) of the network, under its lines
            m_flexGroupBar[pol] = new QWidget(block);
            QHBoxLayout *bar = new QHBoxLayout(m_flexGroupBar[pol]);
            bar->setContentsMargins(indent, 0, 0, 0);
            bar->setSpacing(1);
            QLabel *title = new QLabel("GRP", m_flexGroupBar[pol]);
            title->setFont(small);
            title->setToolTip(tr("Group slots of the %1 network: the 16 temporary addresses (2029568 to 2029583) a short "
                "instruction assigns to pagers for a group message.\nBlue: members being assigned. Amber: waiting for the "
                "group message. Teal: the group message being received. Green: delivered. Red: its frame went by without it. "
                "Violet: assigned again to another frame before it. Orange: the group message did not end.\nGrey: free")
                .arg(pol == 1 ? tr("inverted") : tr("normal")));
            bar->addWidget(title);

            for (int s = 0; s < 16; s++)
            {
                QLabel *cell = new QLabel(QString::number(s, 16).toUpper(), m_flexGroupBar[pol]);
                cell->setFont(small);
                cell->setAlignment(Qt::AlignCenter);
                cell->setFixedWidth(cell->fontMetrics().horizontalAdvance("00") + 4);
                m_flexGroupCell[pol][s] = cell;
                bar->addWidget(cell);
            }

            bar->addStretch(1);
            m_flexGroupBar[pol]->setVisible(false);
            column->addWidget(m_flexGroupBar[pol]);
        }

        grid->addWidget(block, p, 0);
    }
}

// The network being received by a protocol: the polarity in sync, else the one received last
int RadioPagingDemodGUI::currentPolarity(const RadioPagingDemodSink::Status& status, int protocol)
{
    const bool sync[2] = {status.m_pol[protocol][0].m_sync, status.m_pol[protocol][1].m_sync};

    if (sync[0] != sync[1]) {
        m_currentPol[protocol] = sync[1] ? 1 : 0;
    } else if (!sync[0] && (status.m_pol[protocol][0].m_berLive != status.m_pol[protocol][1].m_berLive)) {
        m_currentPol[protocol] = status.m_pol[protocol][1].m_berLive ? 1 : 0;
    } else if (!status.m_pol[protocol][m_currentPol[protocol]].m_seen && status.m_pol[protocol][1 - m_currentPol[protocol]].m_seen) {
        m_currentPol[protocol] = 1 - m_currentPol[protocol];
    }

    return m_currentPol[protocol];
}

// The live line of a protocol: state, mode or speeds, polarity, BER, carrier offset and the messages received
QString RadioPagingDemodGUI::statusLine(const RadioPagingDemodSink::Status& status, int protocol, int pol, bool sync,
    const QString& state, const QString& detail)
{
    if (!status.m_enabled[protocol]) {
        return coloured("OFF", colourInactive);
    }

    const RadioPagingDemodSink::Status::Polarity& s = status.m_pol[protocol][pol];
    const char *stateColour = (state == "VOICE") ? "#e0a050" : (state == "ERS") ? "#e08060" : "#6cc070";
    QString stateHtml = sync ? QString("<b style=\"color:%1\">%2</b>").arg(stateColour, state)
        : QString("<b style=\"color:%1\">HUNT</b>").arg(colourInactive);
    QString ber = s.m_ber.valid()
        ? coloured(QString("BER %1%").arg(100.0 * s.m_ber.ber(), 0, 'f', 2), berColour(s.m_ber, s.m_berLive)) : QString();
    QString offset = s.m_offsetValid
        ? value(QString("%1f %2%3 Hz").arg(QChar(0x394)).arg(s.m_offset < 0.0f ? "-" : "+").arg((int) std::round(std::fabs(s.m_offset))),
            colourLive, s.m_berLive) : QString();
    // The polarity of the network shown once both were received
    bool both = status.m_pol[protocol][0].m_seen && status.m_pol[protocol][1].m_seen;
    QString polarity = (both || (pol == 1)) ? polarityHtml(pol == 1) : QString();

    return statusFields({stateHtml, sync ? value(detail, colourLive, true) : QString(), polarity, ber, offset,
        QString("(%1)").arg(status.m_messages[protocol])});
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

// MEM 12, FLEX also the fragments being reassembled, and the time of the last reset
QString RadioPagingDemodGUI::memoryLine(const RadioPagingDemodSink::Status& status, int protocol, int pol)
{
    if (!status.m_enabled[protocol]) {
        return QString();
    }

    const bool live = status.m_pol[protocol][pol].m_sync || status.m_pol[protocol][pol].m_berLive;
    QStringList fields = {value(QString("MEM %1").arg(status.m_pol[protocol][pol].m_remembered), colourMemory, live)};

    if (protocol == 1) {
        fields.append(value(QString("FRAG %1").arg(status.m_flexReassembliesPol[pol]), colourMemory, live));
    }
    if (status.m_resetTime[protocol] > 0) {
        fields.append(coloured("RESET " + QDateTime::fromMSecsSinceEpoch(status.m_resetTime[protocol]).toString("HH:mm:ss"), colourInactive));
    }

    return statusFields(fields);
}

// FLEX: the network of the last frame of the polarity. Line 1: cycle and frame, collapse, roaming and repeats, the network
// date, time and time zone. Line 2: the system ID and the BER of each phase of a frame of several phases. Only the fields
// received; in colour during the reception, grey once it ended
QString RadioPagingDemodGUI::flexNetworkLines(const RadioPagingDemodSink::Status& status, int pol)
{
    const RadioPagingFlexDecoder::FrameReport& frame = status.m_flexFramePol[pol];
    const RadioPagingFlexDecoder::NetworkTime& t = status.m_flexTimePol[pol];
    const bool live = status.m_pol[1][pol].m_berLive;
    QStringList line1, line2;

    if (frame.m_fiwValid)
    {
        line1.append(value(QString("C%1/F%2").arg(frame.m_cycle, 2, 10, QChar('0')).arg(frame.m_frame, 3, 10, QChar('0')), colourFrame, live));
        QStringList flags;

        if (!frame.m_blocks.empty()) {
            flags.append(QString("c%1").arg(frame.m_blocks.front().m_collapse));
        }
        if (frame.m_roaming) {
            flags.append("R");
        }
        if (frame.m_repeat) {
            flags.append(QString("x%1").arg(frame.m_transmissions));
        }
        if (!flags.isEmpty()) {
            line1.append(value(flags.join(" "), colourFlags, live));
        }
    }

    QStringList when;

    if (t.m_dateValid) {
        when.append(QString("%1-%2-%3").arg(t.m_year).arg(t.m_month, 2, 10, QChar('0')).arg(t.m_day, 2, 10, QChar('0')));
    }
    if (t.m_timeValid)
    {
        when.append(QString("%1:%2:%3").arg(t.m_currentHour, 2, 10, QChar('0')).arg(t.m_currentMinute, 2, 10, QChar('0'))
            .arg((int) t.m_currentSecond, 2, 10, QChar('0')));
    }
    if (t.m_zoneValid)
    {
        // L0 = 0: daylight saving time (STD-43A 3.7.2)
        int minutes = t.m_zoneMinutes;
        QString zone = t.m_zoneDefined
            ? QString("UTC%1%2:%3").arg(minutes < 0 ? "-" : "+").arg(std::abs(minutes) / 60, 2, 10, QChar('0'))
                .arg(std::abs(minutes) % 60, 2, 10, QChar('0'))
            : QString("zone %1").arg(t.m_zone);
        when.append(zone + ((t.m_dst == 0) ? " DST" : ""));
    }
    if (!when.isEmpty()) {
        line1.append(value(when.join(" "), colourLive, live));
    }

    QStringList system;

    if (t.m_ssid1Valid) {
        system.append(QString("LID %1 CZ %2").arg(t.m_localId).arg(t.m_coverageZone));
    }
    if (t.m_ssid2Valid) {
        system.append(QString("CC %1 TMF %2").arg(t.m_country).arg(t.m_tmf));
    }
    if (!system.isEmpty()) {
        line2.append(value(system.join("  "), colourSystem, live));
    }

    // The phases of the last frame: only for a frame of several phases (one phase: the BER of the live line)
    if (frame.m_blocks.size() > 1)
    {
        for (const auto& block : frame.m_blocks)
        {
            const RadioPagingBerWindow& ber = status.m_flexPhaseBerPol[pol][(block.m_phase - 'A') & 3];

            if (ber.valid()) {
                line2.append(coloured(QString("%1 %2%").arg(block.m_phase).arg(100.0 * ber.ber(), 0, 'f', 2), berColour(ber, live)));
            }
        }
    }

    QStringList lines;

    if (!line1.isEmpty()) {
        lines.append(statusFields(line1));
    }
    if (!line2.isEmpty()) {
        lines.append(statusFields(line2));
    }

    return lines.join("<br>");
}

// The live line of each protocol, and the network lines of each polarity received
void RadioPagingDemodGUI::updateStatus()
{
    RadioPagingDemodSink::Status status = m_radioPagingDemod->getStatus();
    bool shown[3][2];

    for (int p = 0; p < 3; p++) {
        shownPolarities(status, p, shown[p]);
    }

    // The network lines of a polarity: its tag when both were received, then its lines; nothing for a protocol off
    auto setNetwork = [&](int p, int pol, const QStringList& lines) {
        QLabel *label = m_networkLabel[p][pol];
        QStringList shownLines;

        for (const QString& line : lines)
        {
            if (!line.isEmpty()) {
                shownLines.append(line);
            }
        }

        bool visible = status.m_enabled[p] && shown[p][pol] && !shownLines.isEmpty();
        label->setVisible(visible);

        if (!visible) {
            return;
        }

        bool both = shown[p][0] && shown[p][1];
        // The polarity before the first line of each network when both were received
        if (both) {
            shownLines[0] = statusField(polarityHtml(pol == 1)) + statusGap + shownLines[0];
        }

        label->setText(shownLines.join("<br>"));
    };

    // POCSAG: the speeds being received on the polarity of the network
    int pol = currentPolarity(status, 0);
    QStringList speeds;

    for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++)
    {
        if (status.m_pocsagSpeedSync[pol][i]) {
            speeds.append(QString::number(RadioPagingDemodSettings::m_pocsagSpeeds[i]));
        }
    }

    ui->pocsagStatus->setText(statusLine(status, 0, pol, status.m_pol[0][pol].m_sync, "SYNC", speeds.join(" ")));

    for (int p = 0; p < 2; p++)
    {
        QString batches = ((p == pol) && (status.m_pocsagBatches > 0))
            ? value(QString("B%1").arg(status.m_pocsagBatches), colourFrame, status.m_pol[0][p].m_berLive) : QString();
        setNetwork(0, p, {batches.isEmpty() ? memoryLine(status, 0, p) : statusField(batches) + statusGap + memoryLine(status, 0, p)});
    }

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
    m_currentPol[1] = flexPol;

    // The re-synchronization has no mode or polarity: it is shown with the network of the last frame
    if (status.m_flexErs && !status.m_flexSync) {
        ui->flexStatus->setText(statusLine(status, 1, flexPol, true, "ERS", QString("%1 s").arg(status.m_flexErsSeconds, 0, 'f', 2)));
    } else {
        ui->flexStatus->setText(statusLine(status, 1, flexPol, status.m_pol[1][flexPol].m_sync || flexHeld, "SYNC",
            status.m_flexModePol[flexPol]));
    }

    for (int p = 0; p < 2; p++) {
        setNetwork(1, p, {flexNetworkLines(status, p), memoryLine(status, 1, p)});
    }

    updateGroupBars(status, shown[1]);

    // GSC: the last page (preamble or non battery saver, batch, slot) with the network it was on
    pol = currentPolarity(status, 2);
    ui->gscStatus->setText(statusLine(status, 2, pol, status.m_pol[2][pol].m_sync, status.m_gscVoicePol[pol] ? "VOICE" : "SYNC", QString()));

    for (int p = 0; p < 2; p++)
    {
        QString page;

        if ((p == pol) && (status.m_messages[2] > 0))
        {
            QStringList fields;
            fields.append(status.m_gscNbs ? QString("NBS") : QString("P%1").arg(status.m_gscPreamble));

            if (status.m_gscBatch) {
                fields.append("BATCH");
            }

            fields.append(QString("S%1").arg(status.m_gscSlot));
            page = value(fields.join(" "), colourPage, status.m_pol[2][p].m_berLive);
        }

        setNetwork(2, p, {page.isEmpty() ? memoryLine(status, 2, p) : statusField(page) + statusGap + memoryLine(status, 2, p)});
    }

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

// FLEX: the group slots of each polarity received, while a slot is in use or has an outcome to show. The colour of a
// slot is its state; its tooltip gives the state, the members and the frame of the group message
void RadioPagingDemodGUI::updateGroupBars(const RadioPagingDemodSink::Status& status, const bool shown[2])
{
    for (int pol = 0; pol < 2; pol++)
    {
        const std::vector<RadioPagingFlexDecoder::GroupSlotState>& states = status.m_flexGroupStatesPol[pol];
        bool any = false;

        for (const auto& state : states) {
            any = any || (state.m_state != RadioPagingFlexDecoder::GroupFree);
        }

        bool visible = status.m_enabled[1] && shown[pol] && any;
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

        for (int s = 0; (s < 16) && (s < (int) states.size()); s++)
        {
            QLabel *cell = m_flexGroupCell[pol][s];
            const RadioPagingFlexDecoder::GroupSlotState& state = states[s];
            SlotStyle style = slotStyle(state.m_state);
            cell->setStyleSheet(style.m_background
                ? QString("QLabel { background-color : %1; color : %2; }").arg(style.m_background, style.m_text)
                : QString("QLabel { color : %1; }").arg(style.m_text));

            QString tip = tr("Group slot %1 (temporary address %2): %3").arg(s).arg(2029568 + s)
                .arg(RadioPagingFlexDecoder::groupStateName(state.m_state));

            if (state.m_state != RadioPagingFlexDecoder::GroupFree)
            {
                tip += tr(", %1 members, group message in frame %2, %3 frames ago").arg(state.m_members)
                    .arg(state.m_targetFrame, 3, 10, QChar('0')).arg(state.m_framesAgo);
            }
            if (inUse[s]) {
                tip += "\n\n" + groupSlotToolTip(*inUse[s], pol, status);
            }

            cell->setToolTip(tip);
        }
    }
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
    // A member of a group: its messages (the group messages it is a member of too)
    QObject::connect(ui->messages, &PagingMessageTable::addressFilterRequested, this, [this](const QString& address) {
        ui->filterAddress->setText(QRegularExpression::escape(address));
    });
    QObject::connect(ui->channel1, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingDemodGUI::on_channel1_currentIndexChanged);
    QObject::connect(ui->channel2, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioPagingDemodGUI::on_channel2_currentIndexChanged);
}

void RadioPagingDemodGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
