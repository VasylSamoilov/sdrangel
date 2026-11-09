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
#include <QPainter>
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
#include "dsp/dspengine.h"
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

namespace {

// Cells with chips and colours keep their HTML in this role; the display text stays plain
// (copy, sort, width)
const int htmlRole = Qt::UserRole + 1;

class HtmlDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QString html = index.data(htmlRole).toString();

        if (html.isEmpty())
        {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        QTextDocument doc;
        doc.setDocumentMargin(1);
        doc.setDefaultFont(opt.font);
        doc.setHtml(html);
        painter->save();
        painter->translate(opt.rect.left(), opt.rect.top() + (opt.rect.height() - doc.size().height()) / 2);
        doc.drawContents(painter, QRectF(0, 0, opt.rect.width(), opt.rect.height()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QString html = index.data(htmlRole).toString();

        if (html.isEmpty()) {
            return QStyledItemDelegate::sizeHint(option, index);
        }

        QTextDocument doc;
        doc.setDocumentMargin(1);
        doc.setDefaultFont(option.font);
        doc.setHtml(html);
        return QSize((int) doc.idealWidth() + 4, std::max((int) doc.size().height(), QStyledItemDelegate::sizeHint(option, index).height()));
    }
};

QString chip(const QString& text, const QString& fg, const QString& bg)
{
    return QString("<span style=\"color:%1;background-color:%2;font-weight:bold\">&nbsp;%3&nbsp;</span>")
        .arg(fg, bg, text.toHtmlEscaped());
}

// Type chips
QString typeChip(const QString& type)
{
    static const QMap<QString, QPair<QString, QString>> labels = {
        {"alphanumeric", {"ALPHA", "#a5d6a7|#1b5e20"}},
        {"numeric", {"NUM", "#90caf9|#1a237e"}},
        {"tone_only", {"TONE", "#ce93d8|#4a148c"}},
        {"tone", {"TONE", "#ce93d8|#4a148c"}},
        {"voice", {"VOICE", "#81d4fa|#01579b"}},
        {"secure", {"SEC", "#cfd8dc|#37474f"}},
        {"special_numeric", {"SPEC", "#90caf9|#1a237e"}},
        {"numbered_numeric", {"NNUM", "#90caf9|#1a237e"}},
        {"numbered_special_numeric", {"NSPEC", "#90caf9|#1a237e"}},
        {"binary", {"BIN", "#cfd8dc|#37474f"}},
        {"short_message", {"SMSG", "#cfd8dc|#37474f"}},
        {"short_instruction", {"SI", "#cfd8dc|#37474f"}},
        {"network", {"NET", "#cfd8dc|#37474f"}}
    };
    auto it = labels.find(type);
    QString label = (it != labels.end()) ? it->first : type.toUpper();
    QStringList colours = ((it != labels.end()) ? it->second : QString("#cfd8dc|#37474f")).split('|');
    return chip(label, colours[0], colours[1]);
}

QString infoChip(const QString& text)
{
    return chip(text, "#b39ddb", "#2e2e2e");
}

QString checkChip(const QString& name, const QString& check)
{
    if (check == "ok") {
        return chip(name + ":ok", "#66bb6a", "#2e2e2e");
    } else if (check == "failed") {
        return chip(name + ":bad", "#ff5252", "#2e2e2e");
    } else if (check == "incomplete") {
        return chip(name + ":?", "#ffca28", "#2e2e2e");
    }
    return QString();
}

} // namespace

// Size the columns using dummy data
void RadioPagingDemodGUI::resizeTable()
{
    int row = ui->messages->rowCount();
    ui->messages->setRowCount(row + 1);
    ui->messages->setItem(row, COL_TIME, new QTableWidgetItem("00:00:00"));
    ui->messages->setItem(row, COL_PROTOCOL, new QTableWidgetItem("POCSAG"));
    ui->messages->setItem(row, COL_RATE, new QTableWidgetItem("3200/4"));
    ui->messages->setItem(row, COL_POLARITY, new QTableWidgetItem("I"));
    ui->messages->setItem(row, COL_ADDRESS, new QTableWidgetItem("0001234567"));
    ui->messages->setItem(row, COL_TYPE, new QTableWidgetItem("VOICE"));
    ui->messages->setItem(row, COL_MESSAGE, new QTableWidgetItem("ABCEDGHIJKLMNOPQRSTUVWXYZABCDEFGHIJ"));
    ui->messages->setItem(row, COL_FRAGMENT, new QTableWidgetItem("REASM 99"));
    ui->messages->setItem(row, COL_INFO, new QTableWidgetItem("PH:A C03/F031 #99 R MD K:ok S:ok"));
    ui->messages->setItem(row, COL_ERRORS, new QTableWidgetItem("X99"));
    ui->messages->resizeColumnsToContents();
    ui->messages->removeRow(row);
}

// Chips of the protocol fields of a record, plain text and HTML
void RadioPagingDemodGUI::recordInfo(const MessageRecord& record, QString& plain, QString& html, QString& tip)
{
    const QVariantMap& d = record.m_protocolData;
    QStringList items;
    QStringList chips;
    auto add = [&](const QString& text, const QString& chipHtml = QString()) {
        items.append(text);
        chips.append(chipHtml.isEmpty() ? infoChip(text) : chipHtml);
    };

    if (record.m_protocol == "pocsag")
    {
        add(QString("B:%1").arg(d.value("batch").toInt() + 1));
        if (d.value("truncated").toBool()) {
            add("TRUNC");
        }
    }
    else if (record.m_protocol == "flex")
    {
        add(QString("PH:%1 C%2/F%3").arg(d.value("phase").toString())
            .arg(d.value("cycle").toInt(), 2, 10, QChar('0')).arg(d.value("frame").toInt(), 3, 10, QChar('0')));
        if (d.value("message_number").toInt() >= 0) {
            add(QString("#%1").arg(d.value("message_number").toInt()));
        }
        if (d.value("retrieval").toBool()) {
            add("R");
        }
        if (d.value("maildrop").toBool()) {
            add("MD");
        }
        if (d.value("priority").toBool()) {
            add("PRI");
        }

        QString addrType = d.value("addr_type").toString();
        if (addrType == "long") {
            add("LONG");
        } else if ((addrType != "short") && (addrType != "temporary") && !addrType.isEmpty()) {
            add(addrType.toUpper(), chip(addrType.toUpper(), "#ce93d8", "#2e2e2e"));
        }

        if (d.contains("group_slot"))
        {
            QStringList members = d.value("group_capcodes").toStringList();
            bool tracked = d.value("group_tracked").toBool();
            QString g = QString("G%1[%2]").arg(d.value("group_slot").toInt()).arg(tracked ? QString::number(members.size()) : QString("?"));
            add(g, chip(g, "#80cbc4", "#103a35"));
            tip = tracked ? QString("G%1 members: %2").arg(d.value("group_slot").toInt()).arg(members.join(", "))
                          : QString("G%1: the group assignment was not received").arg(d.value("group_slot").toInt());
        }

        QString k = checkChip("K", d.value("checksum").toString());
        if (!k.isEmpty()) {
            add(QString("K:%1").arg(d.value("checksum").toString()), k);
        }
        QString s = checkChip("S", d.value("signature").toString());
        if (!s.isEmpty()) {
            add(QString("S:%1").arg(d.value("signature").toString()), s);
        }
    }
    else if (record.m_protocol == "gsc")
    {
        if (d.value("non_battery_saver").toBool()) {
            add("NBS");
        } else if (d.value("batch").toBool()) {
            add("BATCH");
        }
        add(QString("S:%1").arg(d.value("slot").toInt()));
        if (d.contains("checksum_passed")) {
            add(d.value("checksum_passed").toBool() ? "CK:ok" : "CK:bad",
                checkChip("CK", d.value("checksum_passed").toBool() ? "ok" : "failed"));
        }
        if (d.value("interleaved").toBool()) {
            add("INTL");
        }
        if (d.value("truncated").toBool()) {
            add("TRUNC");
        }
    }

    if (record.m_duplicate == MessageRecord::DuplicateDup) {
        add("DUP", chip("DUP", "#bbbbbb", "#3a3a3a"));
    } else if (record.m_duplicate == MessageRecord::DuplicateDupPlus) {
        add("DUP+", chip("DUP+", "#66bb6a", "#3a3a3a"));
    }

    plain = items.join(" ");
    html = chips.join(" ");
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

// Newest message at the bottom, the view follows while it is at the bottom. The end of a voice
// message updates the row of its voice page. Duplicates are greyed
void RadioPagingDemodGUI::recordReceived(const MessageRecord& record)
{
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
                QTableWidgetItem *messageItem = ui->messages->item(row, COL_MESSAGE);
                messageItem->setText(text);
                messageItem->setToolTip(text);
                if (!messageItem->data(htmlRole).toString().isEmpty()) {
                    messageItem->setData(htmlRole, text.toHtmlEscaped());
                }
                return;
            }
        }
    }

    const QVariantMap& d = record.m_protocolData;
    bool duplicate = record.m_duplicate != MessageRecord::DuplicateNone;
    QScrollBar *sb = ui->messages->verticalScrollBar();
    bool atBottom = sb->value() == sb->maximum();
    int row = ui->messages->rowCount();
    ui->messages->setRowCount(row + 1);

    // Duplicates are plain grey text, the others keep their chips
    auto cell = [&](int col, const QString& plain, const QString& html = QString(), const QString& tip = QString()) {
        QTableWidgetItem *item = new QTableWidgetItem(plain);
        if (duplicate) {
            item->setForeground(QColor("#777777"));
        } else if (!html.isEmpty()) {
            item->setData(htmlRole, html);
        }
        if (!tip.isEmpty()) {
            item->setToolTip(tip);
        }
        ui->messages->setItem(row, col, item);
        return item;
    };

    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(record.m_timestamp);
    QTableWidgetItem *timeItem = cell(COL_TIME, dateTime.toString("HH:mm:ss"), QString(), dateTime.toString(Qt::ISODate));
    timeItem->setData(Qt::UserRole, record.m_id);
    cell(COL_PROTOCOL, record.m_protocol.toUpper());
    cell(COL_RATE, record.m_mode);

    bool inverted = record.m_polarity == MessageRecord::PolarityInverted;
    cell(COL_POLARITY, inverted ? "I" : "N",
        inverted ? chip("I", "#2a1f00", "#ffcc44") : chip("N", "#0d1a2a", "#88aacc"),
        inverted ? tr("Inverted polarity") : tr("Normal polarity"));
    cell(COL_ADDRESS, record.m_address);
    cell(COL_TYPE, record.m_msgType, typeChip(record.m_msgType), record.m_msgType);

    // Message, with the alternative decode in grey italics
    QString message = text;
    if (message.isEmpty() && record.m_msgType.startsWith("tone")) {
        message = tr("(tone only)");
    }
    QString messageHtml = message.toHtmlEscaped();
    QString messageTip = text;
    QString altText = d.value("alt_text").toString();
    if ((record.m_rawBits > 0) && !altText.isEmpty())
    {
        QString altType = d.value("alt_type").toString();
        messageHtml += QString(" <i style=\"color:#888888\">[%1: %2]</i>").arg(altType.toHtmlEscaped(), altText.toHtmlEscaped());
        messageTip += QString("\n%1: %2").arg(altType, altText);
    }
    cell(COL_MESSAGE, message, messageHtml, messageTip);

    // Fragment: the n-th fragment received, or the reassembled message of n fragments
    if (d.value("is_fragment").toBool())
    {
        QString f = QString::number(d.value("fragment_count").toInt());
        cell(COL_FRAGMENT, f, chip(f, "#3a2a00", "#ffb74d"), tr("Fragment %1 received").arg(f));
    }
    else if (d.value("fragmented").toBool())
    {
        int n = d.value("fragment_count").toInt();
        bool complete = d.value("complete").toBool();
        QString f = QString("%1 %2").arg(complete ? "REASM" : "INCOMPLETE").arg(n);
        cell(COL_FRAGMENT, f, complete ? chip(f, "#0d2a3a", "#4fc3f7") : chip(f, "#2a1000", "#ff8a65"),
            complete ? tr("Reassembled from %1 fragments").arg(n)
                     : tr("%1 fragments, fragments missing or timed out").arg(n));
    }
    else
    {
        cell(COL_FRAGMENT, QString());
    }

    QString infoPlain, infoHtml, infoTip;
    recordInfo(record, infoPlain, infoHtml, infoTip);
    cell(COL_INFO, infoPlain, infoHtml, infoTip.isEmpty() ? infoPlain : infoTip);

    // Errors: X uncorrectable codewords, ~ corrected bits
    QString errors, errorsHtml;
    if (record.m_errorsUncorrectable > 0)
    {
        errors = QString("X%1").arg(record.m_errorsUncorrectable);
        errorsHtml = QString("<span style=\"color:#ff5252;font-weight:bold\">%1</span>").arg(errors);
    }
    else if (record.m_errorsCorrected > 0)
    {
        errors = QString("~%1").arg(record.m_errorsCorrected);
        errorsHtml = QString("<span style=\"color:#ffca28\">%1</span>").arg(errors);
    }
    else
    {
        errors = "ok";
        errorsHtml = "<span style=\"color:#66bb6a\">ok</span>";
    }
    cell(COL_ERRORS, errors, errorsHtml,
        tr("%1 bit errors corrected, %2 codewords uncorrectable").arg(record.m_errorsCorrected).arg(record.m_errorsUncorrectable));

    while (ui->messages->rowCount() > m_maxRows) {
        ui->messages->removeRow(0);
    }

    if (atBottom) {
        ui->messages->scrollToBottom();
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

    ui->messages->setItemDelegate(new HtmlDelegate(ui->messages));

    CRightClickEnabler *audioRightClickEnabler = new CRightClickEnabler(ui->gscAudio);
    connect(audioRightClickEnabler, SIGNAL(rightClick(const QPoint &)), this, SLOT(audioSelect(const QPoint &)));
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
    ui->pocsagEnabled->setChecked(m_settings.m_pocsagEnabled);
    ui->flexEnabled->setChecked(m_settings.m_flexEnabled);
    ui->gscEnabled->setChecked(m_settings.m_gscEnabled);
    ui->gscVoiceTimeout->setValue(m_settings.m_gscVoiceTimeout);
    ui->gscDeviation->setValue(m_settings.m_gscDeviation);
    ui->gscAudio->setChecked(m_settings.m_gscAudio);
    ui->volume->setValue(std::round(m_settings.m_volume * 100.0f));
    ui->volumeText->setText(QString::number(ui->volume->value()));

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

// Per protocol: SYNC (green) or HUNT (grey), what is received and its polarity, the
// reception quality and the number of messages
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
            pocsag.append(QString::number(RadioPagingDemodSettings::m_pocsagSpeeds[i]) + " " + polarityHtml(status.m_pocsagInverted[i]));
        }
    }

    setStatus(ui->pocsagStatus, status, 0, pocsagSync, "SYNC", pocsag.join(", "));
    setStatus(ui->flexStatus, status, 1, status.m_flexSync, "SYNC",
        status.m_flexMode + " " + polarityHtml(status.m_flexInverted));
    setStatus(ui->gscStatus, status, 2, status.m_gscSync, status.m_gscVoice ? "VOICE" : "SYNC",
        polarityHtml(status.m_gscInverted));

    if (status.m_enabled[0] && (status.m_pocsagBatches > 0)) {
        ui->pocsagStatus->setText(ui->pocsagStatus->text() + QString("<br>batches %1").arg(status.m_pocsagBatches));
    }
    if (status.m_enabled[1]) {
        ui->flexStatus->setText(ui->flexStatus->text() + flexDetails(status));
    }
    if (status.m_enabled[2] && (status.m_messages[2] > 0))
    {
        QString last = status.m_gscNbs ? QString("non battery saver")
            : QString("preamble %1%2").arg(status.m_gscPreamble).arg(status.m_gscBatch ? ", batch" : "");
        ui->gscStatus->setText(ui->gscStatus->text() + QString("<br>last page: %1, slot %2").arg(last).arg(status.m_gscSlot));
    }

    ui->flexStatus->setToolTip(flexToolTip(status));

    // Green while the voice is played
    bool playing = m_settings.m_gscAudio && status.m_enabled[2] && status.m_gscVoice;
    ui->gscAudio->setStyleSheet(playing ? "QToolButton { background-color : green; }" : "");
}

