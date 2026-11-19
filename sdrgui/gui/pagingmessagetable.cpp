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

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QHelpEvent>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "dsp/dspengine.h"
#include "feature/feature.h"
#include "feature/featuregui.h"
#include "feature/featurewebapiutils.h"
#include "gui/audioselectdialog.h"
#include "gui/dialogpositioner.h"
#include "radiopagingpage.h"
#include "radiopagingrecode.h"
#include "pagingvoiceplayer.h"
#include "pagingmessagetable.h"

namespace {

// The cells with chips and colours keep their HTML in this role; the display text stays plain (copy, width)
const int htmlRole = Qt::UserRole + 1;
const int voiceFileRole = Qt::UserRole + 2;     // On the time item of a voice message: the file recorded
const int jsonRole = Qt::UserRole + 3;          // On the time item: the record as JSON
const int tipRole = Qt::UserRole + 4;           // On the message item: its tool tip without the voice file
const int chipTextsRole = Qt::UserRole + 5;     // On the info item: the label of each chip
const int chipTipsRole = Qt::UserRole + 6;      // On the info item: the tool tip of each chip

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

        // The text beside the icon, if the cell has one. The message wraps to the width of its column
        QRect textRect = opt.widget->style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        QTextDocument doc;
        doc.setDocumentMargin(1);
        doc.setDefaultFont(opt.font);
        doc.setHtml(html);
        if (index.column() == PagingMessageTable::ColMessage) {
            doc.setTextWidth(textRect.width());
        }
        painter->save();
        painter->translate(textRect.left(), opt.rect.top() + (opt.rect.height() - doc.size().height()) / 2);
        doc.drawContents(painter, QRectF(0, 0, textRect.width(), opt.rect.height()));
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
        int width = (int) doc.idealWidth() + 4;

        // The message: as high as its text wrapped to the width of its column
        const QTableView *view = qobject_cast<const QTableView *>(parent());
        if (view && (index.column() == PagingMessageTable::ColMessage)) {
            doc.setTextWidth(std::max(20, view->columnWidth(index.column()) - 8));
        }

        return QSize(width, std::max((int) doc.size().height(), QStyledItemDelegate::sizeHint(option, index).height()));
    }
};

QString chip(const QString& text, const QString& fg, const QString& bg)
{
    return QString("<span style=\"color:%1;background-color:%2;font-weight:bold\">&nbsp;%3&nbsp;</span>")
        .arg(fg, bg, text.toHtmlEscaped());
}

QString typeChip(const QString& type)
{
    static const QMap<QString, QPair<QString, QString>> labels = {
        {"alphanumeric", {"ALPHA", "#a5d6a7|#1b5e20"}},
        {"numeric", {"NUM", "#90caf9|#1a237e"}},
        {"tone", {"TONE", "#ce93d8|#4a148c"}},
        {"voice", {"VOICE", "#81d4fa|#01579b"}},
        {"secure", {"SEC", "#cfd8dc|#37474f"}},
        {"special_numeric", {"SPEC", "#90caf9|#1a237e"}},
        {"numbered_numeric", {"NNUM", "#90caf9|#1a237e"}},
        {"numbered_special_numeric", {"NSPEC", "#90caf9|#1a237e"}},
        {"binary", {"BIN", "#cfd8dc|#37474f"}},
        {"short_message", {"SMSG", "#cfd8dc|#37474f"}},
        {"short_instruction", {"SI", "#cfd8dc|#37474f"}}
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

const char *headers[PagingMessageTable::ColumnCount] = {
    "Time", "Dir", "Channel", "Proto", "Rate", "Pol", "Address", "Type", "Message", "Fragment", "Info", "Errors", "State", "Date"
};

} // namespace

PagingMessageTable::PagingMessageTable(QWidget *parent) :
    QTableWidget(0, ColumnCount, parent),
    m_columnMenu(new QMenu(this)),
    m_player(new PagingVoicePlayer(this))
{
    QStringList labels;

    for (int i = 0; i < ColumnCount; i++) {
        labels.append(headers[i]);
    }

    setHorizontalHeaderLabels(labels);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setItemDelegate(new HtmlDelegate(this));
    // Row numbers, as in the Pager demodulator
    verticalHeader()->setVisible(true);
    verticalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    horizontalHeader()->setSectionsMovable(true);
    // The date before the time
    horizontalHeader()->moveSection(horizontalHeader()->visualIndex(ColDate), horizontalHeader()->visualIndex(ColTime));
    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    setContextMenuPolicy(Qt::CustomContextMenu);

    connect(horizontalHeader(), &QHeaderView::customContextMenuRequested, this, &PagingMessageTable::showColumnMenu);
    connect(horizontalHeader(), &QHeaderView::sectionMoved, this, &PagingMessageTable::columnsChanged);
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, &PagingMessageTable::columnsChanged);
    // The messages wrap to the width of their column: the rows follow it
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logicalIndex, int, int) {
        if (logicalIndex == ColMessage) {
            resizeRowsToContents();
        }
    });
    connect(this, &QWidget::customContextMenuRequested, this, &PagingMessageTable::showRowMenu);
    connect(this, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { showDetails(row); });
    connect(m_player, &PagingVoicePlayer::stateChanged, this, &PagingMessageTable::displayPlayer);

    m_defaultColumns = {ColDate, ColTime, ColProtocol, ColAddress, ColType, ColMessage, ColInfo};
    setDefaultColumns(m_defaultColumns);
}

void PagingMessageTable::setDefaultColumns(const QList<Column>& shown)
{
    m_defaultColumns = shown;

    for (int i = 0; i < ColumnCount; i++) {
        setColumnHidden(i, !shown.contains((Column) i));
    }
}

QByteArray PagingMessageTable::columns() const
{
    return horizontalHeader()->saveState();
}

bool PagingMessageTable::setColumns(const QByteArray& state)
{
    if (state.isEmpty()) {
        return false;
    }

    QSignalBlocker blocker(horizontalHeader());
    return horizontalHeader()->restoreState(state);
}

