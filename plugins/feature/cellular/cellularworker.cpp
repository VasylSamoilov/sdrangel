///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov                                             //
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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include <QDebug>

#include "channel/channelapi.h"
#include "maincore.h"
#include "pipes/messagepipes.h"
#include "pipes/objectpipe.h"

#include "carrierradio.h"
#include "echoendpoint.h"
#include "musicendpoint.h"
#include "tonegenerator.h"

#include "cellular.h"
#include "cellularworker.h"

MESSAGE_CLASS_DEFINITION(CellularWorker::MsgAddNetwork, Message)
MESSAGE_CLASS_DEFINITION(CellularWorker::MsgSmsCentres, Message)

using modemnmt::ChannelType;
using modemnmt::NmtChannel;

namespace
{

const int kBlocksPerReport = 100;      // 1 s of 10 ms blocks
const int kBlocksPerLog = 1000;        // a line in the event log every 10 s
// carriers of two networks closer than this on one stream are on the same channel
const double kMinSeparation = 12500.0;

QString typeName(ChannelType type)
{
    switch (type)
    {
    case ChannelType::Cc: return "CC";
    case ChannelType::Cca: return "CC A";
    case ChannelType::Ccb: return "CC B";
    case ChannelType::Tc: return "TC";
    case ChannelType::AcTc: return "AC/TC";
    case ChannelType::CcTc: return "CC/TC";
    case ChannelType::Test: return "test";
    }
    return "?";
}

QString stateName(NmtChannel::State state)
{
    switch (state)
    {
    case NmtChannel::State::Idle: return "idle";
    case NmtChannel::State::RoamingIdent: return "roaming: identity";
    case NmtChannel::State::RoamingConfirm: return "roaming: confirm";
    case NmtChannel::State::MoIdent: return "call: identity";
    case NmtChannel::State::MoConfirm: return "call: proceed to send";
    case NmtChannel::State::MoDialling: return "call: dialling";
    case NmtChannel::State::MoComplete: return "call: address complete";
    case NmtChannel::State::Active: return "call: connected";
    case NmtChannel::State::MtIdent: return "paging: identity";
    case NmtChannel::State::MtRinging: return "paging: ringing";
    case NmtChannel::State::MtComplete: return "paging: answered";
    case NmtChannel::State::MoRelease: return "release by the phone";
    case NmtChannel::State::MtRelease: return "release";
    case NmtChannel::State::ScipOrder: return "switch: order";
    case NmtChannel::State::ScipIdent: return "switch: waiting";
    case NmtChannel::State::ScipIdentReq: return "switch: identity";
    case NmtChannel::State::ScipRelease: return "switch: release";
    case NmtChannel::State::MftIn: return "MFT: in";
    case NmtChannel::State::Mft: return "MFT";
    case NmtChannel::State::MftOut: return "MFT: out";
    case NmtChannel::State::PowerChange: return "call: power change";
    case NmtChannel::State::RecallProceed: return "recall: proceed";
    case NmtChannel::State::RecallDigits: return "recall: digits";
    case NmtChannel::State::RecallEnd: return "recall: end";
    case NmtChannel::State::MtAutoanswer: return "paging: autoanswer";
    case NmtChannel::State::MwiClear: return "message waiting: release";
    case NmtChannel::State::MwiInCall: return "message waiting: in call";
    case NmtChannel::State::Sms: return "short message";
    }
    return "?";
}

QString spanText(const modemcellular::WireSpan& span)
{
    return QString("%1 MHz, %2 kS/s").arg(span.centre / 1e6, 0, 'f', 4).arg(span.sampleRate / 1e3, 0, 'f', 1);
}

bool sameSpan(const modemcellular::WireSpan& a, const modemcellular::WireSpan& b)
{
    return a.centre == b.centre && a.sampleRate == b.sampleRate;
}

QString issueText(const modemnmt::NmtPlanIssue& issue)
{
    return QString("%1: %2").arg(issue.error ? "error" : "warning").arg(QString::fromStdString(issue.text));
}

} // namespace

