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
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

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
    if (!m_address.isEmpty())
    {
        QRegularExpression expression(QRegularExpression::anchoredPattern(QRegularExpression::wildcardToRegularExpression(m_address)));

        if (!expression.match(record.m_address).hasMatch()) {
            return false;
        }
    }
    if (!m_text.isEmpty() && !record.m_text.contains(m_text, Qt::CaseInsensitive)) {
        return false;
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

    f.m_direction = json.contains("direction") ? json["direction"].toInt(-1) : -1;
    f.m_kind = json.contains("kind") ? json["kind"].toInt(-1) : -1;
    f.m_polarity = json.contains("polarity") ? json["polarity"].toInt(-1) : -1;
    f.m_speeds = intsFromJson(json["speeds"]);
    f.m_address = json["address"].toString();
    f.m_text = json["text"].toString();
    f.m_msgType = json["msgType"].toString();
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
    m_logSize(m_defaultLogSize)
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
            return;
        }
    }

    m_log.push_back(record);

    while ((int) m_log.size() > m_logSize) {
        m_log.pop_front();
    }
}

void MessageCenterEngine::deliver(const MessageRecord& record, const QString& sinkName, qint64 now)
{
    if (sinkName == "log")
    {
        store(record);
        return;
    }
    if (sinkName == "queue")
    {
        // Only a message for a transmitter becomes a page
        if ((record.m_direction == MessageRecord::DirectionTX) && !record.m_protocolData.value("transmitter").toString().isEmpty()) {
            addPage(record, now);
        }

        return;
    }

    for (const auto& sink : m_sinks)
    {
        if (sink->name() == sinkName)
        {
            QString error;

            if (!sink->accept(record, error) && (m_sinkErrors.size() < m_maxSinkErrors)) {
                m_sinkErrors.append(error);
            }

            return;
        }
    }
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

            // A status record changes the status and the queue, the capabilities stay as registered
            if ((event == "status") && m_endpoints.contains(key))
            {
                MessageEndpoint& current = m_endpoints[key];
                current.m_status = endpoint.m_status;
                current.m_queueDepth = endpoint.m_queueDepth;
                current.m_capacity = endpoint.m_capacity;
                current.m_lastSeen = endpoint.m_lastSeen;
            }
            else {
                m_endpoints[key] = endpoint;
            }

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

    // The messages: routed. Without a route that matches they are logged
    bool matched = false;

    for (const Route& route : m_routes)
    {
        if (!route.m_enabled || !route.m_filter.matches(record)) {
            continue;
        }

        matched = true;

        for (const QString& sink : route.m_sinks) {
            deliver(record, sink, now);
        }

        if (route.m_stop) {
            break;
        }
    }

    if (matched) {
        m_routed++;
    } else {
        deliver(record, "log", now);
    }
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
