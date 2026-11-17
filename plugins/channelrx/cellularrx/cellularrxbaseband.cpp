///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2019 Edouard Griffiths, F4EXB                                   //
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

#include <QByteArray>
#include <QDebug>

#include "channel/channelapi.h"
#include "maincore.h"
#include "pipes/messagepipes.h"
#include "util/messagequeue.h"

#include "carrierwire.h"

#include "cellularrxbaseband.h"

CellularRxBaseband::CellularRxBaseband(ChannelAPI *channel) :
    m_channel(channel),
    m_sampleRate(0),
    m_batches(0),
    m_running(false)
{
    m_sampleFifo.setSize(SampleSinkFifo::getSizePolicy(960000));
}

CellularRxBaseband::~CellularRxBaseband()
{
}

void CellularRxBaseband::reset()
{
    QMutexLocker mutexLocker(&m_mutex);
    m_sampleFifo.reset();
}

void CellularRxBaseband::startWork()
{
    QMutexLocker mutexLocker(&m_mutex);
    QObject::connect(
        &m_sampleFifo,
        &SampleSinkFifo::dataReady,
        this,
        &CellularRxBaseband::handleData,
        Qt::QueuedConnection
    );
    m_running = true;
}

void CellularRxBaseband::stopWork()
{
    QMutexLocker mutexLocker(&m_mutex);
    QObject::disconnect(
        &m_sampleFifo,
        &SampleSinkFifo::dataReady,
        this,
        &CellularRxBaseband::handleData
    );
    m_running = false;
}

void CellularRxBaseband::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
{
    m_sampleFifo.write(begin, end);
}

void CellularRxBaseband::setSampleRate(int sampleRate)
{
    QMutexLocker mutexLocker(&m_mutex);

    if (sampleRate == m_sampleRate && m_receiver) {
        return;
    }
    m_sampleRate = sampleRate;
    if (sampleRate > 0) {
        m_sampleFifo.setSize(SampleSinkFifo::getSizePolicy(sampleRate));
    }
    makeReceiver();
}

void CellularRxBaseband::setCarriers(const std::vector<modemcellular::CarrierTuning>& carriers)
{
    QMutexLocker mutexLocker(&m_mutex);

    m_carriers = carriers;
    m_levels.assign(carriers.size(), -100.0f);
    if (m_receiver) {
        m_receiver->setCarriers(carriers);
    }
    publishStatus();
}

// under the lock
void CellularRxBaseband::makeReceiver()
{
    m_receiver.reset();
    if (modemcellular::CarrierRadio::rateSupported(m_sampleRate))
    {
        m_receiver = std::make_unique<modemcellular::CarrierReceiver>(m_sampleRate);
        m_receiver->setCarriers(m_carriers);
        m_receiver->setBatchHandler([this](const modemcellular::CarrierBatch& batch) { sendBatch(batch); });
    }
    publishStatus();
}

// under the lock
void CellularRxBaseband::publishStatus()
{
    QMutexLocker statusLocker(&m_statusMutex);

    m_status.sampleRate = m_sampleRate;
    m_status.supported = modemcellular::CarrierRadio::rateSupported(m_sampleRate);
    m_status.samples = m_receiver ? m_receiver->samples() : 0;
    m_status.batches = m_batches;
    m_status.carriers = m_carriers;
    m_status.levels = m_levels;
}

CellularRxBaseband::Status CellularRxBaseband::status() const
{
    QMutexLocker statusLocker(&m_statusMutex);

    return m_status;
}

// under the lock, in the thread of the baseband
void CellularRxBaseband::sendBatch(const modemcellular::CarrierBatch& batch)
{
    m_batches++;
    if (batch.levels.size() == m_levels.size()) {
        m_levels = batch.levels;
    }
    publishStatus();

    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(m_channel, "cellular", pipes);
    if (pipes.isEmpty()) {
        return;
    }

    const std::vector<uint8_t> wire = modemcellular::encodeBatch(batch);
    const QByteArray packet(reinterpret_cast<const char*>(wire.data()), static_cast<int>(wire.size()));

    for (ObjectPipe *pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);

        if (messageQueue) {
            messageQueue->push(MainCore::MsgPacket::create(m_channel, packet));
        }
    }
}

void CellularRxBaseband::handleData()
{
    QMutexLocker mutexLocker(&m_mutex);

    while (m_sampleFifo.fill() > 0)
    {
        SampleVector::iterator part1begin;
        SampleVector::iterator part1end;
        SampleVector::iterator part2begin;
        SampleVector::iterator part2end;

        const std::size_t count = m_sampleFifo.readBegin(m_sampleFifo.fill(), &part1begin, &part1end, &part2begin, &part2end);

        for (int part = 0; part < 2; part++)
        {
            const SampleVector::iterator begin = part == 0 ? part1begin : part2begin;
            const SampleVector::iterator end = part == 0 ? part1end : part2end;

            if (begin == end || !m_receiver) {
                continue;
            }
            m_buffer.resize(static_cast<size_t>(end - begin));
            size_t i = 0;
            for (SampleVector::iterator it = begin; it != end; ++it, ++i)
            {
                m_buffer[i] = std::complex<float>(
                    static_cast<float>(it->m_real) / SDR_RX_SCALEF,
                    static_cast<float>(it->m_imag) / SDR_RX_SCALEF);
            }
            m_receiver->feed(m_buffer.data(), m_buffer.size());
        }

        m_sampleFifo.readCommit(static_cast<unsigned int>(count));
    }
}
