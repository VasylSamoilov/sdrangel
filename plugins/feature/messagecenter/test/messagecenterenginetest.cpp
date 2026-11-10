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

// Self test of the message center engine, not built by default:
//   cmake --build <build dir> --target messagecenterenginetest

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

#include "messagecenterengine.h"

namespace {

int passed = 0;
int failed = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    (ok ? passed : failed)++;
}

MessageRecord rx(const QString& id, const QString& protocol, const QString& address, const QString& text, const QString& alias,
    int speed = 1200, MessageRecord::Polarity polarity = MessageRecord::PolarityNormal)
{
    MessageRecord r;
    r.m_id = id;
    r.m_timestamp = 1000;
    r.m_direction = MessageRecord::DirectionRX;
    r.m_kind = MessageRecord::KindMessage;
    r.m_protocol = protocol;
    r.m_address = address;
    r.m_text = text;
    r.m_msgType = "alphanumeric";
    r.m_channelAlias = alias;
    r.m_speed = speed;
    r.m_polarity = polarity;
    r.m_frequency = 466000000;
    return r;
}

MessageEndpoint transmitter(const QString& alias, const QStringList& protocols)
{
    MessageEndpoint e;
    e.m_alias = alias;
    e.m_role = MessageEndpoint::Transmitter;
    e.m_status = MessageEndpoint::Idle;
    e.m_protocols = protocols;
    return e;
}

MessageRecord pageRecord(const QString& transmitter, const QString& protocol, const QString& address, const QString& text)
{
    MessageRecord r = rx(QString(), protocol, address, text, QString());
    r.m_direction = MessageRecord::DirectionTX;
    r.m_protocolData.insert("transmitter", transmitter);
    return r;
}

MessageRecord event(const QString& id, const QString& state, const QString& reason = QString())
{
    MessageRecord r;
    r.m_id = id;
    r.m_kind = MessageRecord::KindEvent;
    r.m_protocolData.insert("event", "page");
    r.m_protocolData.insert("state", state);
    r.m_protocolData.insert("reason", reason);
    return r;
}

void testFilter()
{
    MessageRecord a = rx("1", "pocsag", "0001234A", "HELLO WORLD", "dsA");
    MessageRecord b = rx("2", "flex", "1234567", "other", "dsB", 3200, MessageRecord::PolarityInverted);
    b.m_duplicate = MessageRecord::DuplicateDup;
    MessageCenterFilter f;

    check(f.matches(a) && f.matches(b) && f.isEmpty(), "Filter: empty selects everything");
    f.m_protocols = QStringList({"pocsag"});
    check(f.matches(a) && !f.matches(b), "Filter: protocol");
    f = MessageCenterFilter();
    f.m_address = "0001*";
    check(f.matches(a) && !f.matches(b), "Filter: address with a wildcard");
    f = MessageCenterFilter();
    f.m_text = "hello";
    check(f.matches(a) && !f.matches(b), "Filter: text, not case sensitive");
    f = MessageCenterFilter();
    f.m_polarity = (int) MessageRecord::PolarityInverted;
    f.m_speeds = {3200, 6400};
    f.m_aliases = QStringList({"dsB"});
    check(!f.matches(a) && f.matches(b), "Filter: polarity, speed and channel together");
    f = MessageCenterFilter();
    f.m_hideDuplicates = true;
    check(f.matches(a) && !f.matches(b), "Filter: hide duplicates");
    f = MessageCenterFilter();
    f.m_frequencyMin = 465000000;
    f.m_frequencyMax = 467000000;
    f.m_direction = (int) MessageRecord::DirectionRX;
    check(f.matches(a) && f.matches(b), "Filter: frequency range and direction");
    f.m_frequencyMin = 470000000;
    check(!f.matches(a), "Filter: frequency below the range");

    MessageCenterFilter g;
    g.m_protocols = QStringList({"gsc", "flex"});
    g.m_text = "x y";
    g.m_voiceOnly = true;
    g.m_timeFrom = 5;
    MessageCenterFilter h = MessageCenterFilter::fromJson(g.toJson());
    check((h.m_protocols == g.m_protocols) && (h.m_text == g.m_text) && h.m_voiceOnly && (h.m_timeFrom == 5) && (h.m_timeTo == -1),
        "Filter: JSON round trip");
}