CellularWorker::CellularWorker(MessageQueue *reports, const QObject *producer, uint32_t lead) :
    m_reports(reports),
    m_producer(producer),
    m_lead(lead),
    m_direct(false)
{
    connect(&m_inputMessageQueue, SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
}

CellularWorker::~CellularWorker()
{
    // the bridges go before the systems they connect
    for (std::unique_ptr<Network>& n : m_networks)
    {
        n->bridge.reset();
        n->cellular.reset();
        n->station.reset();
    }
}

void CellularWorker::handleInputMessages()
{
    Message* message;

    while ((message = m_inputMessageQueue.pop()))
    {
        if (MsgAddNetwork::match(*message)) {
            addNetwork(static_cast<const MsgAddNetwork&>(*message));
        } else if (Cellular::MsgAction::match(*message)) {
            action(static_cast<const Cellular::MsgAction&>(*message));
        } else if (MsgSmsCentres::match(*message)) {
            m_smsCentres = static_cast<const MsgSmsCentres&>(*message).m_centres;
        }
        delete message;
    }
}

void CellularWorker::log(int network, const QString& text)
{
    qInfo().noquote() << QString("CellularWorker: network %1: %2").arg(network).arg(text);
    if (m_reports) {
        m_reports->push(Cellular::MsgReportLog::create(network, text));
    }
}

// The frames a channel sends on its own all the time: the indications of its type.
static bool indicationFrame(modemnmt::MessageType type)
{
    using MT = modemnmt::MessageType;
    return type == MT::Frame1a || type == MT::Frame1aA || type == MT::Frame1aB || type == MT::Frame1b
        || type == MT::Frame4 || type == MT::Frame4b || type == MT::Frame30;
}

// Every frame from the phones; the frames to the phones but the indications of the channel (pages
// and orders of a calling channel are there), a frame sent again and again once with its count.
void CellularWorker::traceFrame(Network& n, size_t index, bool toPhone, const modemnmt::Frame& frame, double quality, double level)
{
    if (!n.station || !n.station->system() || index >= n.traces.size()) {
        return;
    }

    const modemnmt::NmtChannel& channel = n.station->system()->channel(index);
    Network::Trace& trace = n.traces[index];
    const int number = channel.config().channel;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // the indications between them do not break a run of the same frame (pages of a calling channel)
    if (toPhone && indicationFrame(frame.mt)) {
        return;
    }
    if (!toPhone) {
        notePhone(n, index, frame, quality);        // every frame, also the repeats that are not shown
    }

    // each direction folds its own runs: the phone's idle frames do not break the network's run
    const std::string text = modemnmt::describeFrame(frame);
    modemnmt::NmtTraceFold& fold = toPhone ? trace.out : trace.in;
    (toPhone ? trace.outAt : trace.inAt) = now;

    if (fold.repeat(text)) {
        return;
    }
    flushTrace(n, index, toPhone);
    (toPhone ? trace.outType : trace.inType) = QString::fromLatin1(modemnmt::frameDef(frame.mt).number);
    if (toPhone)
    {
        log(n.id, QString("ch %1 > %2").arg(number).arg(QString::fromStdString(text)));
        return;
    }
    log(n.id, QString("ch %1 < %2 (quality %3, level %4 dB)")
        .arg(number)
        .arg(QString::fromStdString(text))
        .arg(quality, 0, 'f', 2)
        .arg(20.0 * std::log10(std::max(level, 1e-6)), 0, 'f', 1));
}

static const char* serviceEndName(modemnmt::NmtServiceEnd result)
{
    switch (result)
    {
    case modemnmt::NmtServiceEnd::Ok: return "delivered";
    case modemnmt::NmtServiceEnd::NoAck: return "sent, not acknowledged";
    case modemnmt::NmtServiceEnd::Undelivered: return "not delivered";
    case modemnmt::NmtServiceEnd::Unreachable: return "the phone did not answer the page";
    case modemnmt::NmtServiceEnd::NoChannel: return "no traffic channel became free";
    case modemnmt::NmtServiceEnd::Rejected: return "refused by the phone";
    }
    return "?";
}

static QString numberText(const modemnmt::NmtSubscriber& subscriber)
{
    return QString::fromStdString(modemnmt::NmtCellularBridge::numberOf(subscriber));
}

// A short message of the tester to a phone: from the number of the centre, sent now.
static modemnmt::SmsDeliver deliverMessage(const std::string& centre, const QString& text)
{
    modemnmt::SmsDeliver message;
    const QDateTime now = QDateTime::currentDateTime();

    message.originator = centre.empty() ? "0" : centre;
    message.text = text.toStdString();
    message.time.year = now.date().year() % 100;
    message.time.month = now.date().month();
    message.time.day = now.date().day();
    message.time.hour = now.time().hour();
    message.time.minute = now.time().minute();
    message.time.second = now.time().second();
    message.time.zoneQuarters = now.offsetFromUtc() / 900;
    return message;
}

// What the protocol tells about the services the tester started, and messages from the phones.
void CellularWorker::setHandlers(Network& n)
{
    const int id = n.id;
    modemnmt::NmtSystem& system = *n.station->system();

    // what a phone shows of itself: the numbers it dials, its SMS centre when it really speaks to it
    auto learnCentre = [this, &n](const modemnmt::NmtSubscriber& subscriber) {
        int y1, area;
        trafficArea(n, y1, area);
        const std::string number = numberText(subscriber).toStdString();
        // the number of this call: the dial observer noted it just before the call became a
        // short message call (the network's centre or the phone's own)
        modemnmt::NmtPhoneInfo& info = known(number);
        const std::string centre = info.dialled().empty() ? n.config.smscNumber : info.dialled().front().number;
        info.noteSmsCentre(centre, y1, area);
        if (m_reports) {
            m_reports->push(Cellular::MsgBookLearn::create(Cellular::MsgBookLearn::SmsCentre, n.config.system, number,
                centre, y1, area));
        }
    };
    // touch tones the phone sends in a call (keys pressed, or a code the phone sends by itself)
    system.setAudioTap([this, id](const modemnmt::NmtSubscriber& subscriber, const float *samples, int count) {
        ToneListener& t = m_tones[numberText(subscriber).toStdString()];
        if (!t.detector) {
            t.detector = std::make_unique<DVDTMF::Detector>(static_cast<int>(modemnmt::NmtBaseStation::kAudioRate));
        }
        std::vector<DVDTMF::Detector::Key> keys;
        t.detector->process(samples, count, keys);
        for (const DVDTMF::Detector::Key& k : keys)
        {
            t.keys += k.m_key;
            t.network = id;
            t.lastMs = QDateTime::currentMSecsSinceEpoch();
        }
    });
    // a phone's own SMS centre (learned or picked in its record) reaches the centre too
    system.setSmsCentreOf([this, &n](const modemnmt::NmtSubscriber& subscriber) {
        const auto it = m_smsCentres.find(std::make_pair(static_cast<int>(n.config.system), numberText(subscriber).toStdString()));
        return it != m_smsCentres.end() ? it->second : std::string();
    });
    system.setDialObserver([this, &n](const modemnmt::NmtSubscriber& subscriber, const std::string& dialled) {
        int y1, area;
        trafficArea(n, y1, area);
        const std::string number = numberText(subscriber).toStdString();
        known(number).noteDialled(dialled, y1, area);
        if (m_reports) {
            m_reports->push(Cellular::MsgBookLearn::create(Cellular::MsgBookLearn::Dialled, n.config.system, number, dialled, y1, area));
        }
    });
    system.setSmsHandlers(
        [this, id, learnCentre](const modemnmt::NmtSubscriber& subscriber, const modemnmt::SmsSubmit& message) -> uint8_t {
            learnCentre(subscriber);
            log(id, QString("SMS from %1 to %2: %3").arg(numberText(subscriber))
                .arg(QString::fromStdString(message.destination)).arg(QString::fromStdString(message.text)));
            return 0;
        },
        [this, id](const modemnmt::NmtSubscriber& subscriber, modemnmt::NmtServiceEnd result, uint8_t cause) {
            log(id, QString("SMS to %1: %2%3").arg(numberText(subscriber)).arg(serviceEndName(result))
                .arg(cause ? QString(" (RP cause %1)").arg(cause) : QString()));
        });
    system.setSmsRequestHandler([this, &n, learnCentre](const modemnmt::NmtSubscriber& subscriber, modemnmt::SmsRpKind kind,
            modemnmt::SmsDeliver& message) {
        const QString who = numberText(subscriber);
        learnCentre(subscriber);

        if (kind != modemnmt::SmsRpKind::ReadyToReceive)
        {
            log(n.id, QString("%1 called the SMS centre: it has memory for messages again").arg(who));
            return false;
        }
        auto it = m_held.find(who.toStdString());
        if (it == m_held.end() || it->second.empty())
        {
            log(n.id, QString("%1 called the SMS centre: it asks for its waiting messages, none wait").arg(who));
            return false;
        }
        message = it->second.front();
        it->second.pop_front();
        log(n.id, QString("%1 called the SMS centre: it asks for its waiting messages, it gets \"%2\" (%3 more waiting)")
            .arg(who).arg(QString::fromStdString(message.text)).arg(it->second.size()));
        return true;
    });
    system.setMwiHandler([this, id](const modemnmt::NmtSubscriber& subscriber, uint8_t flags, modemnmt::NmtServiceEnd result) {
        log(id, QString("message waiting indicators 0x%1 to %2: %3").arg(flags, 2, 16, QChar('0'))
            .arg(numberText(subscriber)).arg(serviceEndName(result)));
    });
    system.setPowerHandler([this, id](const modemnmt::NmtSubscriber& subscriber, int level, bool done) {
        log(id, QString("power of %1 to level %2: %3").arg(numberText(subscriber)).arg(level)
            .arg(done ? "the phone took it" : "the phone did not take it"));
    });
    n.caller->setEventHandler([this, id](const std::string& text) {
        log(id, QString::fromStdString(text));
    });
}

void CellularWorker::action(const Cellular::MsgAction& msg)
{
    Network *found = nullptr;
    for (const std::unique_ptr<Network>& network : m_networks)
    {
        if (network->id == msg.getNetwork()) {
            found = network.get();
        }
    }

    const QString what = QString("%1 %2").arg(Cellular::MsgAction::name(msg.getAction())).arg(msg.getNumber());
    if (!found || !running(*found))
    {
        log(msg.getNetwork(), what + ": the network is not on the air");
        return;
    }

    Network& n = *found;
    modemnmt::NmtSystem& system = *n.station->system();
    const std::string number = msg.getNumber().toStdString();
    const bool valid = number.size() == 7 && number.find_first_not_of("0123456789") == std::string::npos;

    if (!valid)
    {
        log(n.id, what + ": the number is the country digit and six digits, e.g. 3726136");
        return;
    }

    const char country = number[0];
    const std::string digits = number.substr(1);
    bool ok = false;
    QString refused;        // why the protocol may refuse it

    switch (msg.getAction())
    {
    case Cellular::MsgAction::Call:
    {
        modemcellular::Cause cause = modemcellular::Cause::Normal;
        std::unique_ptr<modemcellular::AudioEndpoint> endpoint;
        if (n.music) {
            endpoint = std::make_unique<modemcellular::MusicEndpoint>();
        } else {
            endpoint = std::make_unique<modemcellular::EchoEndpoint>(1.0);
        }
        const bool registered = system.isRegistered(country, digits);
        ok = n.caller->call(number, "", n.bridge->legOf(number), std::move(endpoint), &cause);
        log(n.id, ok
            ? QString("%1: paging the phone (%2), the far end is %3").arg(what).arg(registered ? "registered" : "not registered")
                .arg(n.music ? "music" : "echo")
            : QString("%1: refused (%2)").arg(what).arg(modemcellular::TestCaller::causeName(cause)));
        return;
    }
    case Cellular::MsgAction::HangUp:
        n.caller->hangUp();
        log(n.id, what + ": hung up");
        return;
    case Cellular::MsgAction::Release:
        ok = system.releaseCall(country, digits);
        refused = "the phone has no call that can be released";
        break;
    case Cellular::MsgAction::Sms:
        ok = system.sendSms(country, digits, deliverMessage(n.config.smscNumber, msg.getText()));
        refused = "the phone is busy, the calling channels have no room, or the text cannot be coded";
        break;
    case Cellular::MsgAction::Hold:
    {
        const modemnmt::SmsDeliver message = deliverMessage(n.config.smscNumber, msg.getText());
        std::vector<uint8_t> rp;

        if (!modemnmt::smsBuildDeliver(message, 0, rp))
        {
            log(n.id, what + ": refused: the text cannot be coded");
            return;
        }
        std::deque<modemnmt::SmsDeliver>& queue = m_held[number];
        queue.push_back(message);
        log(n.id, QString("%1: the centre keeps it until the phone fetches it (%2 waiting)").arg(what).arg(queue.size()));
        return;
    }
    case Cellular::MsgAction::Mwi:
    {
        // for a phone in a call: when they go out (text incall, clearing; empty: both)
        const QString when = msg.getText().trimmed().toLower();
        modemnmt::MwiWhen mwiWhen = modemnmt::MwiWhen::InCallAndClearing;

        if (when == "incall") {
            mwiWhen = modemnmt::MwiWhen::InCall;
        } else if (when == "clearing") {
            mwiWhen = modemnmt::MwiWhen::Clearing;
        } else if (!when.isEmpty())
        {
            log(n.id, what + ": refused: when is incall, clearing or empty (both)");
            return;
        }
        const int flags = msg.getValue();

        if (flags <= 0 || flags > modemnmt::MwiAll)
        {
            log(n.id, what + ": refused: " + (flags == 0 ? "the network cannot switch an indicator off"
                : "the indicators are 1 (SMS) to 31"));
            return;
        }
        if (!when.isEmpty()) {
            log(n.id, QString("%1: %2").arg(what).arg(mwiWhen == modemnmt::MwiWhen::InCall
                ? "in the conversation only, not at the clearing" : "at the clearing only (in a call)"));
        }
        // what the protocol cannot send in the phone's state now waits until it can
        auto pending = n.pendingMwi.find(number);
        if (pending == n.pendingMwi.end() && system.sendMwi(country, digits, static_cast<uint8_t>(flags), mwiWhen))
        {
            log(n.id, what + ": started");
            return;
        }
        Network::PendingMwi& p = n.pendingMwi[number];
        if (!p.since.isValid()) {
            p.since = QDateTime::currentDateTime();
        }
        p.flags |= static_cast<uint8_t>(flags);
        p.when = mwiWhen;
        log(n.id, QString("%1: postponed: the phone is busy (%2), it goes out when the phone is free")
            .arg(what).arg(mwiWhen == modemnmt::MwiWhen::InCall ? "not in a conversation now"
                : "in another procedure, or the calling channels have no room"));
        return;
    }
    case Cellular::MsgAction::Power:
        ok = system.changePower(country, digits, msg.getValue());
        refused = "the phone has no connected call, or it has that level already";
        break;
    case Cellular::MsgAction::Move:
        ok = system.moveCall(country, digits, msg.getValue());
        refused = "the phone has no connected call on one channel, a switch is going on, or there is no such free traffic channel";
        break;
    case Cellular::MsgAction::Forget:
        return;         // the book is the feature's: it does it
    }
    log(n.id, what + (ok ? ": started" : ": refused: " + refused));
}

// Indicators that waited for the phone: sent as soon as the protocol takes them.
void CellularWorker::sendPostponed(Network& n)
{
    for (auto it = n.pendingMwi.begin(); it != n.pendingMwi.end();)
    {
        const std::string& number = it->first;
        const Network::PendingMwi& p = it->second;

        if (!n.station->system()->sendMwi(number[0], number.substr(1), p.flags, p.when))
        {
            ++it;
            continue;
        }
        log(n.id, QString("mwi %1: started (postponed since %2)").arg(QString::fromStdString(number))
            .arg(p.since.toString("HH:mm:ss")));
        it = n.pendingMwi.erase(it);
    }
}

// Digits of an identity: 1..9 and 10 for the digit 0 (NMT Doc 450-1 4.3.3).
static bool identityDigits(uint64_t value, int digits)
{
    for (int i = 0; i < digits; i++)
    {
        const uint64_t digit = (value >> (4 * i)) & 0xf;
        if (digit < 1 || digit > 10) {
            return false;
        }
    }
    return true;
}

void CellularWorker::notePhone(Network& n, size_t index, const modemnmt::Frame& frame, double quality)
{
    const modemnmt::NmtChannel& channel = n.station->system()->channel(index);
    const char* digits = modemnmt::frameDef(frame.mt).digits;
    int number = 0;
    int power = 0;

    // only a frame for this channel (the channel number in it is ours) with an identity in it
    if (!std::strchr(digits, 'Z') || !std::strchr(digits, 'X')
        || !modemnmt::decodeChannel(channel.config().system, frame.channelNo, number, power)
        || (number % 1024) != (channel.config().channel % 1024)
        || !identityDigits(frame.msCountry, 1) || !identityDigits(frame.msNumber, 6)) {
        return;
    }

    const std::string id = modemnmt::valueToDigits(frame.msCountry, 1) + modemnmt::valueToDigits(frame.msNumber, 6);
    Cellular::MsgReportStatus::Phone& phone = n.phones[id];

    if (phone.number.isEmpty())
    {
        log(n.id, QString("phone %1 heard on channel %2").arg(QString::fromStdString(id)).arg(channel.config().channel));
        // the book learns the traffic area it was heard on
        int y1, area;
        trafficArea(n, y1, area);
        if (m_reports) {
            m_reports->push(Cellular::MsgBookLearn::create(Cellular::MsgBookLearn::Area, n.config.system, id, std::string(), y1, area));
        }
    }
    phone.number = QString::fromStdString(id);
    phone.channel = channel.config().channel;
    phone.lastHeard = QDateTime::currentDateTime();
    phone.level = index < n.levels.size() ? n.levels[index] : -100.0f;
    phone.quality = static_cast<float>(quality);
    phone.power = power;
    phone.lastFrame = QString::fromLatin1(modemnmt::frameDef(frame.mt).number);
    // its password, in the seizure frames (a value is taken when two frames agree)
    if (std::strchr(digits, 'K'))
    {
        modemnmt::NmtPhoneInfo& info = known(id);
        const std::string before = info.password();
        info.notePassword(frame.msPassword);
        if (info.password() != before && m_reports) {
            m_reports->push(Cellular::MsgBookLearn::create(Cellular::MsgBookLearn::Password, n.config.system, id, info.password()));
        }
    }
}

// The traffic area the network sends, Y1 and Y2 (Y1 may be the country's).
void CellularWorker::trafficArea(const Network& n, int& y1, int& area)
{
    y1 = 0;
    area = 0;
    if (n.station && n.station->system() && n.station->system()->channelCount() > 0)
    {
        const uint8_t ta = n.station->system()->channel(0).config().trafficArea;
        y1 = ta >> 4;
        area = ta & 0x0f;
    }
}

// A run that ended: "ch 18 > 6 again 23 times". The run before the frame just taken, or, when the
// direction was quiet for a while, the run going on.
void CellularWorker::flushTrace(Network& n, size_t index, bool toPhone, bool quiet)
{
    if (index >= n.traces.size() || !n.station || !n.station->system()) {
        return;
    }
    Network::Trace& trace = n.traces[index];
    modemnmt::NmtTraceFold& fold = toPhone ? trace.out : trace.in;
    const int repeats = quiet ? fold.end() : fold.flush();

    if (repeats > 0)
    {
        log(n.id, QString("ch %1 %2 %3 again %4 time%5").arg(n.station->system()->channel(index).config().channel)
            .arg(toPhone ? ">" : "<").arg(toPhone ? trace.outType : trace.inType).arg(repeats).arg(repeats == 1 ? "" : "s"));
    }
}

// The keys of a phone that sent none for 2 s go to the log as one group.
void CellularWorker::flushTones(bool all)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto& t : m_tones)
    {
        ToneListener& l = t.second;
        if (!l.keys.empty() && (all || now - l.lastMs > 2000))
        {
            log(l.network, QString("tones from %1: %2").arg(QString::fromStdString(t.first)).arg(QString::fromStdString(l.keys)));
            l.keys.clear();
        }
    }
}

