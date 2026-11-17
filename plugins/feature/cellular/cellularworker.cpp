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

#include <cmath>

#include <QDebug>

#include "channel/channelapi.h"
#include "maincore.h"
#include "pipes/messagepipes.h"
#include "pipes/objectpipe.h"

#include "carrierradio.h"
#include "tonegenerator.h"

#include "cellular.h"
#include "cellularworker.h"

MESSAGE_CLASS_DEFINITION(CellularWorker::MsgAddNetwork, Message)

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
    n.station = std::make_unique<modemnmt::NmtBaseStation>(n.config);
    n.cellular = std::make_unique<modemcellular::CellularSystem>(n.station->clock(), modemcellular::ToneSet::Denmark);
    n.cellular->setFallback(add.m_fallback == CellularNetworkSettings::FallbackMusic
        ? modemcellular::CallRouter::Fallback::Music : modemcellular::CallRouter::Fallback::Echo);
    const int id = n.id;
    n.cellular->setSubscriberListener([this, id](const std::string& number, bool registered) {
        log(id, QString("%1 %2").arg(QString::fromStdString(number)).arg(registered ? "registered" : "gone"));
    });

    for (const modemnmt::NmtPlanIssue& issue : n.station->issues()) {
        log(id, issueText(issue));
    }
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
    n.boundRx = rxSpan;
    n.boundTx = txSpan;
    n.levels.assign(n.station->system()->channelCount(), -100.0f);
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

    m_reports->push(msg);
}