void testLogAndRoutes()
{
    MessageCenterEngine engine;
    QTemporaryDir directory;
    QString jsonPath = directory.path() + "/out.jsonl";
    QString csvPath = directory.path() + "/out.csv";

    engine.addSink(std::unique_ptr<MessageCenterSink>(new MessageCenterFileSink("json", jsonPath, MessageCenterFileSink::JsonLines)));
    engine.addSink(std::unique_ptr<MessageCenterSink>(new MessageCenterFileSink("csv", csvPath, MessageCenterFileSink::Csv)));

    // No route: the log. Routes: flex to the csv file only (stop), everything else to the log and the json file
    MessageCenterEngine::Route flex;
    flex.m_name = "flex to csv";
    flex.m_filter.m_protocols = QStringList({"flex"});
    flex.m_sinks = QStringList({"csv"});
    flex.m_stop = true;
    MessageCenterEngine::Route all;
    all.m_name = "the rest";
    all.m_sinks = QStringList({"log", "json"});

    engine.ingest(rx("1", "pocsag", "1", "no route", "A"), "channels", 1);
    check((engine.log().size() == 1) && !QFile::exists(jsonPath), "Engine: without a route a record is logged");

    engine.setRoutes({flex, all});
    engine.ingest(rx("2", "flex", "2", "to csv", "A", 3200), "channels", 2);
    engine.ingest(rx("3", "gsc", "3", "to log and json", "A", 600), "channels", 3);
    QFile csv(csvPath);
    QFile json(jsonPath);
    csv.open(QIODevice::ReadOnly);
    json.open(QIODevice::ReadOnly);
    QList<QByteArray> csvLines = csv.readAll().split('\n');
    QList<QByteArray> jsonLines = json.readAll().split('\n');
    check((engine.log().size() == 2) && !engine.find("2") && engine.find("3") && (csvLines.size() == 3) && (jsonLines.size() == 2),
        "Engine: routes send a record to their sinks, a route can stop the others");

    // A voice message is updated by a record of the same id; a source file gives the lines written to it
    MessageRecord voice = rx("v1", "gsc", "100123", "", "A", 600);
    voice.m_msgType = "voice";
    voice.m_partial = true;
    engine.ingest(voice, "channels", 4);
    MessageRecord update = rx("v1", "gsc", "100123", "", "A", 600);
    update.m_msgType = "voice";
    update.m_protocolData.insert("voice_file", "/x/a_rx_1.wav");
    update.m_protocolData.insert("voice_seconds", 4.0);
    update.m_timestamp = 5000;
    engine.ingest(update, "channels", 5);
    const MessageRecord *merged = engine.find("v1");
    MessageCenterFilter voiceOnly;
    voiceOnly.m_voiceOnly = true;
    check(merged && !merged->m_partial && (merged->m_timestamp == 1000) && (merged->m_protocolData.value("voice_file").toString() == "/x/a_rx_1.wav")
        && (engine.select(voiceOnly).size() == 1), "Engine: an update completes the record of the same id");

    MessageCenterEngine other;
    other.addSource(std::unique_ptr<MessageCenterSource>(new MessageCenterFileSource("import", jsonPath)));
    QStringList errors;
    int taken = other.pollSources(errors);
    check((taken >= 1) && errors.isEmpty() && other.find("3"), "Engine: a source file is read and its records ingested");

    QFile append(jsonPath);
    append.open(QIODevice::Append);
    append.write(rx("9", "pocsag", "9", "later", "A").toJsonLine());
    append.write("not json\n");
    append.write("{\"half\":");        // Not complete: not read yet
    append.close();
    QStringList errors2;
    int more = other.pollSources(errors2);
    check((more == 1) && other.find("9"), "Engine: a source reads only the lines added, a line being written waits");

    other.setLogSize(2);
    other.ingest(rx("a", "pocsag", "1", "a", "A"), "x", 1);
    other.ingest(rx("b", "pocsag", "1", "b", "A"), "x", 1);
    other.ingest(rx("c", "pocsag", "1", "c", "A"), "x", 1);
    check((other.log().size() == 2) && other.find("c") && !other.find("a"), "Engine: the log is bounded");
}

