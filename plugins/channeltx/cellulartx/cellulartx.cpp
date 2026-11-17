///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2015-2018 Edouard Griffiths, F4EXB.                             //
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

#include "device/deviceapi.h"
#include "dsp/dspcommands.h"
#include "maincore.h"
#include "pipes/messagepipes.h"

#include "carrierwire.h"

#include "cellulartx.h"

const char * const CellularTx::m_channelIdURI = "sdrangel.channeltx.cellulartx";
const char * const CellularTx::m_channelId = "CellularTx";

namespace
{

// A queue that holds this much more than its lead is not read (the device does not run, or runs
// slower than the receiver by far more than crystals do): it starts again from its lead.
const size_t kOverrun = 24000;      // 0.5 s

} // namespace

CellularTx::CellularTx(DeviceAPI *deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSource),
    m_deviceAPI(deviceAPI),
    m_basebandSampleRate(0),
    m_centerFrequency(0),
    m_lead(0),
    m_batches(0),
    m_resets(0),
    m_audioWritten(0),
    m_audioRead(0),
    m_lastRatio(1.0),
    m_statusCount(0)
{
    setObjectName(m_channelId);

    m_deviceAPI->addChannelSource(this);
    m_deviceAPI->addChannelSourceAPI(this);

    QObject::connect(&m_statusTimer, &QTimer::timeout, this, &CellularTx::sendStatus);
    m_statusTimer.start(1000);
}

CellularTx::~CellularTx()
{
    m_statusTimer.stop();
    m_deviceAPI->removeChannelSourceAPI(this);
    m_deviceAPI->removeChannelSource(this, true);
}

void CellularTx::setDeviceAPI(DeviceAPI *deviceAPI)
{
    if (deviceAPI != m_deviceAPI)
    {
        m_deviceAPI->removeChannelSourceAPI(this);
        m_deviceAPI->removeChannelSource(this, false);
        m_deviceAPI = deviceAPI;
        m_deviceAPI->addChannelSource(this);
        m_deviceAPI->addChannelSourceAPI(this);
    }
}

void CellularTx::start()
{
    qDebug("CellularTx::start");
    QMutexLocker mutexLocker(&m_mutex);

    // the audio that piled up while the device did not run is old
    for (const std::unique_ptr<modemcellular::CarrierQueue>& q : m_queues) {
        q->reset();
    }
}

void CellularTx::stop()
{
    qDebug("CellularTx::stop");
}

// in the thread of the device
void CellularTx::pull(SampleVector::iterator& begin, unsigned int nbSamples)
{
    QMutexLocker mutexLocker(&m_mutex);
    SampleVector::iterator it = begin;

    if (!m_transmitter)
    {
        for (unsigned int i = 0; i < nbSamples; i++, ++it) {
            *it = Sample(0, 0);
        }
        return;
    }

    m_buffer.resize(nbSamples);
    m_transmitter->pull(m_buffer.data(), nbSamples);
    for (unsigned int i = 0; i < nbSamples; i++, ++it)
    {
        it->m_real = static_cast<FixReal>(m_buffer[i].real() * SDR_TX_SCALEF);
        it->m_imag = static_cast<FixReal>(m_buffer[i].imag() * SDR_TX_SCALEF);
    }
}

// under the lock
void CellularTx::makeTransmitter()
{
    m_transmitter.reset();
    if (!modemcellular::CarrierRadio::rateSupported(m_basebandSampleRate)) {
        return;
    }
    m_transmitter = std::make_unique<modemcellular::CarrierTransmitter>(m_basebandSampleRate);
    m_transmitter->setCarriers(m_carriers);
    m_transmitter->setAudioSource([this](size_t index, float* audio, size_t count) {
        if (index < m_queues.size())
        {
            const double ratio = m_queues[index]->ratio();
            m_queues[index]->read(audio, count, ratio);
            if (index == 0)
            {
                m_audioRead += count;
                m_lastRatio = ratio;
            }
        }
    });
}

void CellularTx::setCarriers(const std::vector<modemcellular::CarrierTuning>& carriers, uint32_t lead)
{
    QMutexLocker mutexLocker(&m_mutex);

    m_carriers = carriers;
    m_lead = lead;
    m_queues.clear();
    for (size_t i = 0; i < carriers.size(); i++) {
        m_queues.push_back(std::make_unique<modemcellular::CarrierQueue>(lead));
    }
    if (m_transmitter) {
        m_transmitter->setCarriers(carriers);
    }
}