// A direction with no frame for a second: its run is over, its count is shown.
void CellularWorker::flushQuietTraces(Network& n)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (size_t i = 0; i < n.traces.size(); i++)
    {
        Network::Trace& trace = n.traces[i];
        if (trace.out.running() && now - trace.outAt > 1000) {
            flushTrace(n, i, true, true);
        }
        if (trace.in.running() && now - trace.inAt > 1000) {
            flushTrace(n, i, false, true);
        }
    }
}

void CellularWorker::sendTo(ChannelAPI *channel, const std::vector<uint8_t>& wire)
{
    if (!channel) {
        return;
    }
    const QByteArray bytes(reinterpret_cast<const char*>(wire.data()), static_cast<int>(wire.size()));

    if (m_direct)
    {
        // a blocking call of the feature: the channels cannot go away meanwhile
        channel->getInputMessageQueue()->push(MainCore::MsgPacket::create(m_producer, bytes));
    }
    else if (m_reports)
    {
        // the feature hands it on in its thread, if the channel is still there
        m_reports->push(Cellular::MsgToRadio::create(channel, m_producer, bytes));
    }
}

void CellularWorker::radioGone(qulonglong channel)
{
    auto it = m_radios.find(reinterpret_cast<ChannelAPI*>(static_cast<quintptr>(channel)));

    if (it != m_radios.end()) {
        it->second->channel = nullptr;      // nothing goes to it any more
    }
}