void testEndpoints()
{
    MessageCenterEngine engine;
    MessageEndpoint tx = transmitter("tx1", {"pocsag", "gsc"});
    tx.m_speeds = {1200};
    tx.m_queueDepth = 1;
    tx.m_capacity = 4;
    tx.m_capabilities.insert("maxText", 80);
    MessageRecord reg = MessageEndpoint::toRecord(tx, "register");
    engine.ingest(reg, "channels", 1);

    MessageEndpoint e;
    QString event;
    check(MessageEndpoint::fromRecord(reg, e, event) && (event == "register") && (e.m_alias == "tx1") && (e.m_speeds == QList<int>({1200}))
        && (e.m_capabilities.value("maxText").toInt() == 80), "Endpoint: the registration record round trip");
    check(engine.endpoints().contains("tx:tx1") && engine.endpoints()["tx:tx1"].ready(), "Endpoint: registered, ready with room in its queue");

    tx.m_status = MessageEndpoint::Busy;
    tx.m_queueDepth = 4;
    engine.ingest(MessageEndpoint::toRecord(tx, "status"), "channels", 2);
    check((engine.endpoints()["tx:tx1"].m_status == MessageEndpoint::Busy) && !engine.endpoints()["tx:tx1"].ready()
        && (engine.endpoints()["tx:tx1"].m_speeds == QList<int>({1200})), "Endpoint: a status record changes the status and the queue, not the capabilities");

    MessageRecord flexPage = pageRecord("tx1", "flex", "5", "x");
    MessageRecord slow = pageRecord("tx1", "pocsag", "5", "x");
    slow.m_speed = 512;
    MessageRecord good = pageRecord("tx1", "pocsag", "5", "x");
    good.m_speed = 1200;
    check(!engine.endpoints()["tx:tx1"].canTransmit(flexPage) && !engine.endpoints()["tx:tx1"].canTransmit(slow) && engine.endpoints()["tx:tx1"].canTransmit(good)
        && (engine.eligible(good) == QStringList({"tx1"})), "Endpoint: protocol and speed decide what it can transmit");

    MessageRecord gone = MessageEndpoint::toRecord(tx, "unregister");
    engine.ingest(gone, "channels", 3);
    check(engine.eligible(good).isEmpty(), "Endpoint: unregistered, nothing can be sent to it");
}

