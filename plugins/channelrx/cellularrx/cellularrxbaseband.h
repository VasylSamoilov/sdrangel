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

#ifndef INCLUDE_CELLULARRXBASEBAND_H
#define INCLUDE_CELLULARRXBASEBAND_H

#include <complex>
#include <memory>
#include <vector>

#include <QMutex>
#include <QObject>

#include "dsp/samplesinkfifo.h"

#include "carrierradio.h"

class ChannelAPI;

// The receiver of all carriers of the stream, in its own thread: the samples of the device go
// through a FIFO, the carriers are demodulated, and every block of 10 ms goes as one batch to the
// consumers of the "cellular" pipe of the channel (the controller).
class CellularRxBaseband : public QObject
{
    Q_OBJECT
public:
    struct Status
    {
        double sampleRate = 0.0;
        bool supported = false;
        uint64_t samples = 0;           // audio samples per carrier handed out
        uint64_t batches = 0;
        std::vector<modemcellular::CarrierTuning> carriers;
        std::vector<float> levels;      // dB of full scale, last block
    };

    explicit CellularRxBaseband(ChannelAPI *channel);
    ~CellularRxBaseband() override;

    void reset();
    void startWork();
    void stopWork();
    bool isRunning() const { return m_running; }
    void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end);

    // from the thread of the channel
    void setSampleRate(int sampleRate);
    void setCarriers(const std::vector<modemcellular::CarrierTuning>& carriers);
    Status status() const;

private slots:
    void handleData();

private:
    void sendBatch(const modemcellular::CarrierBatch& batch);
    void makeReceiver();
    void publishStatus();

    ChannelAPI *m_channel;
    SampleSinkFifo m_sampleFifo;
    std::unique_ptr<modemcellular::CarrierReceiver> m_receiver;
    std::vector<modemcellular::CarrierTuning> m_carriers;
    std::vector<std::complex<float>> m_buffer;
    std::vector<float> m_levels;
    int m_sampleRate;
    uint64_t m_batches;
    bool m_running;
    mutable QMutex m_mutex;
    // what status() returns: published under the lock above, read under its own lock, so that
    // the GUI does not wait for the demodulation of a whole FIFO
    Status m_status;
    mutable QMutex m_statusMutex;
};

#endif // INCLUDE_CELLULARRXBASEBAND_H