bool CellularWorker::running(const Network& network) const
{
    return network.station && network.station->running() && network.bridge;
}

// ---- radios ----

CellularWorker::Radio *CellularWorker::radioFor(ChannelAPI *channel, MessageQueue *pipe, bool tx)
{
    auto it = m_radios.find(channel);

    if (it != m_radios.end()) {
        return it->second.get();
    }

    std::unique_ptr<Radio> radio = std::make_unique<Radio>();
    radio->channel = channel;
    radio->pipe = pipe;
    radio->tx = tx;
    Radio *r = radio.get();
    m_radios[channel] = std::move(radio);

    if (pipe)
    {
        // a call still queued when the radio has gone finds nothing
        connect(pipe, &MessageQueue::messageEnqueued, this, [this, channel]() {
            auto found = m_radios.find(channel);
            if (found != m_radios.end()) {
                handlePipe(found->second.get());
            }
        }, Qt::QueuedConnection);
    }
    sendTo(channel, modemcellular::encodeSpanRequest());
    return r;
}

void CellularWorker::dropUnusedRadios()
{
    for (auto it = m_radios.begin(); it != m_radios.end();)
    {
        bool used = false;

        for (const std::unique_ptr<Network>& n : m_networks) {
            used = used || n->rx == it->second.get() || n->tx == it->second.get();
        }
        if (used)
        {
            ++it;
            continue;
        }
        if (it->second->pipe) {
            disconnect(it->second->pipe, &MessageQueue::messageEnqueued, this, nullptr);
        }
        sendTo(it->second->channel, modemcellular::encodeTuning(modemcellular::WireTuning()));
        it = m_radios.erase(it);
    }
}