void testPages()
{
    MessageCenterEngine engine;
    MessageRecord p = pageRecord("tx1", "pocsag", "1234", "HELLO");
    p.m_speed = 1200;
    QString id = engine.addPage(p, 100);

    check(engine.page(id) && (engine.page(id)->m_state == MessageCenterPage::Pending) && engine.dispatch(101).empty(),
        "Page: pending while no transmitter can take it");
    check(engine.find(id) && (engine.find(id)->m_direction == MessageRecord::DirectionTX), "Page: also in the log");

    MessageEndpoint tx = transmitter("tx1", {"pocsag"});
    tx.m_capacity = 2;
    engine.ingest(MessageEndpoint::toRecord(tx, "register"), "channels", 110);
    std::vector<MessageCenterPage> out = engine.dispatch(120);
    check((out.size() == 1) && (engine.page(id)->m_state == MessageCenterPage::Queued) && engine.page(id)->m_dispatched,
        "Page: queued once a transmitter exists, dispatched when it is ready");
    check(engine.dispatch(121).empty(), "Page: not dispatched twice");

    engine.ingest(event(id, "claimed"), "channels", 130);
    check((engine.page(id)->m_state == MessageCenterPage::Claimed) && (engine.page(id)->m_attempts == 1) && !engine.page(id)->m_dispatched
        && (engine.page(id)->m_claimedBy == "tx1"), "Page: claimed by the transmitter, one attempt");
    engine.ingest(event(id, "transmitted"), "channels", 140);
    check(engine.page(id)->m_state == MessageCenterPage::Transmitted, "Page: transmitted");
    engine.ingest(event(id, "sent"), "channels", 150);
    check(engine.page(id)->m_state == MessageCenterPage::Sent && (engine.removeFinished() == 1) && !engine.page(id), "Page: sent, then forgotten");

    // A page rejected goes to the dead letter queue and can be requeued
    QString rejected = engine.addPage(pageRecord("tx1", "pocsag", "1", "x"), 200);
    engine.dispatch(201);
    engine.ingest(event(rejected, "rejected", "message too long"), "channels", 202);
    check((engine.page(rejected)->m_state == MessageCenterPage::Dead) && engine.page(rejected)->m_reason.startsWith("rejected"),
        "Page: rejected by the transmitter, dead with the reason");
    check(engine.requeue(rejected, 210) && (engine.page(rejected)->m_state == MessageCenterPage::Pending) && (engine.page(rejected)->m_attempts == 0),
        "Page: requeued from the dead letter queue");

    // An aborted transmission is retransmitted twice, then the page is dead
    QString retry = engine.addPage(pageRecord("tx1", "pocsag", "2", "retry"), 300);
    bool ok = true;

    for (int attempt = 1; attempt <= MessageCenterEngine::m_maxAttempts; attempt++)
    {
        // Pending pages of the earlier tests may be dispatched first: only this page is followed
        engine.dispatch(300 + attempt * 10);
        ok = ok && engine.page(retry)->m_dispatched;
        engine.ingest(event(retry, "claimed"), "channels", 301 + attempt * 10);
        engine.ingest(event(retry, "transmitted"), "channels", 302 + attempt * 10);
        engine.ingest(event(retry, "aborted"), "channels", 303 + attempt * 10);
    }

    check(ok && (engine.page(retry)->m_state == MessageCenterPage::Dead) && engine.page(retry)->m_reason.startsWith("attempts")
        && (engine.page(retry)->m_attempts == MessageCenterEngine::m_maxAttempts) && (engine.page(retry)->m_retransmits == 2),
        "Page: retransmitted after a failure, dead when the attempts are used");

    // Claim not answered: back to the queue, an attempt, dead at the limit
    QString slow = engine.addPage(pageRecord("tx1", "pocsag", "3", "slow"), 500);
    engine.dispatch(500);
    engine.expireClaims(500 + 5000, 10000);
    bool notYet = engine.page(slow)->m_dispatched;
    engine.expireClaims(500 + 10000, 10000);
    check(notYet && !engine.page(slow)->m_dispatched && (engine.page(slow)->m_attempts == 1) && (engine.page(slow)->m_state == MessageCenterPage::Queued),
        "Page: a claim that does not come in time sends the page back to the queue");

    // The transmitter goes away: claimed pages wait for it, a page being transmitted is interrupted
    QString claimed = engine.addPage(pageRecord("tx2", "pocsag", "4", "claimed"), 600);
    QString onAir = engine.addPage(pageRecord("tx2", "pocsag", "5", "on air"), 600);
    MessageEndpoint tx2 = transmitter("tx2", {"pocsag"});
    engine.ingest(MessageEndpoint::toRecord(tx2, "register"), "channels", 600);
    engine.dispatch(601);
    engine.ingest(event(claimed, "claimed"), "channels", 602);
    engine.ingest(event(onAir, "claimed"), "channels", 602);
    engine.ingest(event(onAir, "transmitted"), "channels", 603);
    engine.ingest(MessageEndpoint::toRecord(tx2, "unregister"), "channels", 610);
    check((engine.page(claimed)->m_state == MessageCenterPage::Pending) && (engine.page(onAir)->m_state == MessageCenterPage::Queued)
        && (engine.page(onAir)->m_retransmits == 1), "Page: a transmitter that goes away releases its claims and interrupts what it was sending");

    // Capacity: the pages are dispatched as far as the transmitter has room
    MessageCenterEngine capacity;
    MessageEndpoint small = transmitter("small", {"pocsag"});
    small.m_capacity = 2;
    capacity.ingest(MessageEndpoint::toRecord(small, "register"), "x", 1);

    for (int i = 0; i < 5; i++) {
        capacity.addPage(pageRecord("small", "pocsag", QString::number(i), "x"), 1);
    }

    check((capacity.dispatch(2).size() == 2) && capacity.dispatch(3).empty() && capacity.pages(MessageCenterPage::Queued).size() == 5,
        "Page: no more pages are dispatched than the transmitter has room for");

    // A page for a transmitter that is not there is withdrawn only while nobody has it
    QString lonely = capacity.addPage(pageRecord("nobody", "pocsag", "1", "x"), 5);
    check(capacity.cancel(lonely) && !capacity.page(lonely), "Page: a page nobody claimed can be withdrawn");
}

