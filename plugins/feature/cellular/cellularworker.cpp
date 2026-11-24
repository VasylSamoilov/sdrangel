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

#include "audio/audiodeviceinfo.h"
#include "audio/audiodevicemanager.h"
#include "channel/channelapi.h"
#include "dsp/dspengine.h"
#include "maincore.h"
#include "radiopagingpage.h"
#include "util/messagerecord.h"

#include <QUuid>
#include "pipes/messagepipes.h"
#include "pipes/objectpipe.h"

#include "carrierradio.h"
#include "echoendpoint.h"
#include "musicendpoint.h"
#include "tonegenerator.h"

#include "cellular.h"
#include "cellularsmstext.h"
#include "cellularworker.h"

MESSAGE_CLASS_DEFINITION(CellularWorker::MsgAddNetwork, Message)
MESSAGE_CLASS_DEFINITION(CellularWorker::MsgSmsCentres, Message)
MESSAGE_CLASS_DEFINITION(CellularWorker::MsgNames, Message)
MESSAGE_CLASS_DEFINITION(CellularWorker::MsgConsole, Message)

using modemnmt::ChannelType;
using modemnmt::NmtChannel;

namespace
{

const int kBlocksPerReport = 100;      // 1 s of 10 ms blocks
const int kBlocksPerLog = 1000;        // a line in the event log every 10 s
const int kBlocksPerSpeech = 20;       // the call meters: 200 ms, the step of their 2 s window
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
    // the signalling states of NMT Doc 450-1 (1997) 4.4.1: scheme A call from the MS, B call to
    // the MS, C switching call in progress, D roaming updating, 4.4.1.3 clearing
    case NmtChannel::State::Idle: return "idle";
    case NmtChannel::State::RoamingIdent: return "D4 MTX requests identity";
    case NmtChannel::State::RoamingConfirm: return "D6 roaming updating confirmation";
    case NmtChannel::State::MoIdent: return "A4 MTX requests identity";
    case NmtChannel::State::MoConfirm: return "A6 MTX ready to receive digits";
    case NmtChannel::State::MoDialling: return "A7 transmission of digits";
    case NmtChannel::State::MoComplete: return "A8 address complete";
    case NmtChannel::State::Active: return "speech";
    case NmtChannel::State::MtIdent: return "B7 MTX requests identity";
    case NmtChannel::State::MtRinging: return "B10 MTX sends ringing order";
    case NmtChannel::State::MtComplete: return "B13 answer";
    case NmtChannel::State::MoRelease: return "MS clearing";
    case NmtChannel::State::MtRelease: return "MTX clearing";
    case NmtChannel::State::ScipOrder: return "C switching: channel order";
    case NmtChannel::State::ScipIdent: return "C switching: MS to the new TC";
    case NmtChannel::State::ScipIdentReq: return "C switching: MTX requests identity";
    case NmtChannel::State::ScipRelease: return "C switching: forced release";
    case NmtChannel::State::MftIn: return "MFT converter in";
    case NmtChannel::State::Mft: return "MFT converter";
    case NmtChannel::State::MftOut: return "MFT converter out";
    case NmtChannel::State::PowerChange: return "MS output power change";
    case NmtChannel::State::RecallProceed: return "register recall";
    case NmtChannel::State::RecallDigits: return "register recall: digits";
    case NmtChannel::State::RecallEnd: return "register recall: end";
    case NmtChannel::State::MtAutoanswer: return "B2 automatic answer";
    case NmtChannel::State::MwiClear: return "MTX clearing with message waiting";
    case NmtChannel::State::MwiInCall: return "message waiting indication";
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
    if (m_audioSink) {
        DSPEngine::instance()->getAudioDeviceManager()->removeAudioSink(&m_audioFifo);
    }
    if (m_consoleSink) {
        DSPEngine::instance()->getAudioDeviceManager()->removeAudioSink(&m_consoleFifo);
    }
    for (std::unique_ptr<Network>& n : m_networks) {
        n->console.reset();
    }
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
        } else if (MsgNames::match(*message)) {
            rename(static_cast<const MsgNames&>(*message));
        } else if (MsgConsole::match(*message)) {
            setConsole(static_cast<const MsgConsole&>(*message).m_console);
        }
        delete message;
    }
}