// The columns sized to the usual content: a row of the longest values, taken out again
void PagingMessageTable::sizeColumns()
{
    int row = rowCount();
    setRowCount(row + 1);

    const char *cells[ColumnCount] = {
        "00:00:00", "TX", "Channel alias", "POCSAG", "3200/4", "I", "0001234567A", "VOICE",
        "ABCEDGHIJKLMNOPQRSTUVWXYZABCDEFGHIJ", "REASM 99", "PH:A C:14 F:127 N:64 R MD K:ok S:ok", "X99", "transmitted",
        "Wed Sep 30 2026"
    };

    for (int c = 0; c < ColumnCount; c++) {
        setItem(row, c, new QTableWidgetItem(cells[c]));
    }

    // The info column is as wide as its chips are drawn (bold, with padding), not as their plain text
    QStringList infoChips;
    for (const QString& t : QString(cells[ColInfo]).split(' ')) {
        infoChips.append(infoChip(t));
    }
    item(row, ColInfo)->setData(htmlRole, infoChips.join(" "));

    QSignalBlocker blocker(horizontalHeader());
    resizeColumnsToContents();
    removeRow(row);
}

int PagingMessageTable::findRow(const QString& id) const
{
    if (id.isEmpty()) {
        return -1;
    }

    for (int row = rowCount() - 1; row >= 0; row--)
    {
        QTableWidgetItem *item = this->item(row, ColTime);

        if (item && (item->data(Qt::UserRole).toString() == id)) {
            return row;
        }
    }

    return -1;
}

void PagingMessageTable::setRowBackground(const QString& id, const QColor& color)
{
    int row = findRow(id);

    if (row < 0) {
        return;
    }

    for (int col = 0; col < columnCount(); col++)
    {
        if (QTableWidgetItem *cellItem = item(row, col)) {
            cellItem->setBackground(QBrush(color));
        }
    }
}

QString PagingMessageTable::selectedId() const
{
    int row = currentRow();
    return ((row >= 0) && item(row, ColTime)) ? item(row, ColTime)->data(Qt::UserRole).toString() : QString();
}

QStringList PagingMessageTable::rowAddresses(int row) const
{
    MessageRecord record;
    const QTableWidgetItem *timeItem = item(row, ColTime);

    if (!timeItem || !MessageRecord::fromJson(QJsonDocument::fromJson(timeItem->data(jsonRole).toString().toUtf8()).object(), record))
    {
        const QTableWidgetItem *address = item(row, ColAddress);
        return address ? QStringList(address->text()) : QStringList();
    }

    return record.addresses();
}

void PagingMessageTable::selectId(const QString& id)
{
    int row = findRow(id);

    if (row >= 0) {
        selectRow(row);
    }
}

QString PagingMessageTable::recordText(const MessageRecord& record)
{
    const QVariantMap& d = record.m_protocolData;

    if (d.contains("voice_end"))
    {
        QString text = QString("voice ended: %1 after %2 s").arg(d.value("voice_end").toString())
            .arg(d.value("voice_seconds").toDouble(), 0, 'f', 1);
        return text;
    }
    if (record.m_text.isEmpty() && (record.m_msgType == "voice"))
    {
        // The voice sent, recorded by its transmitter
        return d.contains("voice_seconds") ? QString("voice, %1 s").arg(d.value("voice_seconds").toDouble(), 0, 'f', 1)
            : QString("(voice)");
    }
    if (record.m_text.isEmpty() && ((record.m_msgType == "tone") || d.value("tone_only").toBool())) {
        return QString("(tone only)");
    }
    // A FLEX short instruction: what it tells the pager of the address
    if (record.m_text.isEmpty() && d.contains("instruction_type"))
    {
        const int type = d.value("instruction_type").toInt();

        if (type == 0)
        {
            const int slot = d.value("instruction_slot").toInt();
            return QString("(added to group slot %1, temporary address %2, group message in frame %3)")
                .arg(slot).arg(2029568 + slot).arg(d.value("instruction_frame").toInt(), 3, 10, QChar('0'));
        }
        if (type == 1) {
            return QString("(system event, flags %1)").arg(d.value("event_flags").toUInt(), 3, 16, QChar('0'));
        }

        return QString("(short instruction type %1)").arg(type);
    }

    return record.m_text;
}

QString PagingMessageTable::statusHtml(const QString& text, const QString& charStatus)
{
    if (charStatus.size() != text.size()) {
        return text.toHtmlEscaped();
    }

    QString html;
    int i = 0;

    while (i < text.size())
    {
        const QChar s = charStatus[i];
        int j = i;

        while ((j < text.size()) && (charStatus[j] == s)) {
            j++;
        }

        const QString run = text.mid(i, j - i).toHtmlEscaped();

        if (s == 'X') {
            html += QString("<span style=\"color:#ff5252;font-weight:bold;background-color:rgba(255,82,82,0.18)\">%1</span>").arg(run);
        } else if (s == 'u') {
            html += QString("<span style=\"color:#ff5252\">%1</span>").arg(run);
        } else if (s == 'c') {
            html += QString("<span style=\"color:#ffca28\">%1</span>").arg(run);
        } else {
            html += run;
        }

        i = j;
    }

    return html;
}

