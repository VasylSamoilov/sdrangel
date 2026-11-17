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

#include <QDebug>

#include "channel/channelapi.h"
#include "maincore.h"

#include "carrierradio.h"
#include "tonegenerator.h"

#include "cellular.h"
#include "cellularworker.h"

MESSAGE_CLASS_DEFINITION(CellularWorker::MsgStart, Message)

using modemnmt::ChannelType;
using modemnmt::NmtChannel;

namespace
{

const int kBlocksPerReport = 100;      // 1 s of 10 ms blocks
const int kBlocksPerLog = 1000;        // a line in the event log every 10 s

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

} // namespace

CellularWorker::CellularWorker() :
    m_rxChannel(nullptr),
    m_txChannel(nullptr),
    m_rxPipe(nullptr),
    m_txPipe(nullptr),
    m_reports(nullptr),
    m_lead(4800),
    m_fallback(0),
    m_running(false),
    m_haveRxSpan(false),
    m_haveTxSpan(false),
    m_batches(0),
    m_gaps(0),
    m_nextSample(0)
{
    connect(&m_inputMessageQueue, SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
}

CellularWorker::~CellularWorker()
{
    m_bridge.reset();
    m_cellular.reset();
    m_station.reset();
}

void CellularWorker::handleInputMessages()
{
    Message* message;

    while ((message = m_inputMessageQueue.pop()))
    {
        if (MsgStart::match(*message)) {
            startWork(static_cast<const MsgStart&>(*message));
        }
        delete message;
    }
}

void CellularWorker::log(const QString& text)
{
    qInfo() << "CellularWorker:" << text;
    if (m_reports) {
        m_reports->push(Cellular::MsgReportLog::create(text));
    }
}

void CellularWorker::sendTo(ChannelAPI *channel, const std::vector<uint8_t>& wire)
{
    if (!channel) {
        return;
    }
    const QByteArray bytes(reinterpret_cast<const char*>(wire.data()), static_cast<int>(wire.size()));
    channel->getInputMessageQueue()->push(MainCore::MsgPacket::create(this, bytes));
}

void CellularWorker::startWork(const MsgStart& start)
{
    m_config = start.m_config;
    m_rxChannel = start.m_rxChannel;
    m_txChannel = start.m_txChannel;
    m_rxPipe = start.m_rxPipe;
    m_txPipe = start.m_txPipe;
    m_reports = start.m_reports;
    m_lead = start.m_lead;
    m_fallback = start.m_fallback;
    m_haveRxSpan = false;
    m_haveTxSpan = false;
    m_boundRx = modemcellular::WireSpan();
    m_boundTx = modemcellular::WireSpan();
    m_batches = 0;
    m_gaps = 0;
    m_txStatus = modemcellular::WireTxStatus();

    m_station = std::make_unique<modemnmt::NmtBaseStation>(m_config);
    m_cellular = std::make_unique<modemcellular::CellularSystem>(m_station->clock(), modemcellular::ToneSet::Denmark);
    m_cellular->setFallback(m_fallback == CellularSettings::FallbackMusic
        ? modemcellular::CallRouter::Fallback::Music : modemcellular::CallRouter::Fallback::Echo);
    m_cellular->setSubscriberListener([this](const std::string& number, bool registered) {
        log(QString("%1 %2").arg(QString::fromStdString(number)).arg(registered ? "registered" : "gone"));
    });

    for (const modemnmt::NmtPlanIssue& issue : m_station->issues()) {
        log(QString("%1: %2").arg(issue.error ? "error" : "warning").arg(QString::fromStdString(issue.text)));
    }
    for (const modemnmt::NmtCarrierStatus& c : m_station->carriers())
    {
        for (const modemnmt::NmtPlanIssue& issue : c.issues) {
            log(QString("channel %1: %2: %3").arg(c.config.channel).arg(issue.error ? "error" : "warning")
                .arg(QString::fromStdString(issue.text)));
        }
    }

    if (m_rxPipe) {
        connect(m_rxPipe, &MessageQueue::messageEnqueued, this, [this]() { handlePipe(m_rxPipe, false); }, Qt::QueuedConnection);
    }
    if (m_txPipe) {
        connect(m_txPipe, &MessageQueue::messageEnqueued, this, [this]() { handlePipe(m_txPipe, true); }, Qt::QueuedConnection);
    }
    m_running = true;

    log("waiting for the spans of the receiver and the transmitter");
    sendTo(m_rxChannel, modemcellular::encodeSpanRequest());
    sendTo(m_txChannel, modemcellular::encodeSpanRequest());
    reportStatus();
}

void CellularWorker::stopWork()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    if (m_rxPipe) {
        disconnect(m_rxPipe, &MessageQueue::messageEnqueued, this, nullptr);
    }
    if (m_txPipe) {
        disconnect(m_txPipe, &MessageQueue::messageEnqueued, this, nullptr);
    }
    unbind();
    m_bridge.reset();
    m_cellular.reset();
    m_station.reset();
    log("stopped");
    m_rxChannel = nullptr;
    m_txChannel = nullptr;
    m_rxPipe = nullptr;
    m_txPipe = nullptr;
}