// Extended FLEX status of the network of the last frame: frame, network time and
// identification (once received several times the same), groups and reassemblies
QString RadioPagingDemodGUI::flexDetails(const RadioPagingDemodSink::Status& status)
{
    const RadioPagingFlexDecoder::FrameReport& frame = status.m_flexFrame;
    const RadioPagingFlexDecoder::NetworkTime& t = status.m_flexTime;
    QString text;

    if (frame.m_fiwValid)
    {
        text += QString("<br>C%1/F%2").arg(frame.m_cycle, 2, 10, QChar('0')).arg(frame.m_frame, 3, 10, QChar('0'));
        if (!frame.m_blocks.empty()) {
            text += QString(" collapse %1").arg(frame.m_blocks.front().m_collapse);
        }
        if (frame.m_roaming) {
            text += " roaming";
        }
        if (frame.m_repeat) {
            text += QString(" repeat x%1").arg(frame.m_transmissions);
        }
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
        int minutes = t.m_zoneMinutes;
        network.append(QString("UTC%1%2:%3 %4").arg(minutes < 0 ? "-" : "+").arg(std::abs(minutes) / 60)
            .arg(std::abs(minutes) % 60, 2, 10, QChar('0')).arg(t.m_dst == 0 ? "dst" : "std"));
    }
    if (t.m_ssid1Valid) {
        network.append(QString("LID %1 coverage %2").arg(t.m_localId).arg(t.m_coverageZone));
    }
    if (t.m_ssid2Valid) {
        network.append(QString("country %1 TMF %2").arg(t.m_country).arg(t.m_tmf));
    }
    if (!network.isEmpty()) {
        text += "<br>" + network.join("&nbsp;&nbsp;");
    }

    QStringList groups;

    for (const auto& g : status.m_flexGroups) {
        groups.append(QString("G%1[%2]&gt;F%3").arg(g.m_slot).arg(g.m_members.size()).arg(g.m_targetFrame, 3, 10, QChar('0')));
    }
    if (!groups.isEmpty() || (status.m_flexReassemblies > 0))
    {
        text += QString("<br>groups %1/16 %2&nbsp;&nbsp;reasm %3/%4").arg(status.m_flexGroups.size()).arg(groups.join(" "))
            .arg(status.m_flexReassemblies).arg(RadioPagingFlexDecoder::m_reassemblySlots);
    }

    return text;
}