// Chips of the protocol fields of a record: a label each (KEY:value for a value, a word for a flag), its HTML and a
// tool tip saying what it is and its value
void PagingMessageTable::recordChips(const MessageRecord& record, QStringList& texts, QStringList& chips, QStringList& tips)
{
    const QVariantMap& d = record.m_protocolData;
    auto add = [&](const QString& text, const QString& tipText, const QString& chipHtml = QString()) {
        texts.append(text);
        chips.append(chipHtml.isEmpty() ? infoChip(text) : chipHtml);
        tips.append(tipText);
    };
    auto checkTip = [](const QString& what, const QString& check) {
        if (check == "ok") {
            return tr("%1: ok, it matches the message received").arg(what);
        } else if (check == "failed") {
            return tr("%1: failed, the message received does not match it (a character is wrong)").arg(what);
        }
        return tr("%1: not checked, a word of the message was lost").arg(what);
    };

    if (record.m_protocol == "pocsag")
    {
        if (d.contains("batch"))
        {
            int batch = d.value("batch").toInt() + 1;
            add(QString("B:%1").arg(batch), tr("Batch %1: the address codeword was in batch %1 of the transmission "
                "(a batch is a sync codeword and 16 codewords; the first batch after the preamble is 1)").arg(batch));
        }
        if (d.value("truncated").toBool()) {
            add("TRUNC", tr("Truncated: the message has no end codeword (idle or address); the signal was lost or two "
                "codewords in a row could not be decoded, so its end may be missing"));
        }
    }
    else if (record.m_protocol == "flex")
    {
        if (d.contains("phase"))
        {
            QString phase = d.value("phase").toString();
            int cycle = d.value("cycle").toInt();
            int frame = d.value("frame").toInt();
            add(QString("PH:%1").arg(phase), tr("Phase %1: the phase the message was sent in (1600 bps has phase A, "
                "3200 bps A and C, 6400 bps A to D)").arg(phase));
            add(QString("C:%1").arg(cycle), tr("Cycle %1 of 0-14: the frame's cycle (15 cycles of 4 minutes make an hour)").arg(cycle));
            add(QString("F:%1").arg(frame), tr("Frame %1 of 0-127: the frame of its cycle (frames of 1.875 s)").arg(frame));
        }
        // A subframe of a multiple transmission: the copies combined of the transmissions
        if (d.contains("transmissions"))
        {
            int copies = d.value("subframe_copies").toInt();
            int transmissions = d.value("transmissions").toInt();
            add(QString("SUB:%1/%2").arg(copies).arg(transmissions), tr("Subframe copies: the frame is sent %2 times "
                "(multiple transmission); %1 of the copies were received and combined").arg(copies).arg(transmissions));
        }
        // The message number as the pager shows it: N + 1 (STD-43A 3.10.1.3 N)
        if (d.contains("message_number") && (d.value("message_number").toInt() >= 0))
        {
            int number = d.value("message_number").toInt() + 1;
            add(QString("N:%1").arg(number), tr("Message number %1: the network numbers the messages of each address in "
                "sequence (sent 0-63, shown + 1 as pagers do); a skipped number is a missed message").arg(number));
        }
        if (d.value("retrieval").toBool()) {
            add("R", tr("Message retrieval flag set (the usual case): the numbers of this address are checked for "
                "skipped numbers; it is clear on a retransmission"));
        }
        // Numbers of the address skipped before this message (R = 1): messages missed
        if (d.contains("missed_numbers"))
        {
            QVariantList missed = d.value("missed_numbers").toList();
            QStringList numbers;

            for (const QVariant& n : missed) {
                numbers.append(QString::number(n.toInt() + 1));
            }

            QString text = (missed.size() > 3)
                ? QString("MISSED:%1-%2").arg(numbers.first(), numbers.last())
                : QString("MISSED:%1").arg(numbers.join(","));
            add(text, tr("Missed messages: %n number(s) of this address skipped before this message (%1), messages "
                "that were not received", "", missed.size()).arg(numbers.join(", ")), chip(text, "#ef9a9a", "#3a1414"));
        }
        if (d.value("maildrop").toBool()) {
            add("MD", tr("Mail drop flag: the message can be handled separately from ordinary messages"));
        }
        if (d.value("priority").toBool()) {
            add("PRI", tr("Priority address: sent ahead of the other addresses of the frame"));
        }

        QString addrType = d.value("addr_type").toString();
        if (addrType == "long") {
            add("LONG", tr("Long address: the capcode takes two address words"));
        } else if ((addrType != "short") && (addrType != "temporary") && !addrType.isEmpty()) {
            add(addrType.toUpper(), tr("Address type: %1").arg(addrType), chip(addrType.toUpper(), "#ce93d8", "#2e2e2e"));
        }

        // Network ID, or a system message of the NID (the NID change instruction has an unpublished layout)
        if (d.contains("nid_service_area"))
        {
            QString traffic = QString("%1").arg(d.value("nid_traffic_flags").toInt(), 4, 2, QChar('0'));
            add(QString("NID:A%1/M%2/T%3").arg(d.value("nid_service_area").toInt()).arg(d.value("nid_multiplier").toInt()).arg(traffic),
                tr("Network ID (roaming): service area %1, multiplier %2, traffic flags %3").arg(d.value("nid_service_area").toInt())
                    .arg(d.value("nid_multiplier").toInt()).arg(traffic));
        }
        if (d.value("nid_change").toBool()) {
            add("NID CHG", tr("Network ID change instruction"));
        } else if (d.value("nid_system_message").toBool()) {
            add("NID MSG", tr("System message of the network ID"));
        }
        if (d.contains("hex_nibble_bits"))
        {
            bool lsb = (d.value("hex_bit_order").toString() == "lsb_first");
            int nibble = d.value("hex_nibble_bits").toInt();
            int bits = d.value("hex_bits").toInt();
            add(QString("HEX:%1/%2/%3").arg(nibble).arg(lsb ? "LSB" : "MSB").arg(bits),
                tr("HEX/binary data: %1 bits a digit, %2 bit first, %3 bits").arg(nibble).arg(lsb ? tr("least significant") : tr("most significant")).arg(bits));
        }
        if (d.value("payload_unknown").toBool()) {
            add("NO LAYOUT", tr("The layout of this payload is not published: shown as data"));
        }
        if (d.contains("fragment_control") && (d.value("fragment_control").toString() != "00"))
        {
            QString uv = d.value("fragment_control").toString();
            add(QString("UV:%1").arg(uv), tr("Fragment control bits U V = %1 (enhanced fragmentation)").arg(uv));
        }
        if (d.value("nul_error").toBool()) {
            add("NUL?", tr("A NUL character inside the message, where only the padding at the end of a fragment may "
                "have it: probably a reception error"));
        }
        if (d.contains("blocking"))
        {
            int blocking = d.value("blocking").toInt();
            bool rtl = (d.value("display_direction").toString() == "rtl");
            add(QString("BLK:%1").arg(blocking), tr("Blocking length: the data is in blocks of %1 bits").arg(blocking));
            add(rtl ? "RTL" : "LTR", rtl ? tr("Display direction: right to left") : tr("Display direction: left to right"));
            if (d.value("header_message").toBool()) {
                add("HDR", tr("Header message: a header to a following transparent message of the same message number"));
            }
            if (d.value("status_info").toBool()) {
                add("STAT", tr("Status information message"));
            }
        }
        if (d.contains("secure_type"))
        {
            QString t = QString("%1%2").arg(d.value("secure_type").toInt() >> 1 & 1).arg(d.value("secure_type").toInt() & 1);
            add(QString("T:%1").arg(t), tr("Secure message type bits t1 t0 = %1").arg(t));
        }
        if (d.value("registration_ack").toBool()) {
            add("REGACK", tr("Registration acknowledgement"));
        }

        if (d.contains("group_slot"))
        {
            QStringList members = d.value("group_capcodes").toStringList();
            bool tracked = d.value("group_tracked").toBool();
            int slot = d.value("group_slot").toInt();
            QString g = QString("G:%1[%2]").arg(slot).arg(tracked ? QString::number(members.size()) : QString("?"));
            // The first members; the window of the members (right click) has them all
            static const int shownMembers = 20;
            QStringList shown = members.mid(0, shownMembers);

            if (members.size() > shownMembers) {
                shown.append(tr("and %1 more").arg(members.size() - shownMembers));
            }

            add(g, tracked ? tr("Group message, group slot %1 (temporary address %2), %3 members:\n%4").arg(slot)
                    .arg(2029568 + slot).arg(members.size()).arg(shown.join("\n"))
                : tr("Group message, group slot %1 (temporary address %2): the group assignment was not received, the members "
                    "are not known").arg(slot).arg(2029568 + slot),
                chip(g, "#80cbc4", "#103a35"));
        }
        // A member added to a group: the slot and the frame of the group message
        if (d.contains("instruction_type") && (d.value("instruction_type").toInt() == 0))
        {
            int slot = d.value("instruction_slot").toInt();
            int frame = d.value("instruction_frame").toInt();
            QString g = QString("G:%1>F:%2").arg(slot).arg(frame);
            add(g, tr("%1 added to group slot %2 (temporary address %3); the group message comes in frame %4")
                .arg(record.m_address).arg(slot).arg(2029568 + slot).arg(frame), chip(g, "#80cbc4", "#103a35"));
        }

        QString checksum = d.value("checksum").toString();
        QString k = checkChip("K", checksum);
        if (!k.isEmpty()) {
            add(QString("K:%1").arg(checksum), checkTip(tr("Checksum K of the message words"), checksum), k);
        }
        QString signature = d.value("signature").toString();
        QString s = checkChip("S", signature);
        if (!s.isEmpty()) {
            add(QString("S:%1").arg(signature), checkTip(tr("Signature S (sum of the characters of the whole message)"), signature), s);
        }
    }
    else if (record.m_protocol == "gsc")
    {
        if (d.value("non_battery_saver").toBool()) {
            add("NBS", tr("Non battery saver transmission"));
        } else if (d.value("batch_mode").toBool()) {
            add("BATCH", tr("Batch mode: the transmission started with a complemented preamble"));
        }
        if (d.contains("slot"))
        {
            int slot = d.value("slot").toInt() + 1;
            add(QString("SLOT:%1").arg(slot), tr("Address %1 of the transmission (the first address is 1)").arg(slot));
        }
        if (d.contains("checksum_passed"))
        {
            bool passed = d.value("checksum_passed").toBool();
            add(passed ? "CK:ok" : "CK:bad", passed ? tr("Checksum of the data blocks: ok") : tr("Checksum of the data blocks: failed"),
                checkChip("CK", passed ? "ok" : "failed"));
        }
        if (d.value("interleaved").toBool()) {
            add("INTL", tr("Interleaved: a tone only address sent in the alert time of a voice page"));
        }
        if (d.value("truncated").toBool()) {
            add("TRUNC", tr("Truncated: the transmission ended before the message did"));
        }
    }

    // A transmitted copy of a message sent before (retransmission)
    if (d.contains("retransmission"))
    {
        int copy = d.value("retransmission").toInt();
        QString c = QString("COPY:%1").arg(copy);
        add(c, tr("Transmitted copy %1 of a message sent before (retransmission)").arg(copy), chip(c, "#ffe082", "#3a2a00"));
    }

    if (record.m_duplicate == MessageRecord::DuplicateDup) {
        add("DUP", tr("Duplicate: the same content was received before"), chip("DUP", "#2a1f00", "#ffcc44"));
    } else if (record.m_duplicate == MessageRecord::DuplicateDupPlus) {
        add("DUP+", tr("Duplicate with fewer errors: the same message was received before, this copy is better"),
            chip("DUP+", "#0d2a12", "#66bb6a"));
    }
}