void CellularWorker::log(int network, const QString& text, Cellular::MsgReportLog::Kind kind)
{
    using K = Cellular::MsgReportLog;
    // the lines of the actions start with the name of the action; errors and warnings say so
    if (kind == K::Event)
    {
        const QString first = text.section(' ', 0, 0);
        if (first == "call" || first == "hangup" || first == "release" || first == "move") {
            kind = K::Call;
        } else if (first == "sms" || first == "hold") {
            kind = K::Sms;
        } else if (first == "mwi") {
            kind = K::Mwi;
        } else if (first == "power") {
            kind = K::Power;
        } else if (first == "error:" || first == "warning:") {
            kind = K::Warning;
        }
    }
    qInfo().noquote() << QString("CellularWorker: network %1: %2").arg(network).arg(text);
    if (m_reports) {
        m_reports->push(Cellular::MsgReportLog::create(network, text, kind));
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
        log(n.id, QString("ch %1 > %2").arg(number).arg(QString::fromStdString(text)), Cellular::MsgReportLog::Frame);
        return;
    }
    log(n.id, QString("ch %1 < %2 (quality %3, level %4 dB)")
        .arg(number)
        .arg(QString::fromStdString(text))
        .arg(quality, 0, 'f', 2)
        .arg(20.0 * std::log10(std::max(level, 1e-6)), 0, 'f', 1), Cellular::MsgReportLog::Frame);
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

// The DCS a message to a phone goes with (as smsBuildDeliver): 0x30 + set if a code is 80h or above
static uint8_t sentDcs(const modemnmt::SmsDeliver& message)
{
    for (uint8_t c : CellularSmsText::codesOf(message.text))
    {
        if (c & 0x80) {
            return static_cast<uint8_t>(0x30 | (message.set & 0x0f));
        }
    }
    return 0x00;
}

void CellularWorker::smsRecord(const Network& n, bool fromPhone, const QString& phone, const QString& other, uint8_t dcs,
    const std::vector<uint8_t>& codes)
{
    MessageRecord record;
    const RadioPagingCharset::Codepage& codepage = CellularSmsText::forDcs(dcs);

    record.m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.m_timestamp = QDateTime::currentMSecsSinceEpoch();
    record.m_direction = fromPhone ? MessageRecord::DirectionRX : MessageRecord::DirectionTX;
    record.m_protocol = "nmt";
    record.m_mode = "SMS";
    record.m_kind = MessageRecord::KindMessage;
    record.m_msgType = "alphanumeric";
    record.m_address = phone;
    record.m_text = CellularSmsText::decode(dcs, codes);
    record.m_rawData = QByteArray(reinterpret_cast<const char*>(codes.data()), static_cast<int>(codes.size()));
    record.m_rawBits = static_cast<int>(codes.size()) * ((dcs & 0xf0) == 0x30 ? 8 : 7);
    record.m_channel = QString("Cellular %1").arg(n.name);
    record.m_quality = MessageRecord::QualityClean;     // the data link delivered it whole (its check sums)
    // the codes and their codepage, so the text can be shown with another one (recode)
    record.m_protocolData.insert("alpha_codes", QString(record.m_rawData.toHex()));
    record.m_protocolData.insert("charset", RadioPagingPage::charsetData(codepage.key()));
    record.m_protocolData.insert("dcs", dcs);
    record.m_protocolData.insert(fromPhone ? "destination" : "originator", other);
    if (m_reports) {
        m_reports->push(MainCore::MsgMessageRecord::create(nullptr, record));
    }
}

// A short message of the tester to a phone: from the number of the centre, sent now, the text in the codes of
// the codepage of the network (7-bit GSM, or 8-bit with the DCS of its NMT Character Set). A character
// without a code there: bad is it (the message is not to be sent).
static modemnmt::SmsDeliver deliverMessage(const QString& codepage, const std::string& centre, const QString& text,
    QString& bad)
{
    modemnmt::SmsDeliver message;
    const QDateTime now = QDateTime::currentDateTime();
    const RadioPagingCharset::Key key = CellularSmsText::keyOf(codepage);
    const RadioPagingCharset::Codepage& cp = CellularSmsText::codepage(key);
    std::string codes;

    bad.clear();
    message.originator = centre.empty() ? "0" : centre;
    if (CellularSmsText::encode(cp, text, codes, bad))
    {
        message.text = codes;
        message.set = CellularSmsText::set(cp);
    }
    else {
        message.text = text.toStdString();
    }
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
    // listening in: both directions of the listened call
    system.setAudioTapToMobile([this, id](const modemnmt::NmtSubscriber& subscriber, const float *samples, int count) {
        if (m_listener && id == m_listenNetwork && numberText(subscriber) == m_listenNumber) {
            m_listener->toPhone(samples, count);
        }
    });
    system.setAudioTap([this, id](const modemnmt::NmtSubscriber& subscriber, const float *samples, int count) {
        if (m_listener && id == m_listenNetwork && numberText(subscriber) == m_listenNumber) {
            m_listener->fromPhone(samples, count);
        }
        ToneListener& t = m_tones[numberText(subscriber).toStdString()];
        if (!t.detector) {
            // the call audio of the tap is at the call rate (8 kHz, RxAudioChain), not the channel's
            t.detector = std::make_unique<DVDTMF::Detector>(modemcellular::kAudioSampleRate);
        }
        std::vector<DVDTMF::Detector::Key> keys;
        t.detector->process(samples, count, keys);
        for (const DVDTMF::Detector::Key& k : keys)
        {
            t.keys += k.m_key;
            t.network = id;
            t.lastMs = QDateTime::currentMSecsSinceEpoch();
            // the console shows the touch tones of the phone it talks to at once
            Network *busy = consoleBusy();
            if (busy && busy->id == id && busy->console->peer() == numberText(subscriber).toStdString())
            {
                m_consoleKeys += QChar(k.m_key);
                m_consoleState = -1;
            }
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
        // where the call goes: the number and what answers it
        const auto own = m_smsCentres.find(std::make_pair(static_cast<int>(n.config.system), number));
        QString type;
        if (dialled == n.config.smscNumber || (own != m_smsCentres.end() && own->second == dialled)) {
            type = "SMS centre";
        } else if (n.cellular && n.cellular->isRegistered(dialled)) {
            type = "phone";
        } else if (dialled.size() == 7 && dialled.find_first_not_of("0123456789") == std::string::npos) {
            type = "phone, not registered here: paged";
        } else {
            type = n.music ? "music, no phone has the number" : "echo, no phone has the number";
        }
        const QString to = QString::fromStdString(dialled);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        n.callInfo[number] = {QString("call to %1 (%2)").arg(to).arg(type.section(',', 0, 0)), now};
        if (type.startsWith("phone")) {
            n.callInfo[dialled] = {QString("call from %1 (phone)").arg(QString::fromStdString(number)), now};
        }
        log(n.id, QString("%1 calls %2: %3").arg(QString::fromStdString(number)).arg(to).arg(type), Cellular::MsgReportLog::Call);
        if (m_reports) {
            m_reports->push(Cellular::MsgBookLearn::create(Cellular::MsgBookLearn::Dialled, n.config.system, number, dialled, y1, area));
        }
    });
    system.setSmsHandlers(
        [this, id, learnCentre, &n](const modemnmt::NmtSubscriber& subscriber, const modemnmt::SmsSubmit& message) -> uint8_t {
            learnCentre(subscriber);
            smsRecord(n, true, numberText(subscriber), QString::fromStdString(message.destination), message.dcs, message.codes);
            // the text with the codepage of its DCS (GSM, or the NMT Character Set of an 8-bit message)
            log(id, QString("SMS from %1 to %2: %3").arg(numberText(subscriber))
                .arg(QString::fromStdString(message.destination)).arg(CellularSmsText::decode(message.dcs, message.codes)),
                Cellular::MsgReportLog::Sms);
            return 0;
        },
        [this, id](const modemnmt::NmtSubscriber& subscriber, modemnmt::NmtServiceEnd result, uint8_t cause) {
            log(id, QString("SMS to %1: %2%3").arg(numberText(subscriber)).arg(serviceEndName(result))
                .arg(cause ? QString(" (RP cause %1)").arg(cause) : QString()), Cellular::MsgReportLog::Sms);
        });
    system.setSmsRequestHandler([this, &n, learnCentre](const modemnmt::NmtSubscriber& subscriber, modemnmt::SmsRpKind kind,
            modemnmt::SmsDeliver& message) {
        const QString who = numberText(subscriber);
        learnCentre(subscriber);

        if (kind != modemnmt::SmsRpKind::ReadyToReceive)
        {
            log(n.id, QString("%1 called the SMS centre: it has memory for messages again").arg(who), Cellular::MsgReportLog::Sms);
            return false;
        }
        auto it = m_held.find(who.toStdString());
        if (it == m_held.end() || it->second.empty())
        {
            log(n.id, QString("%1 called the SMS centre: it asks for its waiting messages, none wait").arg(who), Cellular::MsgReportLog::Sms);
            return false;
        }
        message = it->second.front();
        it->second.pop_front();
        log(n.id, QString("%1 called the SMS centre: it asks for its waiting messages, it gets \"%2\" (%3 more waiting)")
            .arg(who).arg(CellularSmsText::decode(message.set ? static_cast<uint8_t>(0x30 | message.set) : 0x00,
                CellularSmsText::codesOf(message.text))).arg(it->second.size()), Cellular::MsgReportLog::Sms);
        return true;
    });
    system.setMwiHandler([this, id](const modemnmt::NmtSubscriber& subscriber, uint8_t flags, modemnmt::NmtServiceEnd result) {
        log(id, QString("message waiting indicators 0x%1 to %2: %3").arg(flags, 2, 16, QChar('0'))
            .arg(numberText(subscriber)).arg(serviceEndName(result)), Cellular::MsgReportLog::Mwi);
    });
    system.setPowerHandler([this, id](const modemnmt::NmtSubscriber& subscriber, int level, bool done) {
        log(id, QString("power of %1 to level %2: %3").arg(numberText(subscriber)).arg(level)
            .arg(done ? "the phone took it" : "the phone did not take it"), Cellular::MsgReportLog::Power);
    });
    n.caller->setEventHandler([this, id](const std::string& text) {
        log(id, QString::fromStdString(text), Cellular::MsgReportLog::Call);
    });
}

void CellularWorker::action(const Cellular::MsgAction& msg)
{
    if (msg.getAction() >= Cellular::MsgAction::ConsoleDial)
    {
        consoleAction(msg);
        return;
    }
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
        if (ok) {
            n.callInfo[number] = {QString("call from the tester (%1)").arg(n.music ? "music" : "echo"), QDateTime::currentMSecsSinceEpoch()};
        }
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
        // the test call of this phone: its caller hangs up; any other call: the network releases it
        if (n.caller->state() != modemcellular::TestCaller::State::Idle && n.caller->number() == msg.getNumber().toStdString())
        {
            n.caller->hangUp();
            log(n.id, what + ": the test call hung up");
            return;
        }
        ok = system.releaseCall(country, digits);
        refused = "the phone has no call that can be released";
        break;
    case Cellular::MsgAction::Sms:
    {
        QString bad;
        const modemnmt::SmsDeliver message = deliverMessage(n.smsCodepage, n.config.smscNumber, msg.getText(), bad);
        if (!bad.isEmpty())
        {
            log(n.id, what + QString(": refused: '%1' has no code in the SMS codepage of the network").arg(bad));
            return;
        }
        ok = system.sendSms(country, digits, message);
        if (ok) {
            smsRecord(n, false, msg.getNumber(), QString::fromStdString(message.originator), sentDcs(message),
                CellularSmsText::codesOf(message.text));
        }
        refused = "the phone is busy, the calling channels have no room, or the text cannot be coded";
        break;
    }
    case Cellular::MsgAction::Hold:
    {
        QString bad;
        const modemnmt::SmsDeliver message = deliverMessage(n.smsCodepage, n.config.smscNumber, msg.getText(), bad);
        std::vector<uint8_t> rp;

        if (!bad.isEmpty())
        {
            log(n.id, what + QString(": refused: '%1' has no code in the SMS codepage of the network").arg(bad));
            return;
        }
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
    case Cellular::MsgAction::Listen:
        listen(n.id, msg.getNumber(), msg.getValue());
        return;
    case Cellular::MsgAction::ConsoleDial:      // the console's: consoleAction()
    case Cellular::MsgAction::ConsoleAnswer:
    case Cellular::MsgAction::ConsoleHangUp:
    case Cellular::MsgAction::ConsoleDigit:
    case Cellular::MsgAction::ConsoleSms:
        return;
    }
    log(n.id, what + (ok ? ": started" : ": refused: " + refused));
}

void CellularWorker::setFarEnd(Network& n, const CellularFarEnd& farEnd)
{
    const int id = n.id;
    n.musicInput.reset();
    n.cellular->setCallLimit(farEnd.m_callLimitS);
    const QString limit = farEnd.m_callLimitS > 0 ? QString("; every call ends after %1 s").arg(farEnd.m_callLimitS) : QString();

    if (!n.music)
    {
        const double delay = std::max(0.2, std::min(3.0, farEnd.m_echoDelayMs / 1000.0));
        const double level = std::min(0, farEnd.m_echoLevelDb);
        const bool announce = farEnd.m_echoAnnounce;
        n.cellular->setFallbackFactory([delay, level, announce]() {
            return std::unique_ptr<modemcellular::AudioEndpoint>(new modemcellular::EchoEndpoint(delay, level, announce));
        });
        log(id, QString("other numbers: echo, %1 s late, %2 dB%3%4").arg(delay, 0, 'f', 1).arg(level, 0, 'f', 0)
            .arg(announce ? ", a tone when it answers" : "").arg(limit), Cellular::MsgReportLog::Network);
        return;
    }

    modemcellular::MusicOptions options;
    options.levelDb = std::min(0, farEnd.m_musicLevelDb);
    options.loop = farEnd.m_musicLoop;
    QString what = "the built-in melody";
    if (farEnd.m_musicSource == CellularFarEnd::MusicFile)
    {
        QString error;
        options.clip = CellularFarEndAudio::loadClip(farEnd.m_musicFile, error);
        if (options.clip) {
            what = QString("%1 (%2 s)").arg(farEnd.m_musicFile).arg(options.clip->size() / 8000.0, 0, 'f', 1);
        } else {
            log(id, "warning: music: " + error + "; the built-in melody plays", Cellular::MsgReportLog::Warning);
        }
    }
    else if (farEnd.m_musicSource == CellularFarEnd::MusicInput)
    {
        n.musicInput = std::make_unique<CellularMusicInput>(farEnd.m_musicInput);
        CellularMusicInput *input = n.musicInput.get();
        options.input = [input](float* samples, int count) { input->fill(samples, count); };
        what = QString("the audio input %1 (%2 Hz)").arg(farEnd.m_musicInput.isEmpty() ? "by default" : farEnd.m_musicInput)
            .arg(input->rate());
    }
    n.cellular->setFallbackFactory([options]() {
        return std::unique_ptr<modemcellular::AudioEndpoint>(new modemcellular::MusicEndpoint(options));
    });
    log(id, QString("other numbers: music, %1, %2 dB, %3%4").arg(what).arg(options.levelDb, 0, 'f', 0)
        .arg(options.loop || options.input ? "looped" : "once, then the call ends").arg(limit), Cellular::MsgReportLog::Network);
}

// Listening in to one call at a time: on the default speaker of SDRangel (the console's later).
void CellularWorker::listen(int network, const QString& number, int mode)
{
    static const char * const names[] = {"off", "both sides (the phone left, the far end right)",
        "the phone only", "the far end only"};
    using M = modemcellular::CallListener::Mode;
    const M modes[] = {M::Off, M::Both, M::FromPhone, M::ToPhone};
    mode = std::max(0, std::min(3, mode));
    AudioDeviceManager *audio = DSPEngine::instance()->getAudioDeviceManager();

    if (modes[mode] == M::Off)
    {
        if (m_listener) {
            m_listener->setMode(M::Off);
        }
        if (m_audioSink)
        {
            audio->removeAudioSink(&m_audioFifo);
            m_audioSink = false;
        }
        if (!m_listenNumber.isEmpty()) {
            log(m_listenNetwork, QString("listening to %1: off").arg(m_listenNumber), Cellular::MsgReportLog::Call);
        }
        m_listenNumber.clear();
        m_listenNetwork = 0;
        return;
    }
    const int rate = audio->getOutputSampleRate(-1);
    if (!m_listener || m_listener->mode() == M::Off) {
        m_listener = std::make_unique<modemcellular::CallListener>(rate > 0 ? rate : 48000);
    }
    if (!m_audioSink)
    {
        m_audioFifo.setSize(static_cast<uint32_t>((rate > 0 ? rate : 48000) / 2));   // 0.5 s
        audio->addAudioSink(&m_audioFifo, &m_inputMessageQueue, -1);
        m_audioSink = true;
    }
    // another call: the speaker moves there
    if (network != m_listenNetwork || number != m_listenNumber) {
        m_listener->reset();
    }
    m_listener->setMode(modes[mode]);
    m_listenNetwork = network;
    m_listenNumber = number;
    log(network, QString("listening to %1: %2").arg(number).arg(names[mode]), Cellular::MsgReportLog::Call);
}

// ---- the console ----

int CellularWorker::consoleOutputDevice() const
{
    return m_console.m_speaker.isEmpty() ? -1
        : DSPEngine::instance()->getAudioDeviceManager()->getOutputDeviceIndex(m_console.m_speaker);
}

void CellularWorker::setConsole(const CellularConsole& console)
{
    const bool renumbered = console.m_number != m_console.m_number;
    const bool newMic = console.m_microphone != m_console.m_microphone || !m_microphone;
    const bool newSpeaker = console.m_speaker != m_console.m_speaker || !m_consoleSpeaker;
    m_console = console;
    AudioDeviceManager *audio = DSPEngine::instance()->getAudioDeviceManager();

    if (newMic)
    {
        m_microphone = std::make_unique<CellularMusicInput>(m_console.m_microphone);
        // an input that is the monitor of an output (PipeWire/PulseAudio "....monitor") records what
        // the speaker plays: the phone hears itself. A warning only: the operator decides.
        const QString input = m_console.m_microphone.isEmpty() || m_console.m_microphone == AudioDeviceManager::m_defaultDeviceName
            ? AudioDeviceInfo::defaultInputDevice().deviceName() : m_console.m_microphone;
        if (input.contains("monitor", Qt::CaseInsensitive)) {
            log(0, QString("console: warning, the microphone %1 is the monitor of an audio output: the phone hears "
                "itself as an echo (mute the microphone or choose a real input)").arg(input));
        }
    }
    m_micGain = static_cast<float>(std::pow(10.0, std::max(-20, std::min(20, m_console.m_micGainDb)) / 20.0));
    if (newSpeaker)
    {
        if (m_consoleSink) {
            audio->removeAudioSink(&m_consoleFifo);
        }
        const int device = consoleOutputDevice();
        const int rate = audio->getOutputSampleRate(device);
        m_consoleSpeaker = std::make_unique<modemcellular::CallListener>(rate > 0 ? rate : 48000);
        m_consoleSpeaker->setMode(modemcellular::CallListener::Mode::FromPhone);
        m_consoleFifo.setSize(static_cast<uint32_t>((rate > 0 ? rate : 48000) / 2));
        audio->addAudioSink(&m_consoleFifo, &m_inputMessageQueue, device);
        m_consoleSink = true;
    }
    m_consoleSpeaker->setVolumeDb(m_console.m_volumeDb);
    for (const std::unique_ptr<Network>& n : m_networks)
    {
        if (!n->console) {
            continue;
        }
        if (renumbered) {
            makeConsole(*n);
        } else {
            n->console->setAutoAnswer(m_console.m_autoAnswer);
        }
    }
}

// The console's number on the network: a leg of its router.
void CellularWorker::makeConsole(Network& n)
{
    n.console.reset();
    if (!n.cellular || m_console.m_number.isEmpty()) {
        return;
    }
    n.console = std::make_unique<modemcellular::ConsoleLeg>(*n.cellular, m_console.m_number.toStdString());
    n.console->setAutoAnswer(m_console.m_autoAnswer);
    Network *network = &n;
    n.console->setEventHandler([this, network](const std::string& text) { consoleEvent(*network, text); });
    n.console->setMicrophone([this](float* samples, int count) {
        if (m_microphone)
        {
            m_microphone->fill(samples, count);
            const float gain = m_console.m_micMute ? 0.0f : m_micGain;
            for (int i = 0; i < count; i++) {
                samples[i] *= gain;
            }
        }
    });
    n.console->setSpeaker([this](const float* samples, int count) {
        if (m_consoleSpeaker)
        {
            m_consoleSpeaker->fromPhone(samples, count);
            // the speaker's other side stays silent (mono of the far end)
            static const std::vector<float> silence(1024, 0.0f);
            m_consoleSpeaker->toPhone(silence.data(), std::min(count, 1024));
        }
    });
}

CellularWorker::Network *CellularWorker::consoleBusy()
{
    for (const std::unique_ptr<Network>& n : m_networks)
    {
        if (n->console && n->console->state() != modemcellular::ConsoleLeg::State::Idle) {
            return n.get();
        }
    }
    return nullptr;
}

// The network of a console call: given, the one the number was heard on, else the first on the air.
CellularWorker::Network *CellularWorker::consoleNetwork(int network, const QString& number)
{
    Network *first = nullptr;
    for (const std::unique_ptr<Network>& n : m_networks)
    {
        if (!running(*n) || !n->console) {
            continue;
        }
        if (n->id == network) {
            return n.get();
        }
        if (network == 0 && n->phones.count(number.toStdString()) > 0) {
            return n.get();
        }
        if (!first) {
            first = n.get();
        }
    }
    return network == 0 ? first : nullptr;
}

void CellularWorker::consoleEvent(Network& n, const std::string& text)
{
    // one call at a time over all networks: a call that rings while the console is busy on another
    if (text.find(": ringing") != std::string::npos && text.rfind("call from", 0) == 0)
    {
        for (const std::unique_ptr<Network>& other : m_networks)
        {
            if (other.get() != &n && other->console && other->console->state() != modemcellular::ConsoleLeg::State::Idle)
            {
                log(n.id, "console: " + QString::fromStdString(text) + ", refused: the console is in a call on " + other->name,
                    Cellular::MsgReportLog::Call);
                n.console->hangUp();
                return;
            }
        }
    }
    log(n.id, "console: " + QString::fromStdString(text), Cellular::MsgReportLog::Call);
    m_consoleState = -1;        // report now
}

void CellularWorker::consoleAction(const Cellular::MsgAction& msg)
{
    const QString what = QString("console %1 %2").arg(Cellular::MsgAction::name(msg.getAction())).arg(msg.getNumber());
    Network *busy = consoleBusy();

    switch (msg.getAction())
    {
    case Cellular::MsgAction::ConsoleDial:
    {
        if (busy)
        {
            log(busy->id, what + ": the console is in a call");
            return;
        }
        Network *n = consoleNetwork(msg.getNetwork(), msg.getNumber());
        if (!n)
        {
            log(0, what + ": no network on the air");
            return;
        }
        modemcellular::Cause cause = modemcellular::Cause::Normal;
        // a phone's number (registered or not) is paged on the network, as a phone's call to it is
        const std::string dialled = msg.getNumber().toStdString();
        if (!n->console->dial(dialled, n->bridge->legOf(dialled), &cause)) {
            log(n->id, what + QString(": refused (%1)").arg(modemcellular::TestCaller::causeName(cause)));
        }
        m_consoleKeys.clear();
        return;
    }
    case Cellular::MsgAction::ConsoleAnswer:
        if (busy) {
            busy->console->answer();
        }
        m_consoleKeys.clear();
        return;
    case Cellular::MsgAction::ConsoleHangUp:
        if (busy) {
            busy->console->hangUp();
        }
        return;
    case Cellular::MsgAction::ConsoleDigit:
        if (busy && !msg.getText().isEmpty()) {
            busy->console->digit(msg.getText().at(0).toLatin1());
        }
        return;
    case Cellular::MsgAction::ConsoleSms:
    {
        const std::string number = msg.getNumber().toStdString();
        Network *n = consoleNetwork(msg.getNetwork(), msg.getNumber());
        if (!n || number.size() != 7)
        {
            log(n ? n->id : 0, what + ": the number is the country digit and six digits, on a network on the air");
            return;
        }
        QString bad;
        const modemnmt::SmsDeliver message = deliverMessage(n->smsCodepage, m_console.m_number.toStdString(), msg.getText(), bad);
        if (!bad.isEmpty())
        {
            log(n->id, QString("console SMS to %1: refused: '%2' has no code in the SMS codepage of the network")
                .arg(msg.getNumber()).arg(bad), Cellular::MsgReportLog::Sms);
            return;
        }
        const bool ok = n->station->system()->sendSms(number[0], number.substr(1), message);
        if (ok) {
            smsRecord(*n, false, msg.getNumber(), m_console.m_number, sentDcs(message), CellularSmsText::codesOf(message.text));
        }
        log(n->id, QString("console SMS to %1 from %2: %3").arg(msg.getNumber()).arg(m_console.m_number)
            .arg(ok ? "\"" + msg.getText() + "\" on its way" : "refused (the phone is busy or unknown)"), Cellular::MsgReportLog::Sms);
        return;
    }
    default:
        return;
    }
}

void CellularWorker::reportConsole()
{
    Network *busy = consoleBusy();
    const int state = busy ? static_cast<int>(busy->console->state()) : 0;

    if (state != m_consoleState) {
        m_consoleSince = QDateTime::currentDateTime();
    }
    // when it changes, and once a second during a call (its time)
    if (state == m_consoleState && (state == 0 || ++m_consoleTick % kBlocksPerReport != 0)) {
        return;
    }
    m_consoleState = state;
    if (!m_reports) {
        return;
    }
    Cellular::MsgReportConsole *msg = Cellular::MsgReportConsole::create();
    msg->m_state = static_cast<Cellular::MsgReportConsole::State>(state);
    msg->m_network = busy ? busy->id : 0;
    msg->m_number = m_console.m_number;
    msg->m_peer = busy ? QString::fromStdString(busy->console->peer()) : QString();
    msg->m_since = m_consoleSince;
    msg->m_keys = m_consoleKeys;
    m_reports->push(msg);
}

void CellularWorker::consoleOutput()
{
    if (!m_consoleSpeaker || !m_consoleSink) {
        return;
    }
    m_consoleFrames.clear();
    const size_t frames = m_consoleSpeaker->take(m_consoleFrames);
    if (frames > 0 && !m_console.m_speakerMute) {
        m_consoleFifo.write(reinterpret_cast<const quint8*>(m_consoleFrames.data()), static_cast<uint32_t>(frames));
    }
}

void CellularWorker::listenOutput()
{
    if (!m_listener || m_listener->mode() == modemcellular::CallListener::Mode::Off) {
        return;
    }
    m_listenFrames.clear();
    const size_t frames = m_listener->take(m_listenFrames);
    if (frames > 0 && !m_console.m_speakerMute) {
        m_audioFifo.write(reinterpret_cast<const quint8*>(m_listenFrames.data()), static_cast<uint32_t>(frames));
    }
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
        log(n.id, QString("phone %1 heard on channel %2").arg(QString::fromStdString(id)).arg(channel.config().channel),
            Cellular::MsgReportLog::Phone);
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
            .arg(toPhone ? ">" : "<").arg(toPhone ? trace.outType : trace.inType).arg(repeats).arg(repeats == 1 ? "" : "s"),
            Cellular::MsgReportLog::Frame);
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
            log(l.network, QString("tones from %1: %2").arg(QString::fromStdString(t.first)).arg(QString::fromStdString(l.keys)),
                Cellular::MsgReportLog::Tone);
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
    sendNames(radio);
}

// the names of the networks on the radio, for the display of its channel
void CellularWorker::sendNames(Radio& radio)
{
    modemcellular::WireNames names;

    for (const std::unique_ptr<Network>& n : m_networks)
    {
        if (running(*n) && (radio.tx ? n->tx : n->rx) == &radio)
        {
            // 60 UTF-16 units are at most 180 bytes of UTF-8; a pair is not cut in half
            QString name = n->name.left(60);
            if (!name.isEmpty() && name.at(name.size() - 1).isHighSurrogate()) {
                name.chop(1);
            }
            names.networks.push_back(n->id);
            names.names.push_back(name.toStdString());
        }
    }
    sendTo(radio.channel, modemcellular::encodeNames(names));
}

void CellularWorker::rename(const MsgNames& msg)
{
    std::vector<Radio*> radios;

    for (const std::unique_ptr<Network>& n : m_networks)
    {
        const auto it = msg.m_names.find(n->id);
        if (it == msg.m_names.end() || it->second == n->name) {
            continue;
        }
        n->name = it->second;
        for (Radio *r : {n->rx, n->tx})
        {
            if (r && running(*n) && std::find(radios.begin(), radios.end(), r) == radios.end()) {
                radios.push_back(r);
            }
        }
    }
    for (Radio *r : radios) {
        sendNames(*r);
    }
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
    case modemcellular::WireKind::Names:
    case modemcellular::WireKind::Speech:
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
    n.smsCodepage = add.m_smsCodepage;
    // the round trip of the radio: the transmit lead and about 100 ms of the receive path (device
    // buffers, the pipe of the Cellular Rx), in frame periods of 166 bits at 1200 bit/s
    {
        const double roundTrip = m_lead / modemnmt::NmtBaseStation::kAudioRate + 0.1;
        n.config.roundTripFrames = static_cast<int>(std::ceil(roundTrip / (166.0 / 1200.0)));
    }
    n.station = std::make_unique<modemnmt::NmtBaseStation>(n.config);
    n.cellular = std::make_unique<modemcellular::CellularSystem>(n.station->clock(), modemcellular::ToneSet::Denmark);
    n.music = add.m_fallback == CellularNetworkSettings::FallbackMusic;
    const int id = n.id;
    setFarEnd(n, add.m_farEnd);
    n.cellular->setSubscriberListener([this, id](const std::string& number, bool registered) {
        log(id, QString("%1 %2").arg(QString::fromStdString(number)).arg(registered ? "registered" : "gone"), Cellular::MsgReportLog::Phone);
    });

    for (const modemnmt::NmtPlanIssue& issue : n.station->issues()) {
        log(id, issueText(issue), Cellular::MsgReportLog::Warning);
    }
    log(id, QString("the phones get %1 frames for their identity (T = 8 at the phone, %2 for the round trip of the radio)")
        .arg(8 + n.config.roundTripFrames).arg(n.config.roundTripFrames));
    for (const modemnmt::NmtCarrierStatus& c : n.station->carriers())
    {
        for (const modemnmt::NmtPlanIssue& issue : c.issues) {
            log(id, QString("channel %1: %2").arg(c.config.channel).arg(issueText(issue)), Cellular::MsgReportLog::Warning);
        }
    }

    m_networks.push_back(std::move(network));
    n.rx = radioFor(add.m_rxChannel, add.m_rxPipe, false);
    n.tx = radioFor(add.m_txChannel, add.m_txPipe, true);
    log(id, QString("%1 started").arg(n.name), Cellular::MsgReportLog::Network);
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
        log(id, QString("%1 stopped").arg(n.name), Cellular::MsgReportLog::Network);
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
    // the call of the tester and the console go before the router and the base station they use
    n.caller.reset();
    n.console.reset();
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
                .arg(issueText(issue)), Cellular::MsgReportLog::Warning);
        }
    }
    for (const modemnmt::NmtPlanIssue& issue : n.station->streamIssues()) {
        log(n.id, issueText(issue), Cellular::MsgReportLog::Warning);
    }
    if (!ok)
    {
        log(n.id, QString("the network is off (receiver %1, transmitter %2)").arg(spanText(rxSpan)).arg(spanText(txSpan)),
            Cellular::MsgReportLog::Warning);
        reportStatus(n);
        return;
    }

    n.bridge = std::make_unique<modemnmt::NmtCellularBridge>(*n.station->system(), *n.cellular);
    n.caller = std::make_unique<modemcellular::TestCaller>(*n.cellular);
    makeConsole(n);
    setHandlers(n);
    n.phones.clear();
    n.boundRx = rxSpan;
    n.boundTx = txSpan;
    n.levels.assign(n.station->system()->channelCount(), -100.0f);
    n.traces.assign(n.station->system()->channelCount(), Network::Trace());
    n.speechSeen.assign(n.station->system()->channelCount(), Network::SpeechSeen());
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
                .arg(rx ? "<" : ">").arg(QString::fromStdString(text)), Cellular::MsgReportLog::Frame);
        });
    }
    n.batches = 0;
    n.gaps = 0;
    updateTuning(*n.rx);
    updateTuning(*n.tx);

    const size_t carriers = n.station->system()->channelCount();
    log(n.id, QString("on the air with %1 carrier%2 (receiver %3, transmitter %4, lead %5 ms)")
        .arg(carriers).arg(carriers == 1 ? "" : "s").arg(spanText(rxSpan)).arg(spanText(txSpan))
        .arg(m_lead / modemnmt::NmtBaseStation::kAudioRate * 1e3, 0, 'f', 0), Cellular::MsgReportLog::Network);
    static const char * const levels[] = {"low, 20 dB less", "low, 20 dB less", "medium, 10 dB less", "high"};
    const int power = std::max(0, std::min(3, n.config.msPower));
    log(n.id, QString("the phones send with power level %1 at most (%2), in the power bits of every frame")
        .arg(power).arg(levels[power]), Cellular::MsgReportLog::Power);
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
        if (n.console) {
            n.console->process(static_cast<int>(m_rx.length * modemcellular::kAudioSampleRate / modemnmt::NmtBaseStation::kAudioRate));
        }
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
        if (n.batches % kBlocksPerSpeech == 0) {
            reportSpeech(n, n.batches % (2 * kBlocksPerReport) == 0);    // a log line per 2 s window
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
    listenOutput();
    consoleOutput();
    reportConsole();
    // the audio inputs that nothing took (no call) are thrown away
    if (m_microphone) {
        m_microphone->drain();
    }
    for (const std::unique_ptr<Network>& n : m_networks)
    {
        if (n->musicInput) {
            n->musicInput->drain();
        }
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

// One direction of a call's speech for the log: level, where 99 % of it stays below, the share at
// the limit, the pauses, and the six bands relative to the speech.
static QString speechText(const modemcellular::SpeechStats& s)
{
    if (s.speech == 0) {
        return QString("no speech (pauses %1 %)").arg(100.0 * s.pauseShare(), 0, 'f', 0);
    }
    const std::array<double, modemcellular::SpeechStats::kBands> b = s.bandsDb();
    QString bands;

    for (double db : b) {
        bands += QString(" %1").arg(std::max(db, -40.0), 0, 'f', 0);
    }
    // the peak as deviation (1.0 = 3 kHz): piled at 4.7 kHz the phone limits, beyond it over-deviates
    return QString("%1 dB rms, 99 % below %2 dB, %3 % at the limit, peak %4 kHz, pauses %5 %, bands%6 dB")
        .arg(s.levelDb(), 0, 'f', 1).arg(s.p99Db(), 0, 'f', 1).arg(100.0 * s.limitShare(), 0, 'f', 1)
        .arg(3.0 * s.peak, 0, 'f', 1).arg(100.0 * s.pauseShare(), 0, 'f', 0).arg(bands);
}

void CellularWorker::reportSpeech(Network& n, bool logLine)
{
    modemnmt::NmtSystem *system = running(n) ? n.station->system() : nullptr;

    if (!system) {
        return;
    }
    Cellular::MsgReportSpeech *msg = Cellular::MsgReportSpeech::create(n.id);
    // to the boxes of the carriers in the radios: the receiver shows the speech from the phone,
    // the transmitter the speech to it
    modemcellular::WireSpeech toRx, toTx;
    std::vector<int> ids;                       // carrier id of each channel on air

    for (size_t k = 0; k < n.station->carriers().size(); k++)
    {
        if (n.station->carriers()[k].onAir) {
            ids.push_back(n.station->carrierId(k));
        }
    }

    for (size_t i = 0; i < system->channelCount() && i < n.speechSeen.size(); i++)
    {
        const modemnmt::NmtChannel& channel = system->channel(i);
        const modemnmt::NmtChannel::Speech s = channel.speech();
        Network::SpeechSeen& seen = n.speechSeen[i];
        const bool ended = s.call > 0 && !s.live && seen.call == s.call && seen.live;

        if (!s.live && !ended)
        {
            seen.call = s.call;
            seen.live = false;
            continue;
        }
        CellularCallSpeech call;
        modemnmt::NmtSubscriber subscriber;
        subscriber.country = s.country;
        subscriber.number = s.number;
        call.channel = channel.config().channel;
        call.phone = numberText(subscriber);
        call.call = s.call;
        call.live = s.live;
        call.rxRecent = s.rxRecent;
        call.txRecent = s.txRecent;
        call.rxCall = s.rxCall;
        call.txCall = s.txCall;
        call.limited = s.limited;
        if (m_listener && n.id == m_listenNetwork && call.phone == m_listenNumber) {
            call.listen = static_cast<int>(m_listener->mode());
        }
        if (ended)
        {
            // the whole call, kept with the phone and in the log
            call.ended = QDateTime::currentDateTime();
            Cellular::MsgReportStatus::Phone& phone = n.phones[call.phone.toStdString()];
            if (phone.number.isEmpty()) {
                phone.number = call.phone;
            }
            phone.lastCall = call;
            // the speaker of a call that ended goes off
            if (n.id == m_listenNetwork && call.phone == m_listenNumber) {
                listen(n.id, call.phone, 0);
            }
            log(n.id, QString("ch %1 call of %2 ended, speech from the phone: %3; to the phone: %4, limited %5")
                .arg(call.channel).arg(call.phone).arg(speechText(s.rxCall)).arg(speechText(s.txCall)).arg(s.limited),
                Cellular::MsgReportLog::Call);
        }
        else if (logLine)
        {
            log(n.id, QString("ch %1 speech of %2 (2 s) from the phone: %3; to the phone: %4")
                .arg(call.channel).arg(call.phone).arg(speechText(s.rxRecent)).arg(speechText(s.txRecent)),
                Cellular::MsgReportLog::Call);
        }
        seen.call = s.call;
        seen.live = s.live;
        msg->m_calls.append(call);
        if (i < ids.size())
        {
            toRx.carriers.push_back({ids[i], s.live, s.rxRecent, s.rxCall});
            toTx.carriers.push_back({ids[i], s.live, s.txRecent, s.txCall});
        }
    }
    // the destination of a call goes with the phone off the channels (after a while: it is noted
    // when the phone dials, before its channel shows the call)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = n.callInfo.begin(); it != n.callInfo.end();)
    {
        bool onChannel = false;
        for (size_t i = 0; i < system->channelCount(); i++)
        {
            const modemnmt::NmtTransaction *t = system->channel(i).currentTransaction();
            onChannel = onChannel || (t && modemnmt::NmtCellularBridge::numberOf(t->subscriber) == it->first);
        }
        if (!onChannel && now - it->second.since > 5000) {
            it = n.callInfo.erase(it);
        } else {
            ++it;
        }
    }
    if (!toRx.carriers.empty())
    {
        sendTo(n.rx->channel, modemcellular::encodeSpeech(toRx));
        sendTo(n.tx->channel, modemcellular::encodeSpeech(toTx));
    }
    if (msg->m_calls.isEmpty() || !m_reports) {
        delete msg;
    } else {
        m_reports->push(msg);
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
            row.framesReceived = system->channel(onAir).framesReceived();
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
            if (!c.phone.isEmpty() && phones.count(c.phone.toStdString()) > 0)
            {
                // with the destination of its call when it has one
                const auto info = n.callInfo.find(c.phone.toStdString());
                phones[c.phone.toStdString()].state = info != n.callInfo.end()
                    ? QString("channel %1: %2: %3").arg(c.channel).arg(info->second.text).arg(c.state)
                    : QString("channel %1: %2").arg(c.channel).arg(c.state);
            }
        }
        // a call to a phone that is still on the calling channel: scheme B before the traffic channel
        system->forEachTransaction([&phones, &n](const modemnmt::NmtTransaction& t) {
            const char *step = nullptr;
            switch (t.ccPhase)
            {
            case modemnmt::NmtTransaction::CcPhase::Deferred: step = "B2 MS is called (waiting for its battery saving group)"; break;
            case modemnmt::NmtTransaction::CcPhase::Page: step = "B2 MS is called (2a)"; break;
            case modemnmt::NmtTransaction::CcPhase::Order: step = "B5 MTX sends channel order (2b)"; break;
            case modemnmt::NmtTransaction::CcPhase::Queue: step = "B5 queueing (2f), waiting for a free TC"; break;
            case modemnmt::NmtTransaction::CcPhase::None: break;
            }
            if (!step) {
                return;
            }
            const std::string number = modemnmt::NmtCellularBridge::numberOf(t.subscriber);
            Cellular::MsgReportStatus::Phone& phone = phones[number];
            phone.number = QString::fromStdString(number);
            const auto info = n.callInfo.find(number);
            phone.state = QString("calling channel: %1%2").arg(info != n.callInfo.end() ? info->second.text + ": " : QString()).arg(step);
        });
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