void CellularTx::writeBatch(const modemcellular::CarrierBatch& batch)
{
    QMutexLocker mutexLocker(&m_mutex);

    m_batches++;
    if (!m_carriers.empty() && batch.find(m_carriers[0].id) >= 0) {
        m_audioWritten += static_cast<uint64_t>(batch.length);
    }
    for (size_t i = 0; i < m_carriers.size(); i++)
    {
        const int at = batch.find(m_carriers[i].id);

        if (at < 0) {
            continue;
        }
        modemcellular::CarrierQueue& q = *m_queues[i];
        q.write(batch.carrier(static_cast<size_t>(at)), static_cast<size_t>(batch.length));
        if (q.fill() > m_lead + kOverrun)
        {
            q.reset();
            m_resets++;
        }
    }
}

CellularTx::Status CellularTx::status() const
{
    QMutexLocker mutexLocker(&m_mutex);
    Status s;

    s.sampleRate = m_basebandSampleRate;
    s.supported = modemcellular::CarrierRadio::rateSupported(m_basebandSampleRate);
    s.lead = m_lead;
    s.batches = m_batches;
    s.resets = m_resets;
    s.carriers = m_carriers;
    for (const std::unique_ptr<modemcellular::CarrierQueue>& q : m_queues)
    {
        s.fill.push_back(q->fill());
        s.underruns.push_back(q->underruns());
    }
    return s;
}

bool CellularTx::handleMessage(const Message& cmd)
{
    if (DSPSignalNotification::match(cmd))
    {
        const DSPSignalNotification& notif = (const DSPSignalNotification&) cmd;

        m_centerFrequency = notif.getCenterFrequency();
        if (notif.getSampleRate() != m_basebandSampleRate || !m_transmitter)
        {
            QMutexLocker mutexLocker(&m_mutex);
            m_basebandSampleRate = notif.getSampleRate();
            makeTransmitter();
        }
        qDebug() << "CellularTx::handleMessage: DSPSignalNotification: rate:" << m_basebandSampleRate
            << "centre:" << m_centerFrequency;
        sendSpan();
        return true;
    }
    else if (MainCore::MsgPacket::match(cmd))
    {
        const MainCore::MsgPacket& packet = (const MainCore::MsgPacket&) cmd;
        const QByteArray bytes = packet.getPacket();
        const uint8_t *data = reinterpret_cast<const uint8_t*>(bytes.constData());
        const size_t size = static_cast<size_t>(bytes.size());

        switch (modemcellular::wireKind(data, size))
        {
        case modemcellular::WireKind::Batch:
        {
            modemcellular::CarrierBatch batch;

            if (modemcellular::decodeBatch(data, size, batch)) {
                writeBatch(batch);
            }
            return true;
        }
        case modemcellular::WireKind::SpanRequest:
            sendSpan();
            return true;
        case modemcellular::WireKind::Tuning:
        {
            modemcellular::WireTuning tuning;

            if (modemcellular::decodeTuning(data, size, tuning)) {
                setCarriers(tuning.carriers, tuning.lead);
            }
            return true;
        }
        case modemcellular::WireKind::None:
        case modemcellular::WireKind::Span:
        case modemcellular::WireKind::TxStatus:
            break;
        }
        return false;
    }

    return false;
}

void CellularTx::sendToController(const std::vector<uint8_t>& wire)
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "cellular", pipes);
    const QByteArray bytes(reinterpret_cast<const char*>(wire.data()), static_cast<int>(wire.size()));

    for (ObjectPipe *pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);

        if (messageQueue) {
            messageQueue->push(MainCore::MsgPacket::create(this, bytes));
        }
    }
}

void CellularTx::sendSpan()
{
    modemcellular::WireSpan span;

    span.centre = static_cast<double>(m_centerFrequency);
    span.sampleRate = static_cast<double>(m_basebandSampleRate);
    sendToController(modemcellular::encodeSpan(span));
}

void CellularTx::sendStatus()
{
    const Status s = status();

    if (s.carriers.empty()) {
        return;
    }
    if (++m_statusCount % 10 == 0)
    {
        QMutexLocker mutexLocker(&m_mutex);
        // the rates of both sides: audio written by the network against audio read by the device
        qInfo("CellularTx: written %llu, read %llu (%+.0f ppm), ratio %+.0f ppm, fill %zu, empty %llu",
            static_cast<unsigned long long>(m_audioWritten), static_cast<unsigned long long>(m_audioRead),
            m_audioWritten ? (static_cast<double>(m_audioRead) / static_cast<double>(m_audioWritten) - 1.0) * 1e6 : 0.0,
            (m_lastRatio - 1.0) * 1e6, s.fill.empty() ? size_t(0) : s.fill[0],
            static_cast<unsigned long long>(s.underruns.empty() ? 0 : s.underruns[0]));
    }
    modemcellular::WireTxStatus wire;
    for (size_t i = 0; i < s.carriers.size(); i++)
    {
        wire.carriers.push_back(s.carriers[i].id);
        wire.fill.push_back(static_cast<uint32_t>(s.fill[i]));
        wire.underruns.push_back(s.underruns[i]);
    }
    sendToController(modemcellular::encodeTxStatus(wire));
}