void PagingMessageTable::recordInfo(const MessageRecord& record, QString& plain, QString& html, QString& tip)
{
    QStringList texts, chips, tips;
    recordChips(record, texts, chips, tips);
    plain = texts.join(" ");
    html = chips.join(" ");
    tip = tips.join("\n");
}

// The cells of a row from a record. A duplicate is shown like any message, with its DUP or DUP+ flag in the info
void PagingMessageTable::fillRow(int row, const MessageRecord& record, const QString& state)
{
    const QVariantMap& d = record.m_protocolData;

    auto cell = [&](int col, const QString& plain, const QString& html = QString(), const QString& tip = QString()) {
        QTableWidgetItem *item = this->item(row, col);

        if (!item)
        {
            item = new QTableWidgetItem();
            setItem(row, col, item);
        }

        item->setText(plain);
        item->setData(htmlRole, html.isEmpty() ? QVariant() : QVariant(html));
        item->setToolTip(tip);
        return item;
    };

    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(record.m_timestamp);
    cell(ColDate, dateTime.date().toString(Qt::TextDate), QString(), dateTime.toString(Qt::ISODate));
    QTableWidgetItem *timeItem = cell(ColTime, dateTime.toString("HH:mm:ss"), QString(), dateTime.toString(Qt::ISODate));
    timeItem->setData(Qt::UserRole, record.m_id);
    timeItem->setData(voiceFileRole, d.value("voice_file").toString());
    timeItem->setData(jsonRole, QString::fromUtf8(QJsonDocument(record.toJson()).toJson(QJsonDocument::Compact)));

    bool transmitted = (record.m_direction == MessageRecord::DirectionTX);
    cell(ColDirection, transmitted ? "TX" : "RX");
    cell(ColChannel, record.m_channelAlias);
    cell(ColProtocol, record.m_protocol.toUpper());
    cell(ColRate, record.m_mode);

    bool inverted = (record.m_polarity == MessageRecord::PolarityInverted);
    if (record.m_polarity == MessageRecord::PolarityUnknown) {
        cell(ColPolarity, QString());
    } else {
        cell(ColPolarity, inverted ? "I" : "N", inverted ? chip("I", "#2a1f00", "#ffcc44") : chip("N", "#0d1a2a", "#88aacc"),
            inverted ? tr("Inverted polarity") : tr("Normal polarity"));
    }

    cell(ColAddress, record.m_address);
    cell(ColType, record.m_msgType, typeChip(record.m_msgType), record.m_msgType);

    // Message, with the alternative decode in grey italics. With the status of each character: received in the
    // colour of the text, corrected amber, lost red on a red tint (shown as '?' or <...>)
    QString text = recordText(record);
    QString messageHtml = statusHtml(text, d.value("char_status").toString());
    QString messageTip = text;
    QString altText = d.value("alt_text").toString();

    if ((record.m_rawBits > 0) && !altText.isEmpty())
    {
        QString altType = d.value("alt_type").toString();
        messageHtml += QString(" <i style=\"color:#888888\">[%1: %2]</i>").arg(altType.toHtmlEscaped(), altText.toHtmlEscaped());
        messageTip += QString("\n%1: %2").arg(altType, altText);
    }
    cell(ColMessage, text, messageHtml, messageTip)->setData(tipRole, messageTip);
    setVoiceCell(row);

    // Fragment: the n-th fragment received, or the reassembled message of n fragments
    if (d.value("is_fragment").toBool())
    {
        QString f = QString::number(d.value("fragment_count").toInt());
        cell(ColFragment, f, chip(f, "#3a2a00", "#ffb74d"), tr("Fragment %1 received").arg(f));
    }
    else if (d.value("fragmented").toBool())
    {
        int n = d.value("fragment_count").toInt();
        bool complete = d.value("complete").toBool();
        QString f = QString("%1 %2").arg(complete ? "REASM" : "INCOMPLETE").arg(n);
        cell(ColFragment, f, complete ? chip(f, "#0d2a3a", "#4fc3f7") : chip(f, "#2a1000", "#ff8a65"),
            complete ? tr("Reassembled from %1 fragments").arg(n) : tr("%1 fragments, fragments missing or timed out").arg(n));
    }
    else {
        cell(ColFragment, QString());
    }

    QStringList chipTexts, chipHtml, chipTips;
    recordChips(record, chipTexts, chipHtml, chipTips);
    QTableWidgetItem *infoItem = cell(ColInfo, chipTexts.join(" "), chipHtml.join(" "), chipTips.join("\n"));
    infoItem->setData(chipTextsRole, chipTexts);
    infoItem->setData(chipTipsRole, chipTips);

    // Errors: X uncorrectable codewords, ~ corrected bits; none for a message that was not received
    if ((record.m_errorsCorrected < 0) && (record.m_errorsUncorrectable < 0))
    {
        cell(ColErrors, QString());
    }
    else
    {
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

        cell(ColErrors, errors, errorsHtml,
            tr("%1 bit errors corrected, %2 codewords uncorrectable").arg(record.m_errorsCorrected).arg(record.m_errorsUncorrectable));
    }

    cell(ColState, state);
}

