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

#include <QDateTime>
#include <QDebug>

#include "device/deviceapi.h"
#include "dsp/dspcommands.h"
#include "maincore.h"
#include "pipes/messagepipes.h"

#include "carrierwire.h"

#include "cellularrx.h"

const char * const CellularRx::m_channelIdURI = "sdrangel.channel.cellularrx";
const char * const CellularRx::m_channelId = "CellularRx";

CellularRx::CellularRx(DeviceAPI *deviceAPI) :
    ChannelAPI(m_channelIdURI, ChannelAPI::StreamSingleSink),
    m_deviceAPI(deviceAPI),
    m_basebandSampleRate(0),
    m_centerFrequency(0)
{
    setObjectName(m_channelId);
    m_thread.setObjectName("CellularRx");

    m_basebandSink = new CellularRxBaseband(this);
    m_basebandSink->moveToThread(&m_thread);

    m_deviceAPI->addChannelSink(this);
    m_deviceAPI->addChannelSinkAPI(this);
}

CellularRx::~CellularRx()
{
    m_deviceAPI->removeChannelSinkAPI(this);
    m_deviceAPI->removeChannelSink(this, true);

    if (m_basebandSink->isRunning()) {
        stop();
    }

    delete m_basebandSink;
}

void CellularRx::setDeviceAPI(DeviceAPI *deviceAPI)
{
    if (deviceAPI != m_deviceAPI)
    {
        m_deviceAPI->removeChannelSinkAPI(this);
        m_deviceAPI->removeChannelSink(this, false);
        m_deviceAPI = deviceAPI;
        m_deviceAPI->addChannelSink(this);
        m_deviceAPI->addChannelSinkAPI(this);
    }
}

void CellularRx::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool firstOfBurst)
{
    (void) firstOfBurst;
    m_basebandSink->feed(begin, end);
}

void CellularRx::start()
{
    qDebug("CellularRx::start");
    m_basebandSink->reset();
    m_basebandSink->startWork();
    m_thread.start();
    m_basebandSink->setSampleRate(m_basebandSampleRate);
    sendSpan();
}

void CellularRx::stop()
{
    qDebug("CellularRx::stop");
    m_basebandSink->stopWork();
    m_thread.quit();
    m_thread.wait();
}

bool CellularRx::handleMessage(const Message& cmd)
{
    if (DSPSignalNotification::match(cmd))
    {
        const DSPSignalNotification& notif = (const DSPSignalNotification&) cmd;

        m_basebandSampleRate = notif.getSampleRate();
        m_centerFrequency = notif.getCenterFrequency();
        qDebug() << "CellularRx::handleMessage: DSPSignalNotification: rate:" << m_basebandSampleRate
            << "centre:" << m_centerFrequency;
        m_basebandSink->setSampleRate(m_basebandSampleRate);
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
        case modemcellular::WireKind::SpanRequest:
            sendSpan();
            return true;
        case modemcellular::WireKind::Tuning:
        {
            modemcellular::WireTuning tuning;

            if (modemcellular::decodeTuning(data, size, tuning)) {
                m_basebandSink->setCarriers(tuning.carriers);
            }
            return true;
        }
        case modemcellular::WireKind::None:
        case modemcellular::WireKind::Batch:
        case modemcellular::WireKind::Span:
        case modemcellular::WireKind::Names:
        {
            modemcellular::WireNames names;

            if (modemcellular::decodeNames(data, size, names))
            {
                QMutexLocker locker(&m_namesMutex);
                m_networkNames.clear();
                for (size_t i = 0; i < names.networks.size(); i++) {
                    m_networkNames[names.networks[i]] = QString::fromStdString(names.names[i]);
                }
            }
            return true;
        }
        case modemcellular::WireKind::Speech:
        {
            modemcellular::WireSpeech speech;

            if (modemcellular::decodeSpeech(data, size, speech))
            {
                QMutexLocker locker(&m_namesMutex);
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                for (const modemcellular::WireSpeech::Carrier& c : speech.carriers)
                {
                    m_speech[c.carrier] = c;
                    m_speechAt[c.carrier] = now;
                }
            }
            return true;
        }
        case modemcellular::WireKind::TxStatus:
            break;
        }
        return false;
    }

    return false;
}

std::map<int, QString> CellularRx::networkNames() const
{
    QMutexLocker locker(&m_namesMutex);

    return m_networkNames;
}

std::map<int, modemcellular::WireSpeech::Carrier> CellularRx::speech() const
{
    QMutexLocker locker(&m_namesMutex);
    std::map<int, modemcellular::WireSpeech::Carrier> speech = m_speech;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // a live call whose reports stopped 2 s ago (the network went off) is shown as ended
    for (auto& s : speech)
    {
        const auto at = m_speechAt.find(s.first);
        if (s.second.live && (at == m_speechAt.end() || now - at->second > 2000)) {
            s.second.live = false;
        }
    }
    return speech;
}

// the span of the stream to the controller
void CellularRx::sendSpan()
{
    QList<ObjectPipe*> pipes;
    MainCore::instance()->getMessagePipes().getMessagePipes(this, "cellular", pipes);

    modemcellular::WireSpan span;
    span.centre = static_cast<double>(m_centerFrequency);
    span.sampleRate = static_cast<double>(m_basebandSampleRate);
    const std::vector<uint8_t> wire = modemcellular::encodeSpan(span);
    const QByteArray bytes(reinterpret_cast<const char*>(wire.data()), static_cast<int>(wire.size()));

    for (ObjectPipe *pipe : pipes)
    {
        MessageQueue *messageQueue = qobject_cast<MessageQueue*>(pipe->m_element);

        if (messageQueue) {
            messageQueue->push(MainCore::MsgPacket::create(this, bytes));
        }
    }
}