QString RadioPagingDemodGUI::flexToolTip(const RadioPagingDemodSink::Status& status)
{
    QString tip = tr("FLEX state: SYNC or HUNT, mode and polarity of the frame, quality (100% minus the bit error rate: "
        "green no errors, yellow errors corrected, red errors that could not be corrected, grey after the reception) "
        "and messages received.\n"
        "Cxx/Fxxx: cycle and frame of the last frame. Collapse n: pagers wake up every 2^n frames. "
        "Roaming: roaming bit of the frame. Repeat xN: the frame content is sent N times.\n"
        "Date, time and zone: network time from the block information words. LID: local id, coverage: coverage zone, "
        "country: country code, TMF: traffic management flags (which frames this transmitter sends).\n"
        "Groups: temporary addresses in use, Gs[members]>F target frame of the group message. "
        "Reasm: fragmented messages being reassembled.");

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

// Quality is 100% minus the bit error rate. Green: no bit errors, yellow: errors the code
// corrected, light red: errors the code could not correct. Grey once the reception ended
QString RadioPagingDemodGUI::qualityHtml(const RadioPagingBerWindow& ber, bool live)
{
    if (!ber.valid()) {
        return QString();
    }

    QString colour = !live ? "#888888" : (ber.m_uncorrectable > 0) ? "#ff6644" : (ber.m_errorBits > 0) ? "#ffcc44" : "#44cc44";

    return QString("<span style=\"color:%1\">Q %2%</span>").arg(colour).arg(100.0 * (1.0 - ber.ber()), 0, 'f', 1);
}

void RadioPagingDemodGUI::setStatus(QLabel *label, const RadioPagingDemodSink::Status& status, int protocol, bool sync,
    const QString& state, const QString& detail)
{
    if (!status.m_enabled[protocol])
    {
        label->setText("<span style=\"color:#888888\">OFF</span>");
        return;
    }

    QString text = sync
        ? QString("<b style=\"color:#44cc44\">%1</b> %2").arg(state, detail)
        : QString("<b style=\"color:#888888\">HUNT</b>");
    QString quality = qualityHtml(status.m_ber[protocol], status.m_berLive[protocol]);

    if (!quality.isEmpty()) {
        text += "&nbsp;&nbsp;" + quality;
    }

    label->setText(text + QString("&nbsp;&nbsp;(%1)").arg(status.m_messages[protocol]));
}

void RadioPagingDemodGUI::makeUIConnections()
{
    QObject::connect(ui->deltaFrequency, &ValueDialZ::changed, this, &RadioPagingDemodGUI::on_deltaFrequency_changed);
    QObject::connect(ui->rfBW, &QSlider::valueChanged, this, &RadioPagingDemodGUI::on_rfBW_valueChanged);
    QObject::connect(ui->pocsagDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_pocsagDeviation_valueChanged);
    QObject::connect(ui->flexDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_flexDeviation_valueChanged);
    QObject::connect(ui->pocsagEnabled, &QCheckBox::clicked, this, &RadioPagingDemodGUI::on_pocsagEnabled_clicked);
    QObject::connect(ui->flexEnabled, &QCheckBox::clicked, this, &RadioPagingDemodGUI::on_flexEnabled_clicked);
    QObject::connect(ui->gscEnabled, &QCheckBox::clicked, this, &RadioPagingDemodGUI::on_gscEnabled_clicked);
    QObject::connect(ui->gscVoiceTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_gscVoiceTimeout_valueChanged);
    QObject::connect(ui->gscDeviation, QOverload<int>::of(&QSpinBox::valueChanged), this, &RadioPagingDemodGUI::on_gscDeviation_valueChanged);
    QObject::connect(ui->gscAudio, &QToolButton::toggled, this, &RadioPagingDemodGUI::on_gscAudio_toggled);
    QObject::connect(ui->volume, &QDial::valueChanged, this, &RadioPagingDemodGUI::on_volume_valueChanged);
    QObject::connect(ui->clearTable, &QToolButton::clicked, this, &RadioPagingDemodGUI::on_clearTable_clicked);
}

void RadioPagingDemodGUI::updateAbsoluteCenterFrequency()
{
    setStatusFrequency(m_deviceCenterFrequency + m_settings.m_inputFrequencyOffset);
}