void PagingMessageTable::addRecord(const MessageRecord& record, const QString& state)
{
    int row = findRow(record.m_id);

    if (row >= 0)
    {
        // The end of a voice message completes its row
        if (record.m_protocolData.contains("voice_end") && record.m_text.isEmpty())
        {
            QTableWidgetItem *timeItem = item(row, ColTime);
            QTableWidgetItem *messageItem = item(row, ColMessage);
            QString file = record.m_protocolData.value("voice_file").toString();
            QString text = recordText(record);

            timeItem->setData(voiceFileRole, file);
            messageItem->setText(text);
            messageItem->setToolTip(text);
            messageItem->setData(tipRole, text);
            messageItem->setData(htmlRole, QVariant());
            setVoiceCell(row);
            return;
        }

        fillRow(row, record, state);
        resizeRowToContents(row);
        return;
    }

    QScrollBar *sb = verticalScrollBar();
    bool atBottom = (sb->value() == sb->maximum());
    row = rowCount();
    setRowCount(row + 1);
    fillRow(row, record, state);
    resizeRowToContents(row);

    while (rowCount() > m_maxRows) {
        removeRow(0);
    }

    if (atBottom) {
        scrollToBottom();
    }
}

void PagingMessageTable::setAudioDevice(const QString& deviceName)
{
    m_player->setAudioDevice(deviceName);
}

const QString& PagingMessageTable::audioDevice() const
{
    return m_player->audioDevice();
}

// The message cell of a voice message with a file: a play icon (stop while it is played), and the file in the
// tool tip: its path, length and format
void PagingMessageTable::setVoiceCell(int row)
{
    QTableWidgetItem *timeItem = item(row, ColTime);
    QTableWidgetItem *messageItem = item(row, ColMessage);

    if (!timeItem || !messageItem) {
        return;
    }

    QString file = timeItem->data(voiceFileRole).toString();
    QString tip = messageItem->data(tipRole).toString();

    if (file.isEmpty())
    {
        messageItem->setIcon(QIcon());
        messageItem->setToolTip(tip);
        return;
    }

    bool playing = m_player->isPlaying() && (m_player->file() == file);
    messageItem->setIcon(QIcon(playing ? ":/stop.png" : ":/play.png"));
    messageItem->setToolTip(QString("%1\n\n%2\n\n%3").arg(tip, PagingVoicePlayer::fileInfo(file),
        playing ? tr("Click the icon to stop. Right click: the audio output")
                : tr("Click the icon to play. Right click: the audio output")));
}

// The icons of the voice files: the one played has the stop icon
void PagingMessageTable::displayPlayer()
{
    for (int row = 0; row < rowCount(); row++)
    {
        QTableWidgetItem *timeItem = item(row, ColTime);

        if (timeItem && !timeItem->data(voiceFileRole).toString().isEmpty()) {
            setVoiceCell(row);
        }
    }
}

void PagingMessageTable::toggleVoice(int row)
{
    QTableWidgetItem *timeItem = item(row, ColTime);
    QString file = timeItem ? timeItem->data(voiceFileRole).toString() : QString();

    if (file.isEmpty()) {
        return;
    }

    if (m_player->isPlaying() && (m_player->file() == file))
    {
        m_player->stop();
        return;
    }

    QString error;

    if (!m_player->play(file, error)) {
        QMessageBox::information(this, tr("Voice message"), error);
    }
}

namespace {

// A JSON value as one line of a tree: objects and arrays as branches
void addJsonItem(QTreeWidgetItem *parent, const QString& key, const QJsonValue& value)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(parent, QStringList(key));

    if (value.isObject())
    {
        const QJsonObject object = value.toObject();

        for (auto it = object.begin(); it != object.end(); ++it) {
            addJsonItem(item, it.key(), it.value());
        }
        item->setExpanded(true);
    }
    else if (value.isArray())
    {
        const QJsonArray array = value.toArray();

        for (int i = 0; i < array.size(); i++) {
            addJsonItem(item, QString::number(i), array.at(i));
        }
        item->setText(1, QString("[%1]").arg(array.size()));
    }
    else
    {
        QString text = value.isNull() ? QString("null") : value.isBool() ? QString(value.toBool() ? "true" : "false")
            : value.isDouble() ? QString::number(value.toDouble(), 'g', 15) : value.toString();
        item->setText(1, text);
        item->setToolTip(1, text);
    }
}

} // namespace