// takes the carriers off the radios
void CellularWorker::unbind()
{
    m_bridge.reset();
    sendTo(m_rxChannel, modemcellular::encodeTuning(modemcellular::WireTuning()));
    sendTo(m_txChannel, modemcellular::encodeTuning(modemcellular::WireTuning()));
    m_boundRx = modemcellular::WireSpan();
    m_boundTx = modemcellular::WireSpan();
}

void CellularWorker::handlePipe(MessageQueue *queue, bool fromTx)
{
    Message *message;

    while ((message = queue->pop()))
    {
        if (m_running && MainCore::MsgPacket::match(*message)) {
            handlePacket(static_cast<const MainCore::MsgPacket&>(*message).getPacket(), fromTx);
        }
        delete message;
    }
}

void CellularWorker::handlePacket(const QByteArray& bytes, bool fromTx)
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
        if (fromTx)
        {
            m_txSpan = span;
            m_haveTxSpan = true;
        }
        else
        {
            m_rxSpan = span;
            m_haveRxSpan = true;
        }
        tryBind();
        return;
    }
    case modemcellular::WireKind::Batch:
        if (!fromTx && modemcellular::decodeBatch(data, size, m_rx)) {
            processBatch();
        }
        return;
    case modemcellular::WireKind::TxStatus:
        if (fromTx) {
            modemcellular::decodeTxStatus(data, size, m_txStatus);
        }
        return;
    case modemcellular::WireKind::None:
    case modemcellular::WireKind::SpanRequest:
    case modemcellular::WireKind::Tuning:
        return;
    }
}

void CellularWorker::tryBind()
{
    if (!m_haveRxSpan || !m_haveTxSpan) {
        return;
    }
    if (m_station->running() && sameSpan(m_rxSpan, m_boundRx) && sameSpan(m_txSpan, m_boundTx)) {
        return;
    }

    unbind();
    if (m_rxSpan.sampleRate <= 0.0 || m_txSpan.sampleRate <= 0.0)
    {
        log("waiting for both devices to run");
        reportStatus();
        return;
    }
    if (!modemcellular::CarrierRadio::rateSupported(m_rxSpan.sampleRate) || !modemcellular::CarrierRadio::rateSupported(m_txSpan.sampleRate))
    {
        log(QString("error: the sample rates (receiver %1, transmitter %2 kS/s) have to be even multiples of 48 kHz, e.g. 960 kS/s")
            .arg(m_rxSpan.sampleRate / 1e3).arg(m_txSpan.sampleRate / 1e3));
        reportStatus();
        return;
    }

    const modemnmt::NmtStreamSpan rx = {1, m_rxSpan.centre, m_rxSpan.sampleRate};
    const modemnmt::NmtStreamSpan tx = {2, m_txSpan.centre, m_txSpan.sampleRate};
    const bool ok = m_station->bind({rx}, {tx});

    for (const modemnmt::NmtCarrierStatus& c : m_station->carriers())
    {
        for (const modemnmt::NmtPlanIssue& issue : c.streamIssues) {
            log(QString("channel %1 (%2 / %3 MHz): %4: %5").arg(c.config.channel)
                .arg(c.frequency.downlink / 1e6, 0, 'f', 4).arg(c.frequency.uplink / 1e6, 0, 'f', 4)
                .arg(issue.error ? "error" : "warning").arg(QString::fromStdString(issue.text)));
        }
    }
    for (const modemnmt::NmtPlanIssue& issue : m_station->streamIssues()) {
        log(QString("%1: %2").arg(issue.error ? "error" : "warning").arg(QString::fromStdString(issue.text)));
    }
    if (!ok)
    {
        log(QString("the network is off (receiver %1, transmitter %2)").arg(spanText(m_rxSpan)).arg(spanText(m_txSpan)));
        reportStatus();
        return;
    }

    m_bridge = std::make_unique<modemnmt::NmtCellularBridge>(*m_station->system(), *m_cellular);
    m_boundRx = m_rxSpan;
    m_boundTx = m_txSpan;
    m_levels.assign(m_station->system()->channelCount(), -100.0f);
    m_nextSample = 0;

    modemcellular::WireTuning rxTuning;
    rxTuning.carriers = m_station->rxTuning();
    modemcellular::WireTuning txTuning;
    txTuning.carriers = m_station->txTuning();
    txTuning.lead = m_lead;
    sendTo(m_rxChannel, modemcellular::encodeTuning(rxTuning));
    sendTo(m_txChannel, modemcellular::encodeTuning(txTuning));

    log(QString("on the air with %1 carriers (receiver %2, transmitter %3, lead %4 ms)")
        .arg(rxTuning.carriers.size()).arg(spanText(m_rxSpan)).arg(spanText(m_txSpan))
        .arg(m_lead / modemnmt::NmtBaseStation::kAudioRate * 1e3, 0, 'f', 0));
    reportStatus();
}