// the carriers of all networks on the radio
void CellularWorker::updateTuning(Radio& radio)
{
    modemcellular::WireTuning tuning;

    tuning.lead = radio.tx ? m_lead : 0;
    for (const std::unique_ptr<Network>& n : m_networks)
    {
        if (!running(*n)) {
            continue;
        }
        if (!radio.tx && n->rx == &radio)
        {
            const std::vector<modemcellular::CarrierTuning> t = n->station->rxTuning();
            tuning.carriers.insert(tuning.carriers.end(), t.begin(), t.end());
        }
        if (radio.tx && n->tx == &radio)
        {
            const std::vector<modemcellular::CarrierTuning> t = n->station->txTuning();
            tuning.carriers.insert(tuning.carriers.end(), t.begin(), t.end());
        }
    }
    sendTo(radio.channel, modemcellular::encodeTuning(tuning));
}

void CellularWorker::handlePipe(Radio *radio)
{
    Message *message;

    while ((message = radio->pipe->pop()))
    {
        if (MainCore::MsgPacket::match(*message)) {
            handlePacket(*radio, static_cast<const MainCore::MsgPacket&>(*message).getPacket());
        }
        delete message;
    }
}

void CellularWorker::handlePacket(Radio& radio, const QByteArray& bytes)
{
    const uint8_t *data = reinterpret_cast<const uint8_t*>(bytes.constData());
    const size_t size = static_cast<size_t>(bytes.size());

    switch (modemcellular::wireKind(data, size))
    {
    case modemcellular::WireKind::Span:
    {
        modemcellular::WireSpan span;

        if (!modemcellular::decodeSpan(data, size, span)) {
            return;
        }
        radio.span = span;
        radio.haveSpan = true;
        for (std::unique_ptr<Network>& n : m_networks)
        {
            if (n->rx == &radio || n->tx == &radio) {
                tryBind(*n);
            }
        }
        return;
    }
    case modemcellular::WireKind::Batch:
        if (!radio.tx && modemcellular::decodeBatch(data, size, m_rx)) {
            processBatch(radio);
        }
        return;
    case modemcellular::WireKind::TxStatus:
        if (radio.tx) {
            modemcellular::decodeTxStatus(data, size, radio.txStatus);
        }
        return;
    case modemcellular::WireKind::None:
    case modemcellular::WireKind::SpanRequest:
    case modemcellular::WireKind::Tuning:
        return;
    }
}

// ---- networks ----