// The window of the Message Center: the feature window at its index in feature set 0 (there is one feature set),
// nullptr if there is none
FeatureGUI *PagingMessageTable::messageCenterGui()
{
    int featureSetIndex = -1;
    int featureIndex = -1;

    if (!FeatureWebAPIUtils::getFeature(featureSetIndex, featureIndex, "sdrangel.feature.messagecenter")) {
        return nullptr;
    }

    for (QWidget *widget : QApplication::allWidgets())
    {
        FeatureGUI *gui = qobject_cast<FeatureGUI*>(widget);

        if (gui && (gui->getIndex() == featureIndex)) {
            return gui;
        }
    }

    return nullptr;
}

void PagingMessageTable::openMessageCenter()
{
    FeatureGUI *gui = messageCenterGui();

    if (!gui)
    {
        // Added by the main message queue: its window shows when it is made
        FeatureWebAPIUtils::openMessageCenter();
        return;
    }

    gui->show();
    gui->showNormal();
    gui->raise();
    gui->activateWindow();
}

void PagingMessageTable::createRoute(const MessageRecord& record, const QVariantMap& charset, const QString& type)
{
    FeatureGUI *gui = messageCenterGui();

    if (!gui) {
        return;
    }

    gui->showNormal();
    // Queued: the menu or the dialog of the table closes first
    const QString json = QString::fromUtf8(QJsonDocument(record.toJson()).toJson(QJsonDocument::Compact));
    QMetaObject::invokeMethod(gui, "createRouteFromRecord", Qt::QueuedConnection, Q_ARG(QString, json),
        Q_ARG(QVariantMap, charset), Q_ARG(QString, type));
}

