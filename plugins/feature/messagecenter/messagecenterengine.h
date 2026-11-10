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

#ifndef INCLUDE_MESSAGECENTERENGINE_H
#define INCLUDE_MESSAGECENTERENGINE_H

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <deque>
#include <memory>
#include <vector>

#include "util/messagerecord.h"

// The message center without the user interface: where the messages come from (sources), where
// they go (sinks) and which way (routes), the log, the endpoints (the channels that registered)
// and the queue of the pages to transmit.
//
//   source ---> route (filter) ---> sink(s)
//   sources: the channels (through the "messages" pipe, fed with ingest), a JSON lines file
//   sinks:   the log, a JSON lines file, a CSV file, the transmit queue
//
// Endpoints (demodulators and modulators) announce themselves with event records (see
// MessageCenterEndpoint::fromRecord). A page for transmission goes through the states
// pending -> queued -> claimed -> transmitted -> sent; the modulator claims it, and reports
// the states back with event records of the same id (see MessageCenterEngine::applyEvent).
// No Qt GUI and no thread: the caller (the feature) serializes the calls.

// The records a filter selects: every field set must match
struct MessageCenterFilter
{
    QStringList m_protocols;            //!< Lower case protocol names, empty for all
    QStringList m_aliases;              //!< Channel aliases, empty for all
    int m_direction = -1;               //!< MessageRecord::Direction, -1 for both
    int m_kind = -1;                    //!< MessageRecord::Kind, -1 for all
    int m_polarity = -1;                //!< MessageRecord::Polarity, -1 for all
    QList<int> m_speeds;                //!< Bits per second, empty for all
    QString m_address;                  //!< Wildcards * and ?, empty for all
    QString m_text;                     //!< Contained in the text, not case sensitive
    QString m_msgType;                  //!< Message type (alphanumeric, numeric, tone_only, voice), empty for all
    qint64 m_frequencyMin = -1;         //!< Hz, -1: no lower limit
    qint64 m_frequencyMax = -1;
    qint64 m_timeFrom = -1;             //!< Milliseconds since the epoch, -1: no limit
    qint64 m_timeTo = -1;
    bool m_hideDuplicates = false;
    bool m_voiceOnly = false;           //!< Voice messages with a file

    bool matches(const MessageRecord& record) const;
    bool isEmpty() const;
    QJsonObject toJson() const;
    static MessageCenterFilter fromJson(const QJsonObject& json);
};

// A sink takes the records routed to it
class MessageCenterSink
{
public:
    virtual ~MessageCenterSink() {}
    virtual QString name() const = 0;
    //!< False and an error if the record could not be taken
    virtual bool accept(const MessageRecord& record, QString& error) = 0;
};

// A source gives records when polled
class MessageCenterSource
{
public:
    virtual ~MessageCenterSource() {}
    virtual QString name() const = 0;
    //!< The records new since the last call. False and an error if it can not be read
    virtual bool poll(std::vector<MessageRecord>& records, QString& error) = 0;
};

// Records appended to a file, one JSON object per line (lossless) or CSV (the columns of MessageRecord)
class MessageCenterFileSink : public MessageCenterSink
{
public:
    enum Format { JsonLines, Csv };

    MessageCenterFileSink(const QString& name, const QString& path, Format format);
    QString name() const override { return m_name; }
    bool accept(const MessageRecord& record, QString& error) override;
    const QString& path() const { return m_path; }
    Format format() const { return m_format; }

private:
    QString m_name;
    QString m_path;
    Format m_format;
};

// Records read from a JSON lines file; the lines added to it later are read by the next poll
class MessageCenterFileSource : public MessageCenterSource
{
public:
    MessageCenterFileSource(const QString& name, const QString& path);
    QString name() const override { return m_name; }
    bool poll(std::vector<MessageRecord>& records, QString& error) override;
    int skipped() const { return m_skipped; }       //!< Lines that were not records

private:
    QString m_name;
    QString m_path;
    qint64 m_offset = 0;
    int m_skipped = 0;
};

// A channel registered in the message center
struct MessageCenterEndpoint
{
    enum Role { Receiver, Transmitter };
    enum Status { Offline, Idle, Busy };

    QString m_alias;                    //!< The channel alias: the id of the transmitter in a page
    Role m_role = Receiver;
    Status m_status = Offline;
    QStringList m_protocols;            //!< Lower case protocol names it handles
    QList<int> m_speeds;                //!< Bits per second, empty: any
    QList<int> m_polarities;            //!< MessageRecord::Polarity values it handles, empty: both
    int m_queueDepth = 0;               //!< Pages in its own queue
    int m_capacity = 0;                 //!< Pages its queue can take, 0: it does not say
    qint64 m_frequency = -1;
    qint64 m_lastSeen = 0;              //!< Milliseconds since the epoch
    QVariantMap m_capabilities;         //!< The rest as announced