// Reports that come late or twice, and endpoints that share an alias
void testGuards()
{
    MessageCenterEngine engine;
    MessageEndpoint tx = transmitter("146500000", {"pocsag"});
    MessageEndpoint receiver = tx;
    receiver.m_role = MessageEndpoint::Receiver;       // The alias of a receiver and a transmitter is the frequency: the same
    MessageRecord registration = MessageEndpoint::toRecord(tx, "register");
    registration.m_timestamp = 1;                       // The clock of the test
    engine.ingest(registration, "channels", 1);
    engine.ingest(MessageEndpoint::toRecord(receiver, "register"), "channels", 2);
    check((engine.endpoints().size() == 2) && engine.endpoints()["tx:146500000"].canTransmit(pageRecord("146500000", "pocsag", "1", "x")),
        "Guards: a receiver and a transmitter with the same alias are two endpoints");
    engine.ingest(MessageEndpoint::toRecord(receiver, "unregister"), "channels", 3);
    check(engine.endpoints()["tx:146500000"].m_status == MessageEndpoint::Idle, "Guards: the receiver going away leaves the transmitter");

    QString id = engine.addPage(pageRecord("146500000", "pocsag", "1", "x"), 10);
    check(engine.addPage(pageRecord("", "pocsag", "1", "x"), 10).isEmpty(), "Guards: a page needs a transmitter");
    MessageRecord same = pageRecord("146500000", "pocsag", "1", "x");
    same.m_id = id;
    check(engine.addPage(same, 10).isEmpty(), "Guards: a page id is used once");

    engine.dispatch(11);
    engine.ingest(event(id, "claimed"), "channels", 12);
    check(!engine.applyEvent(id, "released", QString(), 13) == false && (engine.page(id)->m_state == MessageCenterPage::Pending),
        "Guards: a claimed page can be released");
    engine.dispatch(14);
    engine.ingest(event(id, "claimed"), "channels", 15);
    engine.ingest(event(id, "transmitted"), "channels", 16);
    check(!engine.applyEvent(id, "released", QString(), 17) && (engine.page(id)->m_state == MessageCenterPage::Transmitted),
        "Guards: a page being transmitted is not released");
    engine.ingest(event(id, "sent"), "channels", 18);
    check(!engine.applyEvent(id, "aborted", QString(), 19) && !engine.applyEvent(id, "claimed", QString(), 19)
        && !engine.applyEvent(id, "transmitted", QString(), 19) && (engine.page(id)->m_state == MessageCenterPage::Sent),
        "Guards: late reports do not move a page that was sent");

    QString dead = engine.addPage(pageRecord("146500000", "pocsag", "2", "x"), 20);
    engine.dispatch(21);
    engine.ingest(event(dead, "rejected", "no"), "channels", 22);
    check(!engine.applyEvent(dead, "aborted", QString(), 23) && !engine.applyEvent(dead, "sent", QString(), 23)
        && (engine.page(dead)->m_state == MessageCenterPage::Dead), "Guards: a dead page stays dead for late reports");
    engine.requeue(dead, 24);
    check(!engine.applyEvent(dead, "aborted", QString(), 25) && (engine.page(dead)->m_state == MessageCenterPage::Pending),
        "Guards: a requeued page ignores the report of its earlier life");

    QString skipped = engine.addPage(pageRecord("146500000", "pocsag", "3", "x"), 30);
    engine.dispatch(31);
    check(!engine.applyEvent(skipped, "transmitted", QString(), 32) && (engine.page(skipped)->m_state == MessageCenterPage::Queued),
        "Guards: transmitted needs the claim first");

    // A transmitter that stops announcing is offline
    engine.ingest(event(skipped, "claimed"), "channels", 33);
    engine.sweep(33 + 20000, 15000);
    check((engine.endpoints()["tx:146500000"].m_status == MessageEndpoint::Offline) && (engine.page(skipped)->m_state == MessageCenterPage::Pending),
        "Guards: a transmitter silent for too long is offline, its claims are released");

    // Events from a file that is read do not move live pages or endpoints
    MessageCenterEngine replay;
    QTemporaryDir directory;
    QString path = directory.path() + "/replay.jsonl";
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(MessageEndpoint::toRecord(tx, "register").toJsonLine());
    file.write(rx("r1", "pocsag", "1", "a message", "A").toJsonLine());
    file.close();
    replay.addSource(std::unique_ptr<MessageCenterSource>(new MessageCenterFileSource("replay", path)));
    QStringList errors;
    replay.pollSources(errors);
    check(replay.endpoints().isEmpty() && replay.find("r1"), "Guards: the announcements in a file that is read are ignored, its messages are logged");

    // An update record that leaves fields out does not erase them
    MessageCenterEngine log;
    MessageRecord full = rx("m1", "gsc", "100123", "", "A", 600);
    full.m_msgType = "voice";
    full.m_partial = true;
    log.ingest(full, "channels", 1);
    MessageRecord bare;
    bare.m_id = "m1";
    bare.m_msgType = "voice";
    bare.m_protocolData.insert("voice_end", "turn_off");
    log.ingest(bare, "channels", 2);
    const MessageRecord *kept = log.find("m1");
    check(kept && (kept->m_protocol == "gsc") && (kept->m_address == "100123") && (kept->m_frequency == 466000000) && (kept->m_speed == 600)
        && (kept->m_channelAlias == "A") && !kept->m_partial, "Guards: an update with fields left out keeps the earlier record's fields");

    // Endpoint records
    MessageEndpoint e;
    QString what;
    MessageRecord notEvent = rx("x", "pocsag", "1", "a", "A");
    MessageEndpoint busy = transmitter("b", {});
    busy.m_status = MessageEndpoint::Busy;
    check(!MessageEndpoint::fromRecord(notEvent, e, what) && !busy.ready() && busy.canTransmit(pageRecord("b", "flex", "1", "x")),
        "Endpoint: a busy one that gives no capacity is not ready, no protocol listed means any");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testFilter();
    testLogAndRoutes();
    testEndpoints();
    testPages();
    testGuards();
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