// Every codepage of the protocol of a received message, and its numeric reading: a row each. The right click on a
// row makes a route of the Message Center that decodes the messages of the address so
void PagingMessageTable::showAllCodepages(const MessageRecord& record)
{
    struct Reading
    {
        RadioPagingCharset::Key m_key;
        QString m_type;
        QString m_label;
    };

    const QStringList types = RadioPagingRecode::types(record);
    const QString alphaType = types.contains("alphanumeric") ? QString("alphanumeric") : QString();
    QList<Reading> readings;

    for (const RadioPagingCharset::Codepage *codepage : RadioPagingCharset::codepages(record.m_protocol.toStdString()))
    {
        readings.append({codepage->key(), (alphaType != record.m_msgType) ? alphaType : QString(),
            QString::fromUtf8(codepage->m_label)});
    }
    if (types.contains("numeric")) {
        readings.append({RadioPagingCharset::Key(), QString("numeric"), tr("Numeric")});
    }

    QDialog *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("%1 %2 in every codepage").arg(record.m_protocol.toUpper(), record.m_address));
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    QTableWidget *table = new QTableWidget((int) readings.size(), 2, dialog);
    table->setHorizontalHeaderLabels({tr("Codepage"), tr("Message")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    const RadioPagingCharset::Key current = RadioPagingPage::charsetKey(record.m_protocolData.value("charset"));

    for (int i = 0; i < (int) readings.size(); i++)
    {
        MessageRecord out;
        QString error;
        const bool done = RadioPagingRecode::redecode(record, readings[i].m_key, readings[i].m_type, out, error);
        QTableWidgetItem *label = new QTableWidgetItem(readings[i].m_label);
        QTableWidgetItem *text = new QTableWidgetItem(done ? out.m_text : error);

        // The reading of the message as it is shown
        if (((readings[i].m_key == current) && readings[i].m_type.isEmpty()) || (readings[i].m_type == record.m_msgType))
        {
            QFont bold = label->font();
            bold.setBold(true);
            label->setFont(bold);
        }
        if (!done) {
            text->setForeground(Qt::gray);
        }

        text->setToolTip(text->text());
        table->setItem(i, 0, label);
        table->setItem(i, 1, text);
    }

    table->resizeColumnToContents(0);
    const bool canRoute = messageCenterGui() != nullptr;

    connect(table, &QTableWidget::customContextMenuRequested, dialog, [table, readings, record, canRoute](const QPoint& pos) {
        QTableWidgetItem *clicked = table->itemAt(pos);

        if (!clicked) {
            return;
        }

        const Reading reading = readings[clicked->row()];
        QMenu menu(table);
        QAction *route = menu.addAction(tr("Create route..."));
        route->setToolTip(tr("A route of the Message Center for the messages of this address, decoded so"));
        route->setEnabled(canRoute);
        connect(route, &QAction::triggered, table, [record, reading]() {
            createRoute(record, reading.m_key.empty() ? QVariantMap() : RadioPagingPage::charsetData(reading.m_key), reading.m_type);
        });
        connect(menu.addAction(tr("Copy message")), &QAction::triggered, table, [table, clicked]() {
            QApplication::clipboard()->setText(table->item(clicked->row(), 1)->text());
        });
        menu.exec(table->viewport()->mapToGlobal(pos));
    });

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(table);
    layout->addWidget(buttons);
    dialog->resize(640, 400);
    new DialogPositioner(dialog, true);
    dialog->show();
}

// Every field of the record of the row, as stored (the JSON of the record): the common fields, protocolData
// and the raw payload, also as bits

// The members of a group message, one capcode a row: a search, copy, and the messages of a member
void PagingMessageTable::showGroupMembers(const MessageRecord& record)
{
    const QVariantList members = record.m_protocolData.value("group_capcodes").toList();
    QDialog *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Group slot %1 (%2): %3 members").arg(record.m_protocolData.value("group_slot").toInt())
        .arg(record.m_address).arg(members.size()));
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    QLineEdit *search = new QLineEdit(dialog);
    search->setPlaceholderText(tr("Search"));
    search->setClearButtonEnabled(true);
    QTableWidget *table = new QTableWidget(members.size(), 2, dialog);
    table->setHorizontalHeaderLabels({tr("#"), tr("Capcode")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    for (int i = 0; i < members.size(); i++)
    {
        table->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        table->setItem(i, 1, new QTableWidgetItem(members[i].toString()));
    }

    table->resizeColumnToContents(0);

    connect(search, &QLineEdit::textChanged, table, [table](const QString& text) {
        for (int i = 0; i < table->rowCount(); i++) {
            table->setRowHidden(i, !text.isEmpty() && !table->item(i, 1)->text().contains(text.trimmed()));
        }
    });
    connect(table, &QTableWidget::customContextMenuRequested, table, [this, table](const QPoint& pos) {
        QTableWidgetItem *clicked = table->itemAt(pos);

        if (!clicked) {
            return;
        }

        const QString capcode = table->item(clicked->row(), 1)->text();
        QMenu menu(table);
        connect(menu.addAction(tr("Messages of %1").arg(capcode)), &QAction::triggered, this, [this, capcode]() {
            emit addressFilterRequested(capcode);
        });
        connect(menu.addAction(tr("Copy")), &QAction::triggered, table, [capcode]() { QApplication::clipboard()->setText(capcode); });
        menu.exec(table->viewport()->mapToGlobal(pos));
    });

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QPushButton *copyAll = buttons->addButton(tr("Copy all"), QDialogButtonBox::ActionRole);
    connect(copyAll, &QPushButton::clicked, dialog, [members]() {
        QStringList list;

        for (const QVariant& m : members) {
            list.append(m.toString());
        }

        QApplication::clipboard()->setText(list.join('\n'));
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(search);
    layout->addWidget(table, 1);
    layout->addWidget(buttons);
    dialog->resize(320, 480);
    dialog->show();
}

void PagingMessageTable::showDetails(int row)
{
    QTableWidgetItem *timeItem = item(row, ColTime);

    if (!timeItem) {
        return;
    }

    const QString json = timeItem->data(jsonRole).toString();
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    QDialog *dialog = new QDialog(this);
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    QTreeWidget *tree = new QTreeWidget(dialog);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QPushButton *copy = buttons->addButton(tr("Copy as JSON"), QDialogButtonBox::ActionRole);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Message %1 %2").arg(object.value("protocol").toString(), object.value("address").toString()));
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("Field"), tr("Value")});

    // A group message: its members first, the list closed when it is long
    const QJsonArray members = object.value("protocolData").toObject().value("group_capcodes").toArray();

    if (!members.isEmpty())
    {
        QTreeWidgetItem *group = new QTreeWidgetItem(tree->invisibleRootItem(),
            QStringList({tr("Group members (%1)").arg(members.size()), tr("slot %1").arg(object.value("protocolData").toObject().value("group_slot").toInt())}));

        for (const QJsonValue& member : members) {
            new QTreeWidgetItem(group, QStringList({QString(), member.toString()}));
        }

        group->setExpanded(members.size() <= 20);
    }

    for (auto it = object.begin(); it != object.end(); ++it) {
        addJsonItem(tree->invisibleRootItem(), it.key(), it.value());
    }

    // The raw payload as bits, in groups of 8 from the first bit
    const QByteArray raw = QByteArray::fromHex(object.value("rawData").toString().toLatin1());
    const int rawBits = object.value("rawBits").toInt(-1);

    if (rawBits > 0)
    {
        QString bits;

        for (int i = 0; (i < rawBits) && (i / 8 < raw.size()); i++)
        {
            if ((i > 0) && (i % 8 == 0)) {
                bits += ' ';
            }
            bits += ((uint8_t) raw[i / 8] & (0x80 >> (i % 8))) ? '1' : '0';
        }

        QTreeWidgetItem *item = new QTreeWidgetItem(tree->invisibleRootItem(), QStringList({tr("rawData (bits)"), bits}));
        item->setToolTip(1, bits);
    }

    tree->resizeColumnToContents(0);
    layout->addWidget(tree);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(copy, &QPushButton::clicked, dialog, [json]() { QApplication::clipboard()->setText(json); });
    dialog->resize(640, 520);
    dialog->show();
}

// A click on the icon of a voice message plays or stops it; elsewhere the click selects as usual
// Over the info column: the tool tip of the chip under the mouse (its label and what it means). The chips are laid out
// as the delegate draws them: the HTML in the text rectangle of the cell, centred vertically
bool PagingMessageTable::viewportEvent(QEvent *event)
{
    if (event->type() != QEvent::ToolTip) {
        return QTableWidget::viewportEvent(event);
    }

    QHelpEvent *help = static_cast<QHelpEvent *>(event);
    QModelIndex index = indexAt(help->pos());
    QTableWidgetItem *it = index.isValid() ? item(index.row(), index.column()) : nullptr;
    QStringList texts = it ? it->data(chipTextsRole).toStringList() : QStringList();
    QStringList tips = it ? it->data(chipTipsRole).toStringList() : QStringList();

    if (texts.isEmpty() || (texts.size() != tips.size())) {
        return QTableWidget::viewportEvent(event);
    }

    QStyleOptionViewItem opt;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    initViewItemOption(&opt);
#else
    opt = viewOptions();
#endif
    opt.rect = visualRect(index);
    opt.features = QStyleOptionViewItem::HasDisplay;
    QRect textRect = style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, this);

    QTextDocument doc;
    doc.setDocumentMargin(1);
    doc.setDefaultFont(opt.font);
    doc.setHtml(it->data(htmlRole).toString());
    QPointF p(help->pos().x() - textRect.left(), help->pos().y() - (opt.rect.top() + (opt.rect.height() - doc.size().height()) / 2));
    int pos = doc.documentLayout()->hitTest(p, Qt::ExactHit);

    // Characters of the document: a non breaking space, the label, a non breaking space, then a space between chips
    int start = 0;

    for (int i = 0; (pos >= 0) && (i < texts.size()); i++)
    {
        int end = start + texts[i].size() + 2;

        if (pos < end)
        {
            QToolTip::showText(help->globalPos(), QString("%1\n%2").arg(texts[i], tips[i]), viewport(), visualRect(index));
            return true;
        }

        start = end + 1;
    }

    return QTableWidget::viewportEvent(event);
}

void PagingMessageTable::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        QModelIndex index = indexAt(event->pos());

        if (index.isValid() && (index.column() == ColMessage))
        {
            QTableWidgetItem *messageItem = item(index.row(), ColMessage);
            QRect rect = visualRect(index);
            int iconWidth = iconSize().isValid() ? iconSize().width() : style()->pixelMetric(QStyle::PM_SmallIconSize);

            if (messageItem && !messageItem->icon().isNull() && (event->pos().x() < rect.left() + iconWidth + 6))
            {
                toggleVoice(index.row());
                event->accept();
                return;
            }
        }
    }

    QTableWidget::mousePressEvent(event);
}