    //!< The endpoint of an event record (protocolData "event" = register, status or unregister), false if it is not one
    static bool fromRecord(const MessageRecord& record, MessageCenterEndpoint& endpoint, QString& event);
    //!< The event record for an endpoint (for the channels to publish)
    static MessageRecord toRecord(const MessageCenterEndpoint& endpoint, const QString& event);
    bool canTransmit(const MessageRecord& page) const;
    bool ready() const;                 //!< Idle or busy with room in its queue
};

// A page to transmit
struct MessageCenterPage
{
    enum State { Pending, Queued, Claimed, Transmitted, Sent, Dead };

    MessageRecord m_record;             //!< The message; its id is the id of the page; protocolData "transmitter" is the alias
    State m_state = Pending;
    int m_attempts = 0;                 //!< Claims so far
    int m_retransmits = 0;              //!< Times it went back to the queue after a failure
    bool m_dispatched = false;          //!< Sent to the transmitter, not claimed yet
    qint64 m_dispatchedAt = 0;
    QString m_claimedBy;
    QString m_reason;                   //!< Why it is dead (rejected, aborted, underrun, interrupted, expired, attempts)
    qint64 m_changed = 0;

    QString transmitter() const { return m_record.m_protocolData.value("transmitter").toString(); }
    static QString stateName(State state);
};

class MessageCenterEngine
{
public:
    static const int m_maxAttempts = 3;         //!< First try and two retransmits
    static const int m_defaultLogSize = 10000;

    MessageCenterEngine();

    // Sinks and sources by name. The log and the transmit queue are always there
    void addSink(std::unique_ptr<MessageCenterSink> sink);
    bool removeSink(const QString& name);
    QStringList sinkNames() const;
    void addSource(std::unique_ptr<MessageCenterSource> source);
    bool removeSource(const QString& name);
    QStringList sourceNames() const;
    //!< Poll the sources and ingest what they give; returns the records taken, errors are collected
    int pollSources(QStringList& errors);

    // Routes: the first matching routes in order; a route sends the record to its sinks. stop: no
    // route after it. Without a route that matches, a record goes to the log
    struct Route
    {
        QString m_name;
        MessageCenterFilter m_filter;
        QStringList m_sinks;            //!< Names: "log", "queue", or a sink added
        bool m_stop = false;
        bool m_enabled = true;
    };
    void setRoutes(const std::vector<Route>& routes) { m_routes = routes; }
    const std::vector<Route>& routes() const { return m_routes; }

    //!< A record from a source: events update the endpoints and the pages, the others are routed
    void ingest(const MessageRecord& record, const QString& source, qint64 now);

    // The log
    void setLogSize(int size);
    const std::deque<MessageRecord>& log() const { return m_log; }
    //!< The log records a filter selects, the newest last
    std::vector<MessageRecord> select(const MessageCenterFilter& filter) const;
    const MessageRecord *find(const QString& id) const;
    void clearLog() { m_log.clear(); }

    // Endpoints
    const QMap<QString, MessageCenterEndpoint>& endpoints() const { return m_endpoints; }
    //!< The transmitters that could send a page
    QStringList eligible(const MessageRecord& page) const;

    // Pages
    //!< A page: record (direction TX) with protocolData "transmitter" (the alias of the transmitter). Returns its id
    QString addPage(MessageRecord record, qint64 now);
    const MessageCenterPage *page(const QString& id) const;
    std::vector<MessageCenterPage> pages(int state = -1) const;     //!< state: MessageCenterPage::State, -1 for all
    //!< Pending pages with a transmitter now go to queued; the queued pages whose transmitter is ready are
    //!< marked dispatched and returned, for the feature to give them to the transmitter
    std::vector<MessageCenterPage> dispatch(qint64 now);
    //!< The state of a page as the transmitter reports it. state: claimed, transmitted, sent, rejected, aborted,
    //!< underrun, interrupted, released
    bool applyEvent(const QString& id, const QString& state, const QString& reason, qint64 now);
    //!< Dispatched pages not claimed for this long go back to the queue (a claim counts as an attempt)
    void expireClaims(qint64 now, qint64 timeoutMs);
    //!< Dead page back to pending with its attempts reset
    bool requeue(const QString& id, qint64 now);
    bool cancel(const QString& id);
    int removeFinished();                       //!< Sent pages are forgotten; returns how many

    // Counters
    int logged() const { return m_logged; }
    int routed() const { return m_routed; }

private:
    std::deque<MessageRecord> m_log;
    int m_logSize;
    int m_logged = 0;
    int m_routed = 0;
    std::vector<Route> m_routes;
    std::vector<std::unique_ptr<MessageCenterSink>> m_sinks;
    std::vector<std::unique_ptr<MessageCenterSource>> m_sources;
    QMap<QString, MessageCenterEndpoint> m_endpoints;
    std::vector<MessageCenterPage> m_pages;
    QStringList m_sinkErrors;

    void store(const MessageRecord& record);
    void deliver(const MessageRecord& record, const QString& sinkName, qint64 now);
    MessageCenterPage *findPage(const QString& id);
    void endpointGone(const QString& alias, qint64 now);
};

#endif // INCLUDE_MESSAGECENTERENGINE_H