void CellularWorker::addNetwork(const MsgAddNetwork& add)
{
    for (const std::unique_ptr<Network>& n : m_networks)
    {
        if (n->id == add.m_id) {
            return;
        }
    }

    std::unique_ptr<Network> network = std::make_unique<Network>();
    Network& n = *network;

    n.id = add.m_id;
    n.name = add.m_name;
    n.config = add.m_config;
    // the round trip of the radio: the transmit lead and about 100 ms of the receive path (device
    // buffers, the pipe of the Cellular Rx), in frame periods of 166 bits at 1200 bit/s
    {
        const double roundTrip = m_lead / modemnmt::NmtBaseStation::kAudioRate + 0.1;
        n.config.roundTripFrames = static_cast<int>(std::ceil(roundTrip / (166.0 / 1200.0)));
    }
    n.station = std::make_unique<modemnmt::NmtBaseStation>(n.config);
    n.cellular = std::make_unique<modemcellular::CellularSystem>(n.station->clock(), modemcellular::ToneSet::Denmark);
    n.music = add.m_fallback == CellularNetworkSettings::FallbackMusic;
    n.cellular->setFallback(n.music ? modemcellular::CallRouter::Fallback::Music : modemcellular::CallRouter::Fallback::Echo);
    const int id = n.id;
    n.cellular->setSubscriberListener([this, id](const std::string& number, bool registered) {
        log(id, QString("%1 %2").arg(QString::fromStdString(number)).arg(registered ? "registered" : "gone"));
    });

    for (const modemnmt::NmtPlanIssue& issue : n.station->issues()) {
        log(id, issueText(issue));
    }
    log(id, QString("the phones get %1 frames for their identity (T = 8 at the phone, %2 for the round trip of the radio)")
        .arg(8 + n.config.roundTripFrames).arg(n.config.roundTripFrames));
    for (const modemnmt::NmtCarrierStatus& c : n.station->carriers())
    {
        for (const modemnmt::NmtPlanIssue& issue : c.issues) {
            log(id, QString("channel %1: %2").arg(c.config.channel).arg(issueText(issue)));
        }
    }

    m_networks.push_back(std::move(network));
    n.rx = radioFor(add.m_rxChannel, add.m_rxPipe, false);
    n.tx = radioFor(add.m_txChannel, add.m_txPipe, true);
    log(id, QString("%1 started").arg(n.name));
    tryBind(n);
    if (!running(n)) {
        reportStatus(n);
    }
}

void CellularWorker::removeNetwork(int id)
{
    m_direct = true;
    remove(id, true);
    m_direct = false;
}

void CellularWorker::remove(int id, bool retryOthers)
{
    for (auto it = m_networks.begin(); it != m_networks.end(); ++it)
    {
        if ((*it)->id != id) {
            continue;
        }
        Network& n = **it;
        Radio *rx = n.rx;
        Radio *tx = n.tx;

        unbind(n);
        n.bridge.reset();
        n.cellular.reset();
        n.station.reset();
        log(id, QString("%1 stopped").arg(n.name));
        m_networks.erase(it);
        if (rx) {
            updateTuning(*rx);
        }
        if (tx) {
            updateTuning(*tx);
        }
        dropUnusedRadios();
        // a network that clashed with this one may go on the air now
        for (std::unique_ptr<Network>& other : m_networks)
        {
            if (retryOthers && !running(*other) && (other->rx == rx || other->tx == tx)) {
                tryBind(*other);
            }
        }
        return;
    }
}

void CellularWorker::removeAll()
{
    m_direct = true;
    while (!m_networks.empty()) {
        remove(m_networks.front()->id, false);
    }
    dropUnusedRadios();
    m_direct = false;
}

// takes the network off the air (the radios are told by the caller)
void CellularWorker::unbind(Network& n)
{
    // the call of the tester goes before the router and the base station it uses
    n.caller.reset();
    n.bridge.reset();
    if (n.station && n.station->running()) {
        n.station->bind({}, {});
    }
    n.boundRx = modemcellular::WireSpan();
    n.boundTx = modemcellular::WireSpan();
}

// a carrier of the network on a channel that another network on the same stream uses
QString CellularWorker::conflict(const Network& network) const
{
    for (const std::unique_ptr<Network>& other : m_networks)
    {
        if (other.get() == &network || !running(*other)) {
            continue;
        }
        const bool sameRx = other->rx == network.rx;
        const bool sameTx = other->tx == network.tx;

        if (!sameRx && !sameTx) {
            continue;
        }
        for (const modemnmt::NmtCarrierStatus& mine : network.station->carriers())
        {
            if (!mine.planned) {
                continue;
            }
            for (const modemnmt::NmtCarrierStatus& theirs : other->station->carriers())
            {
                if (!theirs.onAir) {
                    continue;
                }
                if ((sameRx && std::fabs(mine.frequency.uplink - theirs.frequency.uplink) < kMinSeparation)
                    || (sameTx && std::fabs(mine.frequency.downlink - theirs.frequency.downlink) < kMinSeparation))
                {
                    return QString("channel %1 (%2 MHz) is on the frequency of channel %3 of network %4 (%5)")
                        .arg(mine.config.channel).arg(mine.frequency.downlink / 1e6, 0, 'f', 4)
                        .arg(theirs.config.channel).arg(other->id).arg(other->name);
                }
            }
        }
    }
    return QString();
}

// carriers of other networks on the same stream closer than the 25 kHz the filters are made for
// (but not on the same channel): they hear each other a little
QStringList CellularWorker::neighbours(const Network& network) const
{
    QStringList out;

    for (const std::unique_ptr<Network>& other : m_networks)
    {
        if (other.get() == &network || !running(*other) || (other->rx != network.rx && other->tx != network.tx)) {
            continue;
        }
        for (const modemnmt::NmtCarrierStatus& mine : network.station->carriers())
        {
            for (const modemnmt::NmtCarrierStatus& theirs : other->station->carriers())
            {
                const double d = std::fabs(mine.frequency.downlink - theirs.frequency.downlink);

                if (mine.planned && theirs.onAir && d >= kMinSeparation && d < modemnmt::NmtBaseStation::kChannelSpacing)
                {
                    out.append(QString("channel %1 is %2 kHz from channel %3 of network %4: they hear each other a little")
                        .arg(mine.config.channel).arg(d / 1e3, 0, 'f', 1).arg(theirs.config.channel).arg(other->id));
                }
            }
        }
    }
    return out;
}