// The audio output of the voice files, in the dialog of the audio outputs of SDRangel
void PagingMessageTable::selectAudioDevice(const QPoint& pos)
{
    AudioSelectDialog audioSelect(DSPEngine::instance()->getAudioDeviceManager(), m_player->audioDevice());
    audioSelect.move(pos);
    new DialogPositioner(&audioSelect, false);
    audioSelect.exec();

    if (audioSelect.m_selected && (audioSelect.m_audioDeviceName != m_player->audioDevice()))
    {
        m_player->setAudioDevice(audioSelect.m_audioDeviceName);
        emit audioDeviceChanged(audioSelect.m_audioDeviceName);
    }
}

// Right click in the header: the columns to show
void PagingMessageTable::showColumnMenu(const QPoint& pos)
{
    m_columnMenu->clear();

    for (int i = 0; i < ColumnCount; i++)
    {
        QAction *action = m_columnMenu->addAction(headers[i]);
        action->setCheckable(true);
        action->setChecked(!isColumnHidden(i));
        connect(action, &QAction::triggered, this, [this, i](bool checked) {
            setColumnHidden(i, !checked);
            emit columnsChanged();
        });
    }

    m_columnMenu->popup(horizontalHeader()->viewport()->mapToGlobal(pos));
}

// Right click in a row: copy
void PagingMessageTable::showRowMenu(const QPoint& pos)
{
    QTableWidgetItem *clicked = itemAt(pos);

    if (!clicked) {
        return;
    }

    int row = clicked->row();
    QMenu menu(this);

    connect(menu.addAction(tr("Details...")), &QAction::triggered, this, [this, row]() { showDetails(row); });
    connect(menu.addAction(tr("Copy cell")), &QAction::triggered, this, [clicked]() {
        QApplication::clipboard()->setText(clicked->text());
    });
    connect(menu.addAction(tr("Copy row")), &QAction::triggered, this, [this, row]() {
        QStringList cells;

        for (int c = 0; c < ColumnCount; c++)
        {
            if (!isColumnHidden(c) && item(row, c)) {
                cells.append(item(row, c)->text());
            }
        }

        QApplication::clipboard()->setText(cells.join('\t'));
    });
    connect(menu.addAction(tr("Copy as JSON")), &QAction::triggered, this, [this, row]() {
        QApplication::clipboard()->setText(item(row, ColTime)->data(jsonRole).toString());
    });

    // The voice file of the row, and the audio output of the voice files
    QString file = item(row, ColTime) ? item(row, ColTime)->data(voiceFileRole).toString() : QString();
    menu.addSeparator();

    if (!file.isEmpty())
    {
        bool playing = m_player->isPlaying() && (m_player->file() == file);
        connect(menu.addAction(QIcon(playing ? ":/stop.png" : ":/play.png"), playing ? tr("Stop voice") : tr("Play voice")),
            &QAction::triggered, this, [this, row]() { toggleVoice(row); });
    }

    QPoint global = viewport()->mapToGlobal(pos);
    connect(menu.addAction(tr("Audio output...")), &QAction::triggered, this, [this, global]() { selectAudioDevice(global); });

    // A received message decoded again: as the other type, with another codepage of its protocol
    MessageRecord record;
    const QJsonObject json = QJsonDocument::fromJson(item(row, ColTime)->data(jsonRole).toString().toUtf8()).object();
    const bool parsed = MessageRecord::fromJson(json, record);

    // The members of a group message, all of them
    if (parsed && !record.m_protocolData.value("group_capcodes").toList().isEmpty())
    {
        connect(menu.addAction(tr("Group members (%1)...").arg(record.m_protocolData.value("group_capcodes").toList().size())),
            &QAction::triggered, this, [this, record]() { showGroupMembers(record); });
    }

    // A route of the Message Center filled from the message
    if (parsed && (record.m_kind == MessageRecord::KindMessage) && !record.m_address.isEmpty())
    {
        QAction *route = menu.addAction(tr("Create route..."));
        route->setToolTip(tr("A route of the Message Center for the messages of this address"));
        route->setEnabled(messageCenterGui() != nullptr);
        connect(route, &QAction::triggered, this, [record]() { createRoute(record, QVariantMap(), QString()); });
    }

    if (parsed && !RadioPagingRecode::types(record).isEmpty())
    {
        QMenu *decode = menu.addMenu(tr("Decode as"));
        connect(decode->addAction(tr("All codepages...")), &QAction::triggered, this, [this, record]() { showAllCodepages(record); });
        decode->addSeparator();
        const QString state = item(row, ColState) ? item(row, ColState)->text() : QString();
        auto redecode = [this, record, state](const RadioPagingCharset::Key& key, const QString& type) {
            MessageRecord out;
            QString error;

            if (!RadioPagingRecode::redecode(record, key, type, out, error))
            {
                qWarning() << "PagingMessageTable: decode again:" << error;
                return;
            }

            int r = findRow(out.m_id);

            if (r >= 0) {
                fillRow(r, out, state);
            }

            emit recordRedecoded(out);
        };

        for (const QString& type : RadioPagingRecode::types(record))
        {
            QAction *action = decode->addAction(type == "numeric" ? tr("Numeric") : tr("Alphanumeric"));
            action->setCheckable(true);
            action->setChecked(type == record.m_msgType);
            connect(action, &QAction::triggered, this, [redecode, type]() { redecode(RadioPagingCharset::Key(), type); });
        }

        decode->addSeparator();
        const RadioPagingCharset::Key current = RadioPagingPage::charsetKey(record.m_protocolData.value("charset"));
        QMap<QString, QMenu*> vendors;

        for (const RadioPagingCharset::Codepage *codepage : RadioPagingCharset::codepages(record.m_protocol.toStdString()))
        {
            const QString vendor = QString::fromUtf8(codepage->m_vendor);

            if (!vendors.contains(vendor)) {
                vendors[vendor] = decode->addMenu(vendor);
            }

            QAction *action = vendors[vendor]->addAction(QString::fromUtf8(codepage->m_label));
            const RadioPagingCharset::Key key = codepage->key();
            action->setCheckable(true);
            action->setChecked(key == current);
            connect(action, &QAction::triggered, this, [redecode, key]() { redecode(key, QString()); });
        }
    }

    menu.exec(global);
}
