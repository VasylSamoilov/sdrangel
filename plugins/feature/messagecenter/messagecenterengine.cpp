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

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QUdpSocket>
#include <QUuid>

#include <algorithm>

#include "util/csv.h"
#include "radiopagingpagerformat.h"
#include "radiopagingrecode.h"
#include "messagecenterengine.h"

namespace {

QJsonArray intsToJson(const QList<int>& values)
{
    QJsonArray array;

    for (int v : values) {
        array.append(v);
    }

    return array;
}

QList<int> intsFromJson(const QJsonValue& value)
{
    QList<int> out;

    for (const QJsonValue& v : value.toArray()) {
        out.append(v.toInt());
    }

    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------

bool MessageCenterFilter::matches(const MessageRecord& record) const
{
    if (!m_protocols.isEmpty() && !m_protocols.contains(record.m_protocol, Qt::CaseInsensitive)) {
        return false;
    }
    // The channel of a message; a page also by the transmitter it is for
    if (!m_aliases.isEmpty() && !m_aliases.contains(record.m_channelAlias)
        && !m_aliases.contains(record.m_protocolData.value("transmitter").toString())) {
        return false;
    }
    if ((m_direction >= 0) && ((int) record.m_direction != m_direction)) {
        return false;
    }
    if ((m_kind >= 0) && ((int) record.m_kind != m_kind)) {
        return false;
    }
    if ((m_polarity >= 0) && ((int) record.m_polarity != m_polarity)) {
        return false;
    }
    if (!m_speeds.isEmpty() && !m_speeds.contains(record.m_speed)) {
        return false;
    }
    if (!m_msgType.isEmpty() && (record.m_msgType != m_msgType)) {
        return false;
    }
    // The address, or the address of a member of the group of a group message
    if (!m_address.isEmpty())
    {
        const QRegularExpression& expression = compile(m_compiledAddress, m_address, true);
        const QStringList addresses = record.addresses();

        if (!expression.isValid() || std::none_of(addresses.begin(), addresses.end(),
            [&expression](const QString& a) { return expression.match(a).hasMatch(); })) {
            return false;
        }
    }
    if (!m_text.isEmpty())
    {
        const QRegularExpression& expression = compile(m_compiledText, m_text, true, QRegularExpression::DotMatchesEverythingOption);

        if (!expression.isValid() || !expression.match(record.m_text).hasMatch()) {
            return false;
        }
    }
    if (!m_regExp.isEmpty())
    {
        const QRegularExpression& expression = compiled();
        const QStringList texts = fieldTexts(record, m_matchField);

        if (!expression.isValid() || std::none_of(texts.begin(), texts.end(),
            [&expression](const QString& t) { return expression.match(t).hasMatch(); })) {
            return false;
        }
    }
    if ((m_frequencyMin >= 0) && (record.m_frequency < m_frequencyMin)) {
        return false;
    }
    if ((m_frequencyMax >= 0) && ((record.m_frequency < 0) || (record.m_frequency > m_frequencyMax))) {
        return false;
    }
    if ((m_timeFrom >= 0) && (record.m_timestamp < m_timeFrom)) {
        return false;
    }
    if ((m_timeTo >= 0) && (record.m_timestamp > m_timeTo)) {
        return false;
    }
    if (m_hideDuplicates && (record.m_duplicate != MessageRecord::DuplicateNone)) {
        return false;
    }
    if (m_voiceOnly && record.m_protocolData.value("voice_file").toString().isEmpty()) {
        return false;
    }

    return true;
}

bool MessageCenterFilter::matches(const MessageRecord& record, const QString& source) const
{
    if (!m_sources.isEmpty() && !m_sources.contains(source)) {
        return false;
    }

    return matches(record);
}

// Anchored: the whole text must match, as the filters of the other plugins
const QRegularExpression& MessageCenterFilter::compile(Compiled& compiled, const QString& pattern, bool anchored,
    QRegularExpression::PatternOptions options)
{
    if (!compiled.m_done || (compiled.m_pattern != pattern))
    {
        compiled.m_expression = QRegularExpression(anchored ? QRegularExpression::anchoredPattern(pattern) : pattern, options);
        compiled.m_expression.optimize();
        compiled.m_pattern = pattern;
        compiled.m_done = true;
    }

    return compiled.m_expression;
}

const QRegularExpression& MessageCenterFilter::compiled() const
{
    return compile(m_compiled, m_regExp, false);
}

QString MessageCenterFilter::regExpError() const
{
    if (m_regExp.isEmpty()) {
        return QString();
    }

    const QRegularExpression& expression = compiled();
    return expression.isValid() ? QString() : expression.errorString();
}

QString MessageCenterFilter::errors() const
{
    QStringList errors;

    if (!m_address.isEmpty() && !compile(m_compiledAddress, m_address, true).isValid()) {
        errors.append(QString("address: %1").arg(compile(m_compiledAddress, m_address, true).errorString()));
    }
    if (!m_text.isEmpty() && !compile(m_compiledText, m_text, true, QRegularExpression::DotMatchesEverythingOption).isValid()) {
        errors.append(QString("text: %1").arg(compile(m_compiledText, m_text, true, QRegularExpression::DotMatchesEverythingOption).errorString()));
    }
    if (!regExpError().isEmpty()) {
        errors.append(QString("match: %1").arg(regExpError()));
    }

    return errors.join("; ");
}

QStringList MessageCenterFilter::captures(const MessageRecord& record) const
{
    if (m_regExp.isEmpty()) {
        return QStringList();
    }

    const QRegularExpression& expression = compiled();

    if (!expression.isValid()) {
        return QStringList();
    }

    // The first text of the field that matches: the address, else a member of the group
    for (const QString& text : fieldTexts(record, m_matchField))
    {
        QRegularExpressionMatch match = expression.match(text);

        if (match.hasMatch()) {
            return match.capturedTexts();
        }
    }

    return QStringList();
}

QStringList MessageCenterFilter::fieldTexts(const MessageRecord& record, int field)
{
    if (field == MatchAddress)
    {
        const QStringList addresses = record.addresses();
        return addresses.isEmpty() ? QStringList(QString()) : addresses;
    }

    return QStringList(fieldText(record, field));
}

QString MessageCenterFilter::fieldText(const MessageRecord& record, int field)
{
    switch (field)
    {
    case MatchAddress:
        return record.m_address;
    case MatchMessage:
        return record.m_text;
    case MatchProtocol:
        return record.m_protocol;
    case MatchType:
        return record.m_msgType;
    case MatchChannel:
        return record.m_channelAlias;
    case MatchAny:
        return (record.addresses() + QStringList({record.m_text, record.m_protocol, record.m_msgType, record.m_channelAlias})).join(' ');
    default:
        return QString();
    }
}

QString MessageCenterFilter::matchFieldName(int field)
{
    static const char *names[] = {"Address", "Message", "Protocol", "Type", "Channel", "Any"};
    return ((field >= 0) && (field <= MatchAny)) ? QString(names[field]) : QString();
}

QString messageCenterSubstitute(const QString& text, const MessageRecord& record, const QStringList& captures)
{
    QString out;
    int pos = 0;

    while (pos < text.size())
    {
        int start = text.indexOf("${", pos);
        int end = (start < 0) ? -1 : text.indexOf('}', start + 2);

        if (end < 0)
        {
            out += text.mid(pos);
            break;
        }

        out += text.mid(pos, start - pos);
        QString name = text.mid(start + 2, end - start - 2);
        bool isIndex;
        int index = name.toInt(&isIndex);
        bool known = true;
        QString value;

        if (isIndex && (index >= 0)) {
            value = (index < captures.size()) ? captures[index] : QString();
        } else if (name == "address") {
            value = record.m_address;
        } else if (name == "message") {
            value = record.m_text;
        } else if (name == "protocol") {
            value = record.m_protocol;
        } else if (name == "type") {
            value = record.m_msgType;
        } else if (name == "channel") {
            value = record.m_channelAlias;
        } else if (name == "mode") {
            value = record.m_mode;
        } else if (name == "id") {
            value = record.m_id;
        } else if (name == "frequency") {
            value = QString::number(record.m_frequency);
        } else if (name == "time") {
            value = QDateTime::fromMSecsSinceEpoch(record.m_timestamp, Qt::UTC).toString(Qt::ISODate);
        } else {
            known = false;
        }

        out += known ? value : text.mid(start, end - start + 1);
        pos = end + 1;
    }

    return out;
}

bool MessageCenterFilter::isEmpty() const
{
    return toJson().isEmpty();
}

QJsonObject MessageCenterFilter::toJson() const
{
    QJsonObject json;

    if (!m_protocols.isEmpty()) {
        json["protocols"] = QJsonArray::fromStringList(m_protocols);
    }
    if (!m_aliases.isEmpty()) {
        json["aliases"] = QJsonArray::fromStringList(m_aliases);
    }
    if (!m_sources.isEmpty()) {
        json["sources"] = QJsonArray::fromStringList(m_sources);
    }
    if (m_direction >= 0) {
        json["direction"] = m_direction;
    }
    if (m_kind >= 0) {
        json["kind"] = m_kind;
    }
    if (m_polarity >= 0) {
        json["polarity"] = m_polarity;
    }
    if (!m_speeds.isEmpty()) {
        json["speeds"] = intsToJson(m_speeds);
    }
    if (!m_address.isEmpty()) {
        json["address"] = m_address;
    }
    if (!m_text.isEmpty()) {
        json["text"] = m_text;
    }
    if (!m_msgType.isEmpty()) {
        json["msgType"] = m_msgType;
    }
    if (!m_regExp.isEmpty())
    {
        json["regExp"] = m_regExp;
        json["matchField"] = m_matchField;
    }
    if (m_frequencyMin >= 0) {
        json["frequencyMin"] = (double) m_frequencyMin;
    }
    if (m_frequencyMax >= 0) {
        json["frequencyMax"] = (double) m_frequencyMax;
    }
    if (m_timeFrom >= 0) {
        json["timeFrom"] = (double) m_timeFrom;
    }
    if (m_timeTo >= 0) {
        json["timeTo"] = (double) m_timeTo;
    }
    if (m_hideDuplicates) {
        json["hideDuplicates"] = true;
    }
    if (m_voiceOnly) {
        json["voiceOnly"] = true;
    }

    return json;
}

MessageCenterFilter MessageCenterFilter::fromJson(const QJsonObject& json)
{
    MessageCenterFilter f;

    for (const QJsonValue& v : json["protocols"].toArray()) {
        f.m_protocols.append(v.toString());
    }
    for (const QJsonValue& v : json["aliases"].toArray()) {
        f.m_aliases.append(v.toString());
    }
    for (const QJsonValue& v : json["sources"].toArray()) {
        f.m_sources.append(v.toString());
    }

    f.m_direction = json.contains("direction") ? json["direction"].toInt(-1) : -1;
    f.m_kind = json.contains("kind") ? json["kind"].toInt(-1) : -1;
    f.m_polarity = json.contains("polarity") ? json["polarity"].toInt(-1) : -1;
    f.m_speeds = intsFromJson(json["speeds"]);
    f.m_address = json["address"].toString();
    f.m_text = json["text"].toString();
    f.m_msgType = json["msgType"].toString();
    f.m_regExp = json["regExp"].toString();
    f.m_matchField = std::max((int) MatchAddress, std::min((int) MatchAny, json["matchField"].toInt(MatchAddress)));
    f.m_frequencyMin = json.contains("frequencyMin") ? (qint64) json["frequencyMin"].toDouble(-1) : -1;
    f.m_frequencyMax = json.contains("frequencyMax") ? (qint64) json["frequencyMax"].toDouble(-1) : -1;
    f.m_timeFrom = json.contains("timeFrom") ? (qint64) json["timeFrom"].toDouble(-1) : -1;
    f.m_timeTo = json.contains("timeTo") ? (qint64) json["timeTo"].toDouble(-1) : -1;
    f.m_hideDuplicates = json["hideDuplicates"].toBool(false);
    f.m_voiceOnly = json["voiceOnly"].toBool(false);
    return f;
}

// ---------------------------------------------------------------------------
// File sink and source
// ---------------------------------------------------------------------------

MessageCenterFileSink::MessageCenterFileSink(const QString& name, const QString& path, Format format) :
    m_name(name),
    m_path(path),
    m_format(format)
{
}

bool MessageCenterFileSink::accept(const MessageRecord& record, QString& error)
{
    QFile file(m_path);

    if (!file.open(QIODevice::Append | QIODevice::Text))
    {
        error = QString("Cannot write %1").arg(m_path);
        return false;
    }

    QByteArray line;

    if (m_format == JsonLines) {
        line = record.toJsonLine();
    }
    else
    {
        if (file.size() == 0) {
            line += MessageRecord::csvHeader().join(',').toUtf8() + "\n";
        }

        line += record.toCSV().toUtf8() + "\n";
    }

    if (file.write(line) != line.size())
    {
        error = QString("Cannot write %1").arg(m_path);
        return false;
    }

    return true;
}

MessageCenterFileSource::MessageCenterFileSource(const QString& name, const QString& path) :
    m_name(name),
    m_path(path)
{
}

bool MessageCenterFileSource::poll(std::vector<MessageRecord>& records, QString& error)
{
    QFile file(m_path);

    if (!file.open(QIODevice::ReadOnly))
    {
        error = QString("Cannot read %1").arg(m_path);
        return false;
    }

    if (file.size() < m_offset) {
        m_offset = 0;           // The file was replaced
    }

    file.seek(m_offset);
    int lines = 0;
    m_more = false;

    // A big file is read over several polls, and a line has a limit
    while (!file.atEnd() && (lines++ < m_maxLinesPerPoll))
    {
        qint64 start = file.pos();
        QByteArray line = file.readLine(m_maxLineLength);

        if (!line.endsWith('\n') && (line.size() >= m_maxLineLength - 1))
        {
            // Not a record: skip to the end of the line
            file.readLine();
            m_offset = file.pos();
            m_skipped++;
            continue;
        }

        // A line being written is read when it is complete
        if (!line.endsWith('\n'))
        {
            file.seek(start);
            break;
        }

        m_offset = file.pos();
        line = line.trimmed();

        if (line.isEmpty()) {
            continue;
        }

        QJsonParseError parseError;
        QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        MessageRecord record;

        if ((parseError.error != QJsonParseError::NoError) || !document.isObject()
            || !MessageRecord::fromJson(document.object(), record))
        {
            m_skipped++;
            continue;
        }

        records.push_back(record);
    }

    // Stopped by the number of lines: the rest is for the next poll
    m_more = (lines > m_maxLinesPerPoll) && !file.atEnd();
    return true;
}

// ---------------------------------------------------------------------------
// CSV source
// ---------------------------------------------------------------------------

MessageCenterCsvSource::MessageCenterCsvSource(const QString& name, const QString& path, bool newRowsOnly) :
    m_name(name),
    m_path(path),
    m_newRowsOnly(newRowsOnly)
{
}

bool MessageCenterCsvSource::poll(std::vector<MessageRecord>& records, QString& error)
{
    QFile file(m_path);

    if (!file.open(QIODevice::ReadOnly))
    {
        error = QString("Cannot read %1").arg(m_path);
        return false;
    }

    if (file.size() < m_offset)
    {
        // The file was replaced
        m_offset = 0;
        m_headerRead = false;
        m_notReported = true;
    }

    file.seek(m_offset);
    int lines = 0;
    m_more = false;

    while (!file.atEnd() && (lines++ < m_maxLinesPerPoll))
    {
        qint64 start = file.pos();
        QByteArray line = file.readLine(m_maxLineLength);

        if (!line.endsWith('\n') && (line.size() >= m_maxLineLength - 1))
        {
            // Not a row: skip to the end of the line
            file.readLine();
            m_offset = file.pos();
            m_skipped++;
            continue;
        }

        // A line being written is read when it is complete
        if (!line.endsWith('\n'))
        {
            file.seek(start);
            break;
        }

        m_offset = file.pos();
        QString text = QString::fromUtf8(line);

        if (text.trimmed().isEmpty()) {
            continue;
        }

        QTextStream in(&text, QIODevice::ReadOnly);
        QStringList row;

        if (!CSV::readRow(in, &row)) {
            m_skipped++;
            continue;
        }

        if (!m_headerRead)
        {
            m_columns.clear();

            for (int i = 0; i < row.size(); i++) {
                m_columns.insert(row[i].trimmed(), i);
            }

            m_headerRead = true;
            bool recordLog = m_columns.contains("id") && m_columns.contains("timestamp") && m_columns.contains("protocol");
            bool pagerLog = true;

            for (const QString& column : RadioPagingPagerFormat::csvColumns()) {
                pagerLog = pagerLog && m_columns.contains(column);
            }

            m_pagerFormat = !recordLog && pagerLog;

            if (!recordLog && !pagerLog)
            {
                // Not a log: nothing is read from this file until it is replaced
                m_columns.clear();
                m_offset = file.size();

                if (m_notReported)
                {
                    m_notReported = false;
                    error = QString("%1 is not a log of messages (the first row must name the columns)").arg(m_path);
                    return false;
                }

                return true;
            }

            // The rows written before: skipped, the next polls read the new ones
            if (m_newRowsOnly && !m_skipped0)
            {
                m_skipped0 = true;
                m_offset = file.size();
                return true;
            }

            continue;
        }

        if (m_columns.isEmpty()) {
            continue;
        }

        MessageRecord record;
        bool ok = m_pagerFormat ? RadioPagingPagerFormat::fromCsvRow(m_columns, row, record)
            : MessageRecord::fromCSV(m_columns, row, record);

        if (ok) {
            records.push_back(record);
        } else {
            m_skipped++;
        }
    }

    m_more = (lines > m_maxLinesPerPoll) && !file.atEnd();
    return true;
}

// ---------------------------------------------------------------------------
// UDP source and sink
// ---------------------------------------------------------------------------

MessageCenterUdpSource::MessageCenterUdpSource(const QString& name, const QString& address, quint16 port) :
    m_name(name),
    m_socket(new QUdpSocket())
{
    QHostAddress host(address.isEmpty() ? QString("127.0.0.1") : address);

    if (host.isNull()) {
        m_bindError = QString("%1 is not an IP address").arg(address);
    } else if ((port == 0) || !m_socket->bind(host, port)) {
        m_bindError = QString("Cannot listen on %1:%2: %3").arg(host.toString()).arg(port).arg(m_socket->errorString());
    }
}

MessageCenterUdpSource::~MessageCenterUdpSource()
{
    delete m_socket;
}

bool MessageCenterUdpSource::poll(std::vector<MessageRecord>& records, QString& error)
{
    if (!m_bindError.isEmpty())
    {
        // Said once: the source stays there, nothing is received
        if (m_errorReported) {
            return true;
        }

        m_errorReported = true;
        error = m_bindError;
        return false;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (int n = 0; (n < m_maxDatagramsPerPoll) && m_socket->hasPendingDatagrams(); n++)
    {
        qint64 size = m_socket->pendingDatagramSize();

        if ((size < 0) || (size > m_maxDatagramSize))
        {
            // Too big to be a message
            QByteArray discard(1, 0);
            m_socket->readDatagram(discard.data(), 1);
            m_skipped++;
            continue;
        }

        QByteArray datagram(size, 0);
        m_socket->readDatagram(datagram.data(), size);
        std::vector<MessageRecord> found;

        if (datagram.trimmed().startsWith('{'))
        {
            for (const QByteArray& line : datagram.split('\n'))
            {
                QJsonParseError parseError;
                QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &parseError);
                MessageRecord record;

                if ((parseError.error == QJsonParseError::NoError) && document.isObject() && MessageRecord::fromJson(document.object(), record)) {
                    found.push_back(record);
                } else if (!line.trimmed().isEmpty()) {
                    m_skipped++;
                }
            }
        }
        else
        {
            MessageRecord record;

            if (RadioPagingPagerFormat::fromDatagram(datagram, now, record)) {
                found.push_back(record);
            } else {
                m_skipped++;
            }
        }

        for (MessageRecord& record : found)
        {
            // Received from the network: a message received, never a page for a transmitter
            record.m_direction = MessageRecord::DirectionRX;
            record.m_protocolData.remove("transmitter");

            if (record.m_kind == MessageRecord::KindEvent) {
                continue;
            }

            records.push_back(record);
        }
    }

    return true;
}

MessageCenterUdpSink::MessageCenterUdpSink(const QString& name, const QString& address, quint16 port, Format format) :
    m_name(name),
    m_addressText(address),
    m_port(port),
    m_format(format),
    m_socket(new QUdpSocket()),
    m_address(address)
{
    m_addressValid = !m_address.isNull() && (port != 0);
}

MessageCenterUdpSink::~MessageCenterUdpSink()
{
    delete m_socket;
}

bool MessageCenterUdpSink::accept(const MessageRecord& record, QString& error)
{
    if (!m_addressValid)
    {
        error = QString("%1: %2:%3 is not an address to send to").arg(m_name).arg(m_addressText).arg(m_port);
        return false;
    }

    QByteArray datagram = (m_format == Json) ? record.toJsonLine() : RadioPagingPagerFormat::datagram(record);

    // The end of a voice message, an event: nothing to send in the format
    if (datagram.isEmpty()) {
        return true;
    }
    if (datagram.size() > m_maxDatagramSize)
    {
        error = QString("%1: the message is too long for a datagram").arg(m_name);
        return false;
    }
    if (m_socket->writeDatagram(datagram, m_address, m_port) != datagram.size())
    {
        error = QString("%1: %2").arg(m_name).arg(m_socket->errorString());
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

MessageCenterCommandSink::MessageCenterCommandSink(const QString& name, const QString& command, Launcher launcher) :
    m_name(name),
    m_command(command),
    m_launcher(launcher)
{
    if (!m_launcher)
    {
        m_launcher = [](const QString& program, const QStringList& arguments) {
            return QProcess::startDetached(program, arguments);
        };
    }
}

bool MessageCenterCommandSink::accept(const MessageRecord& record, const QStringList& captures, QString& error)
{
    // The messages only: not an update that completes one, nor a diagnostic
    if ((record.m_kind != MessageRecord::KindMessage) || RadioPagingPagerFormat::isCompletion(record)) {
        return true;
    }

    QStringList arguments = QProcess::splitCommand(m_command);

    if (arguments.isEmpty())
    {
        error = QString("%1: no command").arg(m_name);
        return false;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    while (!m_started.empty() && (m_started.front() <= now - 1000)) {
        m_started.pop_front();
    }

    if ((int) m_started.size() >= m_maxPerSecond)
    {
        error = QString("%1: more than %2 commands in a second, this one was not run").arg(m_name).arg(m_maxPerSecond);
        return false;
    }

    // The program is the one set; the text of the message is only ever an argument
    QString program = arguments.takeFirst();

    for (QString& argument : arguments) {
        argument = messageCenterSubstitute(argument, record, captures);
    }

    m_started.push_back(now);

    if (!m_launcher(program, arguments))
    {
        error = QString("%1: cannot run %2").arg(m_name).arg(program);
        return false;
    }

    return true;
}

MessageCenterActionSink::MessageCenterActionSink(const QString& name, const QString& type, const QString& text, quint32 color, Action action) :
    m_name(name),
    m_type(type),
    m_text(text),
    m_color(color),
    m_action(action)
{
}

bool MessageCenterActionSink::accept(const MessageRecord& record, const QStringList& captures, QString& error)
{
    (void) error;

    if ((record.m_kind != MessageRecord::KindMessage) || RadioPagingPagerFormat::isCompletion(record)) {
        return true;
    }

    if (m_action) {
        m_action(m_type, record, messageCenterSubstitute(m_text, record, captures), m_color);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Endpoint
// ---------------------------------------------------------------------------

// Page
// ---------------------------------------------------------------------------

QString MessageCenterPage::stateName(State state)
{
    static const char *names[] = {"pending", "queued", "claimed", "transmitted", "sent", "dead"};
    return names[(int) state];
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

MessageCenterEngine::MessageCenterEngine() :
    m_logSize(m_defaultLogSize),
    m_routes(defaultRoutes())
{
}

void MessageCenterEngine::addSink(std::unique_ptr<MessageCenterSink> sink)
{
    removeSink(sink->name());
    m_sinks.push_back(std::move(sink));
}

bool MessageCenterEngine::removeSink(const QString& name)
{
    for (auto it = m_sinks.begin(); it != m_sinks.end(); ++it)
    {
        if ((*it)->name() == name)
        {
            m_sinks.erase(it);
            return true;
        }
    }

    return false;
}

QStringList MessageCenterEngine::sinkNames() const
{
    QStringList names({"log", "queue"});

    for (const auto& sink : m_sinks) {
        names.append(sink->name());
    }

    return names;
}

void MessageCenterEngine::addSource(std::unique_ptr<MessageCenterSource> source)
{
    removeSource(source->name());
    m_sources.push_back(std::move(source));
}

bool MessageCenterEngine::removeSource(const QString& name)
{
    for (auto it = m_sources.begin(); it != m_sources.end(); ++it)
    {
        if ((*it)->name() == name)
        {
            m_sources.erase(it);
            return true;
        }
    }

    return false;
}

QStringList MessageCenterEngine::sourceNames() const
{
    QStringList names;

    for (const auto& source : m_sources) {
        names.append(source->name());
    }

    return names;
}

int MessageCenterEngine::pollSources(QStringList& errors)
{
    int taken = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (const auto& source : m_sources)
    {
        std::vector<MessageRecord> records;
        QString error;

        if (!source->poll(records, error)) {
            errors.append(QString("%1: %2").arg(source->name()).arg(error));
        }

        for (const MessageRecord& record : records) {
            ingest(record, source->name(), now);
        }

        taken += (int) records.size();
    }

    return taken;
}

void MessageCenterEngine::setLogSize(int size)
{
    m_logSize = std::max(1, size);

    while ((int) m_log.size() > m_logSize) {
        m_log.pop_front();
    }
}

// The same id is the same message: the later record (a voice message that ended, a page that was
// sent) completes the earlier one
void MessageCenterEngine::store(const MessageRecord& record)
{
    m_logged++;

    if (!record.m_id.isEmpty())
    {
        for (auto it = m_log.rbegin(); it != m_log.rend(); ++it)
        {
            if (it->m_id != record.m_id) {
                continue;
            }

            QVariantMap data = it->m_protocolData;

            for (auto d = record.m_protocolData.begin(); d != record.m_protocolData.end(); ++d) {
                data.insert(d.key(), d.value());
            }

            // What the later record sets completes the earlier one; what it leaves out stays
            MessageRecord merged = *it;
            merged.m_protocolData = data;

            if (!record.m_text.isEmpty() && (it->m_partial || !record.m_partial)) {
                merged.m_text = record.m_text;
                merged.m_charStatus = record.m_charStatus;
            }
            if (!record.m_msgType.isEmpty()) {
                merged.m_msgType = record.m_msgType;
            }
            if (!record.m_address.isEmpty()) {
                merged.m_address = record.m_address;
            }
            if (!record.m_mode.isEmpty()) {
                merged.m_mode = record.m_mode;
            }
            if (!record.m_channelAlias.isEmpty()) {
                merged.m_channelAlias = record.m_channelAlias;
            }
            if (!record.m_protocol.isEmpty()) {
                merged.m_protocol = record.m_protocol;
            }
            if (record.m_rawBits >= 0) {
                merged.m_rawData = record.m_rawData;
                merged.m_rawBits = record.m_rawBits;
            }
            if (record.m_speed > 0) {
                merged.m_speed = record.m_speed;
            }
            if (record.m_frequency >= 0) {
                merged.m_frequency = record.m_frequency;
            }
            if (record.m_polarity != MessageRecord::PolarityUnknown) {
                merged.m_polarity = record.m_polarity;
            }

            // Only an update of a message still partial leaves it partial
            merged.m_partial = it->m_partial && record.m_partial;
            *it = merged;
            appendHistory(merged);
            return;
        }
    }

    m_log.push_back(record);
    appendHistory(record);

    while ((int) m_log.size() > m_logSize) {
        m_log.pop_front();
    }
}

bool MessageCenterEngine::replace(const MessageRecord& record)
{
    for (auto it = m_log.rbegin(); it != m_log.rend(); ++it)
    {
        if (it->m_id == record.m_id)
        {
            *it = record;
            m_logged++;
            appendHistory(record);
            return true;
        }
    }

    return false;
}

void MessageCenterEngine::clearLog()
{
    QString error;

    m_log.clear();
    m_logged++;     // The views show the empty log
    if (!m_historyPath.isEmpty() && !writeHistory(error)) {
        qWarning("MessageCenterEngine::clearLog: %s", qPrintable(error));
    }
}

bool MessageCenterEngine::setHistory(const QString& path, qint64 maxBytes, QString& error)
{
    m_historyMaxBytes = std::max((qint64) 1, maxBytes);

    if (path == m_historyPath)
    {
        return ((m_historyBytes <= m_historyMaxBytes) || m_historyPath.isEmpty()) ? true : writeHistory(error);
    }

    m_historyPath = path;
    m_historyBytes = 0;

    if (m_historyPath.isEmpty()) {
        return true;
    }

    // Messages in the log already (the history turned on later): the file starts with them
    if (!m_log.empty()) {
        return writeHistory(error);
    }

    QFile file(m_historyPath);

    if (!file.exists()) {
        return writeHistory(error);
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QString("%1: %2").arg(m_historyPath, file.errorString());
        return false;
    }

    // The last version of each message, in the order the messages came; the newest logSize of them
    std::vector<MessageRecord> records;
    QMap<QString, size_t> index;
    int lines = 0;

    while (!file.atEnd())
    {
        const QByteArray line = file.readLine().trimmed();
        MessageRecord record;

        if (line.isEmpty()) {
            continue;
        }

        lines++;

        if (!MessageRecord::fromJson(QJsonDocument::fromJson(line).object(), record)) {
            continue;
        }
        if (!record.m_id.isEmpty() && index.contains(record.m_id))
        {
            records[index[record.m_id]] = record;
            continue;
        }
        if (!record.m_id.isEmpty()) {
            index.insert(record.m_id, records.size());
        }

        records.push_back(record);
    }

    m_historyBytes = file.size();
    file.close();

    const size_t first = (records.size() > (size_t) m_logSize) ? records.size() - (size_t) m_logSize : 0;

    for (size_t i = first; i < records.size(); i++) {
        m_log.push_back(records[i]);
    }

    m_logged += (int) m_log.size();     // The views show the log loaded

    // Other versions of the messages or older messages in the file: written again with the log alone
    if ((lines != (int) m_log.size()) || (m_historyBytes > m_historyMaxBytes)) {
        return writeHistory(error);
    }

    return true;
}

void MessageCenterEngine::appendHistory(const MessageRecord& record)
{
    if (m_historyPath.isEmpty()) {
        return;
    }

    QString error;
    QFile file(m_historyPath);
    const QByteArray line = record.toJsonLine();

    if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || (file.write(line) != line.size()))
    {
        qWarning("MessageCenterEngine::appendHistory: %s: %s", qPrintable(m_historyPath), qPrintable(file.errorString()));
        return;
    }

    file.close();
    m_historyBytes += line.size();

    // Over the limit: the newest messages of the log (the record is in it)
    if ((m_historyBytes > m_historyMaxBytes) && !writeHistory(error)) {
        qWarning("MessageCenterEngine::appendHistory: %s", qPrintable(error));
    }
}

bool MessageCenterEngine::writeHistory(QString& error)
{
    QSaveFile file(m_historyPath);

    if (!file.open(QIODevice::WriteOnly))
    {
        error = QString("%1: %2").arg(m_historyPath, file.errorString());
        return false;
    }

    // The newest messages of the log up to half the size limit: the file grows again before it is written
    // again
    std::vector<QByteArray> lines;
    qint64 bytes = 0;

    for (auto it = m_log.rbegin(); it != m_log.rend(); ++it)
    {
        QByteArray line = it->toJsonLine();

        if (bytes + line.size() > m_historyMaxBytes / 2) {
            break;
        }

        bytes += line.size();
        lines.push_back(std::move(line));
    }

    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        file.write(*it);
    }

    if (!file.commit())
    {
        error = QString("%1: %2").arg(m_historyPath, file.errorString());
        return false;
    }

    m_historyBytes = bytes;
    return true;
}

void MessageCenterEngine::deliver(const MessageRecord& record, const QString& sinkName, const QStringList& captures, qint64 now)
{
    if (sinkName == "log")
    {
        store(record);
        m_sinkStats[sinkName].m_accepted++;
        return;
    }
    if (sinkName == "queue")
    {
        // Only a message for a transmitter becomes a page
        if ((record.m_direction == MessageRecord::DirectionTX) && !record.m_protocolData.value("transmitter").toString().isEmpty())
        {
            addPage(record, now);
            m_sinkStats[sinkName].m_accepted++;
        }

        return;
    }

    for (const auto& sink : m_sinks)
    {
        if (sink->name() == sinkName)
        {
            QString error;

            if (sink->accept(record, captures, error))
            {
                m_sinkStats[sinkName].m_accepted++;
            }
            else
            {
                m_sinkStats[sinkName].m_failed++;

                if (m_sinkErrors.size() < m_maxSinkErrors) {
                    m_sinkErrors.append(error);
                }
            }

            return;
        }
    }
}

void MessageCenterEngine::importRecord(const MessageRecord& record)
{
    store(record);
}

QString MessageCenterEngine::endpointKey(MessageEndpoint::Role role, const QString& alias)
{
    return QString(role == MessageEndpoint::Transmitter ? "tx:" : "rx:") + alias;
}

void MessageCenterEngine::ingest(const MessageRecord& record, const QString& source, qint64 now)
{
    const QVariantMap& d = record.m_protocolData;

    // The announcements of the channels and the states of the pages come from the channels, not
    // from a file that is read (a replayed file must not move the live pages)
    if (record.m_kind == MessageRecord::KindEvent)
    {
        if (sourceNames().contains(source)) {
            return;
        }

        MessageEndpoint endpoint;
        QString event;

        if (MessageEndpoint::fromRecord(record, endpoint, event))
        {
            // A receiver and a transmitter may have the same alias (the frequency): they are two endpoints
            QString key = endpointKey(endpoint.m_role, endpoint.m_alias);

            // A channel whose alias changed (the alias is the frequency by default): its entry of the old alias goes
            if ((record.m_deviceSetIndex >= 0) && (record.m_channelIndex >= 0))
            {
                const QString channel = QString("%1:%2").arg(record.m_deviceSetIndex).arg(record.m_channelIndex);

                for (auto it = m_endpointChannels.begin(); it != m_endpointChannels.end();)
                {
                    if ((it.value() == channel) && (it.key() != key) && m_endpoints.contains(it.key())
                        && (m_endpoints[it.key()].m_role == endpoint.m_role))
                    {
                        if (endpoint.m_role == MessageEndpoint::Transmitter) {
                            endpointGone(m_endpoints[it.key()].m_alias, now);
                        }

                        m_endpoints.remove(it.key());
                        it = m_endpointChannels.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }

                m_endpointChannels[key] = channel;
            }

            // Every announcement (status too) carries the whole description: the protocols, speeds and
            // capabilities follow the settings of the channel
            m_endpoints[key] = endpoint;

            if ((m_endpoints[key].m_status == MessageEndpoint::Offline) && (endpoint.m_role == MessageEndpoint::Transmitter)) {
                endpointGone(endpoint.m_alias, now);
            }

            return;
        }
        if (d.value("event").toString() == "page")
        {
            applyEvent(record.m_id, d.value("state").toString(), d.value("reason").toString(), now);
            return;
        }
    }

    // The messages: each route that selects it sends it to its sinks, in the order of the routes, until one that
    // stops. The log is a sink as the others: the default route sends every message to it
    bool matched = false;

    for (const Route& route : m_routes)
    {
        if (!route.m_enabled || !route.m_filter.matches(record, source)) {
            continue;
        }

        matched = true;
        MessageRecord decoded;
        QString error;
        const bool redecoded = route.decodes()
            && RadioPagingRecode::redecode(record, route.m_decodeCharset, route.m_decodeType, decoded, error);
        const MessageRecord& routed = redecoded ? decoded : record;
        QStringList captures = route.m_filter.captures(routed);

        for (const QString& sink : route.m_sinks) {
            deliver(routed, sink, captures, now);
        }

        if (route.m_stop) {
            break;
        }
    }

    if (matched) {
        m_routed++;
    }
}

std::vector<MessageCenterEngine::Route> MessageCenterEngine::defaultRoutes()
{
    Route all;
    all.m_name = "All messages to the log";
    all.m_sinks = QStringList("log");
    return {all};
}

std::vector<MessageRecord> MessageCenterEngine::select(const MessageCenterFilter& filter) const
{
    std::vector<MessageRecord> out;

    for (const MessageRecord& record : m_log)
    {
        if (filter.matches(record)) {
            out.push_back(record);
        }
    }

    return out;
}

const MessageRecord *MessageCenterEngine::find(const QString& id) const
{
    for (auto it = m_log.rbegin(); it != m_log.rend(); ++it)
    {
        if (it->m_id == id) {
            return &*it;
        }
    }

    return nullptr;
}

QStringList MessageCenterEngine::eligible(const MessageRecord& page) const
{
    QStringList aliases;

    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it)
    {
        if ((it->m_status != MessageEndpoint::Offline) && it->canTransmit(page)) {
            aliases.append(it->m_alias);
        }
    }

    return aliases;
}

QString MessageCenterEngine::addPage(MessageRecord record, qint64 now)
{
    if (record.m_id.isEmpty()) {
        record.m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    // A page has its transmitter and an id of its own
    if (record.m_protocolData.value("transmitter").toString().isEmpty() || findPage(record.m_id)) {
        return QString();
    }

    record.m_direction = MessageRecord::DirectionTX;
    record.m_kind = MessageRecord::KindMessage;

    if (record.m_timestamp <= 0) {
        record.m_timestamp = now;
    }

    MessageCenterPage page;
    page.m_record = record;
    page.m_changed = now;
    m_pages.push_back(page);
    store(record);
    return record.m_id;
}

MessageCenterPage *MessageCenterEngine::findPage(const QString& id)
{
    for (MessageCenterPage& page : m_pages)
    {
        if (page.m_record.m_id == id) {
            return &page;
        }
    }

    return nullptr;
}

const MessageCenterPage *MessageCenterEngine::page(const QString& id) const
{
    for (const MessageCenterPage& page : m_pages)
    {
        if (page.m_record.m_id == id) {
            return &page;
        }
    }

    return nullptr;
}

std::vector<MessageCenterPage> MessageCenterEngine::pages(int state) const
{
    std::vector<MessageCenterPage> out;

    for (const MessageCenterPage& page : m_pages)
    {
        if ((state < 0) || ((int) page.m_state == state)) {
            out.push_back(page);
        }
    }

    return out;
}

// Pending -> queued when the transmitter named on the page is there and can send it; queued ->
// dispatched when it is ready. A page that has used its attempts is dead
std::vector<MessageCenterPage> MessageCenterEngine::dispatch(qint64 now)
{
    std::vector<MessageCenterPage> out;
    QMap<QString, int> outstanding;

    for (const MessageCenterPage& page : m_pages)
    {
        if (page.m_dispatched) {
            outstanding[page.transmitter()]++;
        }
    }

    for (MessageCenterPage& page : m_pages)
    {
        const QString alias = page.transmitter();
        auto endpoint = m_endpoints.constFind(endpointKey(MessageEndpoint::Transmitter, alias));

        if (page.m_state == MessageCenterPage::Pending)
        {
            if ((endpoint != m_endpoints.constEnd()) && (endpoint->m_status != MessageEndpoint::Offline)
                && endpoint->canTransmit(page.m_record))
            {
                page.m_state = MessageCenterPage::Queued;
                page.m_changed = now;
            }
        }

        if ((page.m_state != MessageCenterPage::Queued) || page.m_dispatched) {
            continue;
        }

        if (page.m_attempts >= m_maxAttempts)
        {
            page.m_state = MessageCenterPage::Dead;
            page.m_reason = "attempts";
            page.m_changed = now;
            continue;
        }

        if ((endpoint == m_endpoints.constEnd()) || !endpoint->ready()
            || ((endpoint->m_capacity > 0) && (endpoint->m_queueDepth + outstanding[alias] >= endpoint->m_capacity))) {
            continue;
        }

        page.m_dispatched = true;
        page.m_dispatchedAt = now;
        page.m_changed = now;
        outstanding[alias]++;
        out.push_back(page);
    }

    return out;
}

bool MessageCenterEngine::applyEvent(const QString& id, const QString& state, const QString& reason, qint64 now)
{
    MessageCenterPage *page = findPage(id);

    if (!page) {
        return false;
    }

    auto failed = [&](const QString& why)
    {
        // Out of attempts: dead. Else it goes back to the queue to be sent again
        page->m_dispatched = false;
        page->m_claimedBy.clear();
        page->m_changed = now;

        if (page->m_attempts >= m_maxAttempts)
        {
            page->m_state = MessageCenterPage::Dead;
            page->m_reason = QString("attempts (%1)").arg(why);
        }
        else
        {
            page->m_state = MessageCenterPage::Queued;
            page->m_retransmits++;
            page->m_reason = why;
        }
    };

    // A late or repeated report does not move a page that is somewhere else already
    const MessageCenterPage::State current = page->m_state;
    const bool claimed = (current == MessageCenterPage::Claimed);
    const bool transmitted = (current == MessageCenterPage::Transmitted);

    if (state == "claimed")
    {
        if ((current != MessageCenterPage::Queued) && (current != MessageCenterPage::Pending)) {
            return false;
        }
        if (page->m_attempts >= m_maxAttempts)
        {
            page->m_state = MessageCenterPage::Dead;
            page->m_reason = "attempts";
            page->m_dispatched = false;
            page->m_changed = now;
            return false;
        }

        page->m_state = MessageCenterPage::Claimed;
        page->m_attempts++;
        page->m_dispatched = false;
        page->m_claimedBy = page->transmitter();
        page->m_reason.clear();
    }
    else if (state == "transmitted")
    {
        if (!claimed) {
            return false;
        }

        page->m_state = MessageCenterPage::Transmitted;
    }
    else if (state == "sent")
    {
        if (!claimed && !transmitted) {
            return false;
        }

        page->m_state = MessageCenterPage::Sent;
        page->m_dispatched = false;
        page->m_reason.clear();
    }
    else if (state == "rejected")
    {
        if ((current == MessageCenterPage::Sent) || (current == MessageCenterPage::Dead) || transmitted) {
            return false;
        }

        page->m_state = MessageCenterPage::Dead;
        page->m_reason = reason.isEmpty() ? QString("rejected") : QString("rejected: %1").arg(reason);
        page->m_dispatched = false;
    }
    else if ((state == "aborted") || (state == "underrun") || (state == "interrupted"))
    {
        if (!claimed && !transmitted) {
            return false;
        }

        failed(state);
    }
    else if (state == "released")
    {
        // Nothing went on air: it waits for the transmitter
        if (!claimed && (current != MessageCenterPage::Queued)) {
            return false;
        }

        page->m_state = MessageCenterPage::Pending;
        page->m_dispatched = false;
        page->m_claimedBy.clear();
    }
    else
    {
        return false;
    }

    page->m_changed = now;
    return true;
}

// A transmitter gone: what it had claimed and not transmitted waits for it; what it was
// transmitting is interrupted
int MessageCenterEngine::removeEndpoints(const QStringList& keys, bool offline, qint64 now)
{
    int removed = 0;

    for (auto it = m_endpoints.begin(); it != m_endpoints.end();)
    {
        if ((offline && (it->m_status == MessageEndpoint::Offline)) || keys.contains(it.key()))
        {
            if (it->m_role == MessageEndpoint::Transmitter) {
                endpointGone(it->m_alias, now);
            }

            m_endpointChannels.remove(it.key());
            it = m_endpoints.erase(it);
            removed++;
        }
        else
        {
            ++it;
        }
    }

    return removed;
}

void MessageCenterEngine::endpointGone(const QString& alias, qint64 now)
{
    for (MessageCenterPage& page : m_pages)
    {
        if (page.transmitter() != alias) {
            continue;
        }

        if ((page.m_state == MessageCenterPage::Claimed) || (page.m_state == MessageCenterPage::Queued))
        {
            page.m_state = MessageCenterPage::Pending;
            page.m_dispatched = false;
            page.m_claimedBy.clear();
            page.m_changed = now;
        }
        else if (page.m_state == MessageCenterPage::Transmitted) {
            applyEvent(page.m_record.m_id, "interrupted", QString(), now);
        }
    }
}

void MessageCenterEngine::expireClaims(qint64 now, qint64 timeoutMs)
{
    for (MessageCenterPage& page : m_pages)
    {
        if (!page.m_dispatched || (now - page.m_dispatchedAt < timeoutMs)) {
            continue;
        }

        page.m_dispatched = false;
        page.m_attempts++;
        page.m_retransmits++;
        page.m_changed = now;

        if (page.m_attempts >= m_maxAttempts)
        {
            page.m_state = MessageCenterPage::Dead;
            page.m_reason = "attempts (claim timed out)";
        }
        else {
            page.m_reason = "claim timed out";
        }
    }
}

// A channel that has not announced itself for this long is gone: its transmitter's pages are
// released or interrupted, it is offline until it announces again
void MessageCenterEngine::sweep(qint64 now, qint64 silentMs)
{
    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it)
    {
        if ((it->m_status == MessageEndpoint::Offline) || (now - it->m_lastSeen <= silentMs)) {
            continue;
        }

        it->m_status = MessageEndpoint::Offline;

        if (it->m_role == MessageEndpoint::Transmitter) {
            endpointGone(it->m_alias, now);
        }
    }
}

QStringList MessageCenterEngine::takeSinkErrors()
{
    QStringList errors = m_sinkErrors;
    m_sinkErrors.clear();
    return errors;
}

bool MessageCenterEngine::requeue(const QString& id, qint64 now)
{
    MessageCenterPage *page = findPage(id);

    if (!page || (page->m_state != MessageCenterPage::Dead)) {
        return false;
    }

    page->m_state = MessageCenterPage::Pending;
    page->m_attempts = 0;
    page->m_retransmits = 0;
    page->m_dispatched = false;
    page->m_claimedBy.clear();
    page->m_reason.clear();
    page->m_changed = now;
    return true;
}

// A page nobody has claimed can be withdrawn
bool MessageCenterEngine::cancel(const QString& id)
{
    for (auto it = m_pages.begin(); it != m_pages.end(); ++it)
    {
        if (it->m_record.m_id != id) {
            continue;
        }
        if (!it->m_dispatched && ((it->m_state == MessageCenterPage::Pending) || (it->m_state == MessageCenterPage::Queued)
            || (it->m_state == MessageCenterPage::Dead)))
        {
            m_pages.erase(it);
            return true;
        }

        return false;
    }

    return false;
}

int MessageCenterEngine::removeFinished()
{
    size_t before = m_pages.size();
    m_pages.erase(std::remove_if(m_pages.begin(), m_pages.end(),
        [](const MessageCenterPage& p) { return p.m_state == MessageCenterPage::Sent; }), m_pages.end());
    return (int) (before - m_pages.size());
}