void CellularWorker::tryBind(Network& n)
{
    if (!n.rx || !n.tx || !n.rx->haveSpan || !n.tx->haveSpan) {
        return;
    }
    if (running(n) && sameSpan(n.rx->span, n.boundRx) && sameSpan(n.tx->span, n.boundTx)) {
        return;
    }

    const bool wasRunning = running(n);
    unbind(n);
    if (wasRunning)
    {
        updateTuning(*n.rx);
        updateTuning(*n.tx);
    }

    const modemcellular::WireSpan& rxSpan = n.rx->span;
    const modemcellular::WireSpan& txSpan = n.tx->span;

    if (rxSpan.sampleRate <= 0.0 || txSpan.sampleRate <= 0.0)
    {
        log(n.id, "waiting for both devices to run");
        reportStatus(n);
        return;
    }
    if (!modemcellular::CarrierRadio::rateSupported(rxSpan.sampleRate) || !modemcellular::CarrierRadio::rateSupported(txSpan.sampleRate))
    {
        log(n.id, QString("error: the sample rates (receiver %1, transmitter %2 kS/s) have to be even multiples of 48 kHz, e.g. 960 kS/s")
            .arg(rxSpan.sampleRate / 1e3).arg(txSpan.sampleRate / 1e3));
        reportStatus(n);
        return;
    }
    const QString clash = conflict(n);
    if (!clash.isEmpty())
    {
        log(n.id, "error: " + clash + ": the network is off");
        reportStatus(n);
        return;
    }
    for (const QString& near : neighbours(n)) {
        log(n.id, "warning: " + near);
    }

    const modemnmt::NmtStreamSpan rx = {1, rxSpan.centre, rxSpan.sampleRate};
    const modemnmt::NmtStreamSpan tx = {2, txSpan.centre, txSpan.sampleRate};
    const bool ok = n.station->bind({rx}, {tx});

    for (const modemnmt::NmtCarrierStatus& c : n.station->carriers())
    {
        for (const modemnmt::NmtPlanIssue& issue : c.streamIssues) {
            log(n.id, QString("channel %1 (%2 / %3 MHz): %4").arg(c.config.channel)
                .arg(c.frequency.downlink / 1e6, 0, 'f', 4).arg(c.frequency.uplink / 1e6, 0, 'f', 4)
                .arg(issueText(issue)));
        }
    }
    for (const modemnmt::NmtPlanIssue& issue : n.station->streamIssues()) {
        log(n.id, issueText(issue));
    }
    if (!ok)
    {
        log(n.id, QString("the network is off (receiver %1, transmitter %2)").arg(spanText(rxSpan)).arg(spanText(txSpan)));
        reportStatus(n);
        return;
    }

    n.bridge = std::make_unique<modemnmt::NmtCellularBridge>(*n.station->system(), *n.cellular);
    n.caller = std::make_unique<modemcellular::TestCaller>(*n.cellular);
    setHandlers(n);
    n.phones.clear();
    n.boundRx = rxSpan;
    n.boundTx = txSpan;
    n.levels.assign(n.station->system()->channelCount(), -100.0f);
    n.traces.assign(n.station->system()->channelCount(), Network::Trace());
    for (size_t i = 0; i < n.station->system()->channelCount(); i++)
    {
        Network *network = &n;
        n.station->system()->channel(i).setFrameTap(
            [this, network, i](bool toPhone, const modemnmt::Frame& frame, double quality, double level) {
                traceFrame(*network, i, toPhone, frame, quality, level);
            });
        // the data link of short message calls: "ch 18 < DMS DT 2 data 01 18 ...", runs folded
        n.station->system()->channel(i).setDmsTap([this, network, i](bool rx, const std::string& text) {
            Network::Trace& trace = network->traces[i];
            modemnmt::NmtTraceFold& fold = rx ? trace.in : trace.out;
            (rx ? trace.inAt : trace.outAt) = QDateTime::currentMSecsSinceEpoch();
            if (fold.repeat("DMS " + text)) {
                return;
            }
            flushTrace(*network, i, !rx);
            (rx ? trace.inType : trace.outType) = "DMS";
            log(network->id, QString("ch %1 %2 DMS %3").arg(network->station->system()->channel(i).config().channel)
                .arg(rx ? "<" : ">").arg(QString::fromStdString(text)));
        });
    }
    n.batches = 0;
    n.gaps = 0;
    updateTuning(*n.rx);
    updateTuning(*n.tx);

    const size_t carriers = n.station->system()->channelCount();
    log(n.id, QString("on the air with %1 carrier%2 (receiver %3, transmitter %4, lead %5 ms)")
        .arg(carriers).arg(carriers == 1 ? "" : "s").arg(spanText(rxSpan)).arg(spanText(txSpan))
        .arg(m_lead / modemnmt::NmtBaseStation::kAudioRate * 1e3, 0, 'f', 0));
    reportStatus(n);
}

void CellularWorker::processBatch(Radio& radio)
{
    m_txOut.clear();

    for (std::unique_ptr<Network>& network : m_networks)
    {
        Network& n = *network;

        if (n.rx != &radio || !running(n)) {
            continue;
        }
        if (n.batches > 0 && m_rx.sample != n.nextSample) {
            n.gaps += m_rx.sample > n.nextSample ? (m_rx.sample - n.nextSample) / static_cast<uint64_t>(m_rx.length > 0 ? m_rx.length : 1) : 1;
        }
        n.nextSample = m_rx.sample + static_cast<uint64_t>(m_rx.length);

        n.station->process(m_rx, n.txBatch);
        n.bridge->run();
        // the audio of the tester's call, the same time as the block at the call rate
        n.caller->process(static_cast<int>(m_rx.length * modemcellular::kAudioSampleRate / modemnmt::NmtBaseStation::kAudioRate));
        sendPostponed(n);
        flushQuietTraces(n);
        flushTones(false);

        // the audio of all networks on one transmitter goes as one batch
        modemcellular::CarrierBatch& out = m_txOut[n.tx];
        if (out.carriers.empty())
        {
            out.sample = n.txBatch.sample;
            out.length = n.txBatch.length;
            out.samples.clear();
            out.levels.clear();
        }
        out.carriers.insert(out.carriers.end(), n.txBatch.carriers.begin(), n.txBatch.carriers.end());
        out.samples.insert(out.samples.end(), n.txBatch.samples.begin(), n.txBatch.samples.end());

        for (size_t i = 0; i < n.txBatch.carriers.size() && i < n.levels.size(); i++)
        {
            const int at = m_rx.find(n.txBatch.carriers[i]);

            if (at >= 0 && static_cast<size_t>(at) < m_rx.levels.size()) {
                n.levels[i] = m_rx.levels[static_cast<size_t>(at)];
            }
        }

        if (++n.batches % kBlocksPerReport == 0) {
            reportStatus(n);
        }
        if (n.batches % kBlocksPerLog == 0)
        {
            QString line = QString("%1 s on the air, %2 blocks missed;")
                .arg(n.station->samples() / modemnmt::NmtBaseStation::kAudioRate, 0, 'f', 0).arg(n.gaps);
            const modemcellular::WireTxStatus& st = n.tx->txStatus;
            for (size_t k = 0; k < st.carriers.size(); k++)
            {
                if (st.carriers[k] / modemnmt::NmtBaseStation::kIdStride != n.id) {
                    continue;
                }
                line += QString(" ch %1: tx %2 ms, %3 empty;").arg(st.carriers[k] % modemnmt::NmtBaseStation::kIdStride)
                    .arg(st.fill[k] / modemnmt::NmtBaseStation::kAudioRate * 1e3, 0, 'f', 0)
                    .arg(st.underruns[k]);
            }
            log(n.id, line);
        }
    }

    for (const auto& out : m_txOut) {
        sendAudio(out.first->channel, modemcellular::encodeBatch(out.second));
    }
}