void CellularWorker::processBatch()
{
    if (!m_station || !m_station->running() || !m_bridge) {
        return;
    }
    if (m_batches > 0 && m_rx.sample != m_nextSample) {
        m_gaps += m_rx.sample > m_nextSample ? (m_rx.sample - m_nextSample) / static_cast<uint64_t>(m_rx.length > 0 ? m_rx.length : 1) : 1;
    }
    m_nextSample = m_rx.sample + static_cast<uint64_t>(m_rx.length);

    m_station->process(m_rx, m_tx);
    m_bridge->run();
    sendTo(m_txChannel, modemcellular::encodeBatch(m_tx));

    for (size_t i = 0; i < m_tx.carriers.size() && i < m_levels.size(); i++)
    {
        const int at = m_rx.find(m_tx.carriers[i]);

        if (at >= 0 && static_cast<size_t>(at) < m_rx.levels.size()) {
            m_levels[i] = m_rx.levels[static_cast<size_t>(at)];
        }
    }

    if (++m_batches % kBlocksPerReport == 0) {
        reportStatus();
    }
    if (m_batches % kBlocksPerLog == 0)
    {
        QString line = QString("%1 s on the air, %2 blocks missed;")
            .arg(m_station->samples() / modemnmt::NmtBaseStation::kAudioRate, 0, 'f', 0).arg(m_gaps);
        for (size_t k = 0; k < m_txStatus.carriers.size(); k++)
        {
            line += QString(" ch %1: tx %2 ms, %3 empty;").arg(m_txStatus.carriers[k] % modemnmt::NmtBaseStation::kIdStride)
                .arg(m_txStatus.fill[k] / modemnmt::NmtBaseStation::kAudioRate * 1e3, 0, 'f', 0)
                .arg(m_txStatus.underruns[k]);
        }
        log(line);
    }
}

void CellularWorker::reportStatus()
{
    if (!m_reports || !m_station) {
        return;
    }
    Cellular::MsgReportStatus *msg = Cellular::MsgReportStatus::create();
    modemnmt::NmtSystem *system = m_station->system();

    if (system)
    {
        msg->m_summary = QString("On the air: %1 registered, %2 calls, %3 s, %4 blocks missed")
            .arg(system->registeredCount())
            .arg(m_cellular ? m_cellular->callCount() : 0)
            .arg(m_station->samples() / modemnmt::NmtBaseStation::kAudioRate, 0, 'f', 0)
            .arg(m_gaps);
    }
    else if (!m_haveRxSpan || !m_haveTxSpan) {
        msg->m_summary = QString("Waiting for the %1").arg(!m_haveRxSpan ? "receiver" : "transmitter");
    } else {
        msg->m_summary = "Off (see the log)";
    }

    size_t onAir = 0;
    for (size_t i = 0; i < m_station->carriers().size(); i++)
    {
        const modemnmt::NmtCarrierStatus& c = m_station->carriers()[i];
        Cellular::MsgReportStatus::Carrier row;

        row.channel = c.config.channel;
        row.type = typeName(c.config.type);
        row.downlink = c.frequency.downlink;
        row.uplink = c.frequency.uplink;
        if (c.onAir && system && onAir < system->channelCount())
        {
            row.state = stateName(system->channel(onAir).state());
            row.rxLevel = onAir < m_levels.size() ? m_levels[onAir] : -100.0f;
            const int id = m_station->carrierId(i);
            for (size_t k = 0; k < m_txStatus.carriers.size(); k++)
            {
                if (m_txStatus.carriers[k] == id)
                {
                    row.txFillMs = m_txStatus.fill[k] / modemnmt::NmtBaseStation::kAudioRate * 1e3;
                    row.underruns = m_txStatus.underruns[k];
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
