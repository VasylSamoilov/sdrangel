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

#ifndef INCLUDE_CELLULARTX_H
#define INCLUDE_CELLULARTX_H

#include <complex>
#include <memory>
#include <vector>

#include <QMutex>
#include <QThread>
#include <QTimer>

#include "channel/channelapi.h"
#include "dsp/basebandsamplesource.h"
#include "util/message.h"

#include "carrierqueue.h"
#include "carrierradio.h"

#include "cellulartxsettings.h"

class DeviceAPI;
class ObjectPipe;
class CellularTxAudio;

// The transmitter of the cellular base stations: one per transmitting device, all the carriers of
// the networks whose downlinks are in the span of the device. The carriers come from the controller
// (the Cellular feature) through the input queue; the audio comes through the "cellulartx" message
// pipe from the controller to the channel, drained in the thread of the channel. Each carrier has
// a queue that holds the lead of the transmitter over the receiver and follows the drift between
// both devices (CarrierQueue); the span of the stream and the state of the queues go to the
// controller through the "cellular" message pipe.
class CellularTx : public BasebandSampleSource, public ChannelAPI
{
public:
    struct Status
    {
        double sampleRate = 0.0;
        bool supported = false;
        uint32_t lead = 0;
        uint64_t batches = 0;
        uint64_t resets = 0;            // queues that ran over (the transmitter did not take the audio)
        std::vector<modemcellular::CarrierTuning> carriers;
        std::vector<size_t> fill;
        std::vector<uint64_t> underruns;
    };

    CellularTx(DeviceAPI *deviceAPI);
    ~CellularTx() override;
    void destroy() override { delete this; }
    void setDeviceAPI(DeviceAPI *deviceAPI) override;
    DeviceAPI *getDeviceAPI() override { return m_deviceAPI; }

    void start() override;
    void stop() override;
    void pull(SampleVector::iterator& begin, unsigned int nbSamples) override;
    void pushMessage(Message *msg) override { m_inputMessageQueue.push(msg); }
    QString getSourceName() override { return objectName(); }

    void getIdentifier(QString& id) override { id = objectName(); }
    QString getIdentifier() const override { return objectName(); }
    const QString& getURI() const { return getName(); }
    void getTitle(QString& title) override { title = m_settings.m_title; }
    qint64 getCenterFrequency() const override { return 0; }
    void setCenterFrequency(qint64) override {}

    QByteArray serialize() const override { return m_settings.serialize(); }
    bool deserialize(const QByteArray& data) override { return m_settings.deserialize(data); }

    int getNbSinkStreams() const override { return 0; }
    int getNbSourceStreams() const override { return 1; }
    int getStreamIndex() const override { return m_settings.m_streamIndex; }
    qint64 getStreamCenterFrequency(int, bool) const override { return m_centerFrequency; }

    Status status() const;
    qint64 getDeviceCenterFrequency() const { return m_centerFrequency; }
    // audio of the controller into the queues (thread of the audio, see CellularTxAudio)
    void writeBatch(const modemcellular::CarrierBatch& batch);

    static const char * const m_channelIdURI;
    static const char * const m_channelId;

private:
    DeviceAPI *m_deviceAPI;
    CellularTxSettings m_settings;
    int m_basebandSampleRate;
    qint64 m_centerFrequency;
    QThread m_thread;
    CellularTxAudio *m_audio;
    ObjectPipe *m_audioPipe;                    // from the controller, registered by us
    const QObject *m_audioProducer;

    mutable QMutex m_mutex;
    std::unique_ptr<modemcellular::CarrierTransmitter> m_transmitter;
    std::vector<modemcellular::CarrierTuning> m_carriers;
    std::vector<std::unique_ptr<modemcellular::CarrierQueue>> m_queues;   // parallel to m_carriers
    uint32_t m_lead;
    uint64_t m_batches;
    uint64_t m_resets;
    uint64_t m_audioWritten;        // audio samples of the first carrier from the network
    uint64_t m_audioRead;           // and taken by the device (one per output sample at 48 kHz)
    double m_lastRatio;
    unsigned int m_statusCount;
    std::vector<std::complex<float>> m_buffer;
    QTimer m_statusTimer;

    bool handleMessage(const Message& cmd) override;
    void makeTransmitter();
    void setCarriers(const std::vector<modemcellular::CarrierTuning>& carriers, uint32_t lead);
    void useAudioPipe(const QObject *producer);
    void dropAudioPipe();
    void sendToController(const std::vector<uint8_t>& wire);
    void sendSpan();
    void sendStatus();
};

#endif // INCLUDE_CELLULARTX_H