// the audio for a transmitter goes through the pipe it registered from the controller to it
// (drained in its own thread); before it registered (its carriers are on the way) there is
// nothing to send
void CellularWorker::sendAudio(ChannelAPI *channel, const std::vector<uint8_t>& wire)
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(m_producer, "cellulartx", pipes);

    for (ObjectPipe *pipe : pipes)
    {
        MessageQueue *queue = qobject_cast<MessageQueue*>(pipe->m_element);

        if (pipe->m_consumer == channel && queue)
        {
            queue->push(MainCore::MsgPacket::create(m_producer,
                QByteArray(reinterpret_cast<const char*>(wire.data()), static_cast<int>(wire.size()))));
            return;
        }
    }
}

void CellularWorker::reportStatus(const Network& n)
{
    if (!m_reports || !n.station) {
        return;
    }
    Cellular::MsgReportStatus *msg = Cellular::MsgReportStatus::create(n.id);
    modemnmt::NmtSystem *system = running(n) ? n.station->system() : nullptr;

    if (system)
    {
        msg->m_onAir = true;
        msg->m_registered = static_cast<int>(system->registeredCount());
        msg->m_calls = n.cellular ? static_cast<int>(n.cellular->callCount()) : 0;
        msg->m_summary = QString("On the air: %1 registered, %2 calls, %3 s, %4 blocks missed")
            .arg(system->registeredCount())
            .arg(n.cellular ? n.cellular->callCount() : 0)
            .arg(n.station->samples() / modemnmt::NmtBaseStation::kAudioRate, 0, 'f', 0)
            .arg(n.gaps);
    }
    else if (!n.rx->haveSpan || !n.tx->haveSpan) {
        msg->m_summary = QString("Waiting for the %1").arg(!n.rx->haveSpan ? "receiver" : "transmitter");
    } else {
        msg->m_summary = "Off (see the log)";
    }

    size_t onAir = 0;
    for (size_t i = 0; i < n.station->carriers().size(); i++)
    {
        const modemnmt::NmtCarrierStatus& c = n.station->carriers()[i];
        Cellular::MsgReportStatus::Carrier row;

        row.channel = c.config.channel;
        row.type = typeName(c.config.type);
        row.downlink = c.frequency.downlink;
        row.uplink = c.frequency.uplink;
        if (c.onAir && system && onAir < system->channelCount())
        {
            row.state = stateName(system->channel(onAir).state());
            if (const modemnmt::NmtTransaction *t = system->channel(onAir).currentTransaction()) {
                row.phone = numberText(t->subscriber);
            }
            row.rxLevel = onAir < n.levels.size() ? n.levels[onAir] : -100.0f;
            const int id = n.station->carrierId(i);
            const modemcellular::WireTxStatus& st = n.tx->txStatus;
            for (size_t k = 0; k < st.carriers.size(); k++)
            {
                if (st.carriers[k] == id)
                {
                    row.txFillMs = st.fill[k] / modemnmt::NmtBaseStation::kAudioRate * 1e3;
                    row.underruns = st.underruns[k];
                }
            }
            onAir++;
        }
        else
        {
            row.state = "off";
            for (const modemnmt::NmtPlanIssue& issue : c.issues) {
                if (issue.error) {
                    row.state = "off: " + QString::fromStdString(issue.text);
                }
            }
            for (const modemnmt::NmtPlanIssue& issue : c.streamIssues) {
                if (issue.error) {
                    row.state = "off: " + QString::fromStdString(issue.text);
                }
            }
        }
        msg->m_carriers.append(row);
    }

    // the phones heard, and the registered ones (also when not heard since the start)
    if (system)
    {
        std::map<std::string, Cellular::MsgReportStatus::Phone> phones = n.phones;

        system->forEachRegistered([&phones](const modemnmt::NmtSubscriber& subscriber) {
            Cellular::MsgReportStatus::Phone& phone = phones[modemnmt::NmtCellularBridge::numberOf(subscriber)];
            phone.number = numberText(subscriber);
            phone.registered = true;
        });
        for (const Cellular::MsgReportStatus::Carrier& c : msg->m_carriers)
        {
            if (!c.phone.isEmpty() && phones.count(c.phone.toStdString()) > 0) {
                phones[c.phone.toStdString()].state = QString("channel %1: %2").arg(c.channel).arg(c.state);
            }
        }
        for (auto& p : phones)
        {
            Cellular::MsgReportStatus::Phone& phone = p.second;
            const auto info = m_known.find(p.first);
            if (info != m_known.end())
            {
                phone.password = QString::fromStdString(info->second.password());
                phone.dialled = QString::fromStdString(modemnmt::NmtPhoneInfo::text(info->second.dialled(), false));
                phone.smsCentre = QString::fromStdString(modemnmt::NmtPhoneInfo::text(info->second.smsCentres(), true));
            }
            const auto held = m_held.find(p.first);
            phone.held = held != m_held.end() ? static_cast<int>(held->second.size()) : 0;
            const auto pending = n.pendingMwi.find(p.first);
            phone.pendingMwi = pending != n.pendingMwi.end() ? pending->second.flags : 0;
            msg->m_phones.append(phone);
        }
    }

    m_reports->push(msg);
}
