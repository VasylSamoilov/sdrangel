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

// Self test of the table of paging messages: rows, updates by id, chips, columns. Run with
// QT_QPA_PLATFORM=offscreen. Prints one line per check and returns non-zero if any check fails.

#include <QApplication>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <cstdio>
#include <vector>

#include "dsp/wavfilerecord.h"
#include "gui/pagingmessagetable.h"
#include "gui/pagingvoiceplayer.h"

namespace {

int passes = 0;
int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        failures++;
    }
}

MessageRecord record(const QString& id, const QString& protocol, const QString& address, const QString& text)
{
    MessageRecord r;
    r.m_id = id;
    r.m_timestamp = 1000000;
    r.m_protocol = protocol;
    r.m_mode = "1200";
    r.m_msgType = "alphanumeric";
    r.m_address = address;
    r.m_text = text;
    r.m_errorsCorrected = 0;
    r.m_errorsUncorrectable = 0;
    return r;
}

QString cell(const PagingMessageTable& table, int row, PagingMessageTable::Column column)
{
    return table.item(row, column) ? table.item(row, column)->text() : QString();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    PagingMessageTable table;
    table.setMaxRows(3);

    // A row per record, the same id updates its row
    MessageRecord a = record("a", "pocsag", "1234567A", "HELLO");
    a.m_direction = MessageRecord::DirectionTX;
    a.m_errorsCorrected = -1;
    a.m_errorsUncorrectable = -1;
    table.addRecord(a, "queued");
    table.addRecord(record("b", "flex", "0001234567", "FLEX"));
    check(table.rowCount() == 2, "two records are two rows");
    table.addRecord(a, "sent");
    check((table.rowCount() == 2) && (cell(table, 0, PagingMessageTable::ColState) == "sent")
        && (cell(table, 0, PagingMessageTable::ColDirection) == "TX") && cell(table, 0, PagingMessageTable::ColErrors).isEmpty(),
        "the same id updates its row: the state, and no errors for a message that was transmitted");
    check((cell(table, 1, PagingMessageTable::ColErrors) == "ok") && (cell(table, 1, PagingMessageTable::ColDirection) == "RX"),
        "a received message shows its errors");

    // The oldest rows go first
    table.addRecord(record("c", "gsc", "123456/1", "C"));
    table.addRecord(record("d", "gsc", "123456/2", "D"));
    check((table.rowCount() == 3) && (cell(table, 0, PagingMessageTable::ColAddress) == "0001234567"), "the oldest row goes at the maximum");

    // The end of a voice message completes its row
    MessageRecord voice = record("v", "gsc", "123456/3", QString());
    voice.m_msgType = "voice";
    table.addRecord(voice);
    check(cell(table, 2, PagingMessageTable::ColMessage) == "(voice)", "a voice message without its end");
    MessageRecord end = record("v", "gsc", "1234563", QString());
    end.m_msgType = "voice";
    end.m_protocolData.insert("voice_end", "timeout");
    end.m_protocolData.insert("voice_seconds", 4.5);
    table.addRecord(end);
    check((table.rowCount() == 3) && cell(table, 2, PagingMessageTable::ColMessage).startsWith("voice ended: timeout after 4.5 s"),
        "the end of a voice message updates its row");
    check(table.item(2, PagingMessageTable::ColMessage)->icon().isNull(), "no voice file: no play icon");

    // The voice file: a play icon in the message cell, the file in its tool tip
    QTemporaryDir dir;
    WavFileRecord recorder(8000);
    recorder.setMono(true);
    recorder.setFileName(dir.path() + "/voice");
    recorder.startRecording();
    std::vector<qint16> samples(8000, 1000);
    recorder.writeMono(samples.data(), (int) samples.size());
    recorder.stopRecording();
    QString file = recorder.getCurrentFileName();
    end.m_protocolData.insert("voice_file", file);
    table.addRecord(end);
    QTableWidgetItem *message = table.item(2, PagingMessageTable::ColMessage);
    QString voiceTip = message->toolTip();
    check(!message->icon().isNull() && voiceTip.contains(file) && voiceTip.contains("1.0 s, 8000 Hz, 1 channel, 16 bit")
        && voiceTip.contains("Click the icon to play"), qPrintable("a voice file: play icon, path, length and format in the tool tip: " + voiceTip));
    check(PagingVoicePlayer::fileInfo(dir.path() + "/none.wav").contains("not there any more"), "a file that is not there");
    check(!cell(table, 2, PagingMessageTable::ColMessage).contains("double-click"), "no double-click hint");

    // Chips: the fields of a transmitted message are the ones it has
    MessageRecord flexTx = record("f", "flex", "0001234567", "X");
    flexTx.m_direction = MessageRecord::DirectionTX;
    flexTx.m_protocolData.insert("priority", true);
    flexTx.m_protocolData.insert("retransmission", 2);
    QString plain, html, tip;
    PagingMessageTable::recordInfo(flexTx, plain, html, tip);
    check((plain == "PRI COPY 2") && html.contains("COPY 2"), "a transmitted FLEX message: priority and copy, no decode fields");

    MessageRecord flexRx = record("g", "flex", "0001234567", "X");
    flexRx.m_protocolData.insert("phase", "A");
    flexRx.m_protocolData.insert("cycle", 3);
    flexRx.m_protocolData.insert("frame", 31);
    flexRx.m_protocolData.insert("message_number", 0);
    flexRx.m_protocolData.insert("retrieval", true);
    flexRx.m_protocolData.insert("checksum", "ok");
    PagingMessageTable::recordInfo(flexRx, plain, html, tip);
    check(plain == "PH:A C03/F031 #1 R K:ok", "a received FLEX message: phase, number as shown by pagers, R, checksum");

    // Columns: hidden ones, saved and restored
    table.setDefaultColumns({PagingMessageTable::ColTime, PagingMessageTable::ColMessage});
    check(table.isColumnHidden(PagingMessageTable::ColAddress) && !table.isColumnHidden(PagingMessageTable::ColMessage),
        "the default columns are shown, the others hidden");
    table.setColumnHidden(PagingMessageTable::ColAddress, false);
    QByteArray saved = table.columns();
    PagingMessageTable other;
    check(other.setColumns(saved) && !other.isColumnHidden(PagingMessageTable::ColAddress)
        && other.isColumnHidden(PagingMessageTable::ColState), "the columns are restored");
    check(!other.setColumns(QByteArray()), "no saved columns: false, the defaults stay");

    // Details: every field of the record, protocolData and the raw payload as bits
    MessageRecord detailed = record("h", "pocsag", "1234567D", "HI");
    detailed.m_protocolData.insert("function", 3);
    detailed.m_rawData = QByteArray::fromHex("a5f0");
    detailed.m_rawBits = 12;
    PagingMessageTable details;
    details.addRecord(detailed);
    details.showDetails(0);
    QTreeWidget *tree = details.findChild<QTreeWidget*>();
    bool function = false, bits = false, address = false;

    for (QTreeWidgetItemIterator it(tree); tree && *it; ++it)
    {
        function = function || (((*it)->text(0) == "function") && ((*it)->text(1) == "3") && (*it)->parent()
            && ((*it)->parent()->text(0) == "protocolData"));
        bits = bits || (((*it)->text(0) == "rawData (bits)") && ((*it)->text(1) == "10100101 1111"));
        address = address || (((*it)->text(0) == "address") && ((*it)->text(1) == "1234567D"));
    }

    check(tree && function && bits && address, "details: the address, protocolData and the raw payload as bits");

    // Character status: corrected amber, lost red on a red tint, received as the text; no status, escaped text
    QString statusRow = PagingMessageTable::statusHtml("AB?<", ".cXX");
    check(statusRow.startsWith("A<span style=\"color:#ffca28\">B</span>") && statusRow.contains("#ff5252") && statusRow.contains("?&lt;</span>"),
        "char status: corrected and lost characters coloured, lost ones in one span");
    check(PagingMessageTable::statusHtml("A<B", "..") == "A&lt;B", "char status: a status that does not fit the text is not used");

    std::printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
