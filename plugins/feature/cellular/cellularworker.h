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

#ifndef INCLUDE_FEATURE_CELLULARWORKER_H_
#define INCLUDE_FEATURE_CELLULARWORKER_H_

#include <map>
#include <memory>
#include <vector>

#include <QObject>

#include "util/message.h"
#include "util/messagequeue.h"

#include "carrierbatch.h"
#include "carrierwire.h"
#include "cellularsystem.h"
#include "nmtbasestation.h"
#include "nmtcellularbridge.h"

class ChannelAPI;

// The network in the thread of the worker. It asks both radios for their spans; when both are
// known it binds the carriers to them and gives each radio its carriers. Then every batch of the
// receiver runs the network one block on: the received audio in, the clock on, the timers, the
// calls (router and bridge), the audio for the transmitter out. All state of the network lives
// here and is touched by this thread only.
class CellularWorker : public QObject
{
    Q_OBJECT
public:
    class MsgStart : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        modemnmt::NmtNetworkConfig m_config;
        ChannelAPI *m_rxChannel = nullptr;
        ChannelAPI *m_txChannel = nullptr;
        MessageQueue *m_rxPipe = nullptr;       // what the receiver sends us
        MessageQueue *m_txPipe = nullptr;       // what the transmitter sends us
        MessageQueue *m_reports = nullptr;      // the input queue of the feature
        uint32_t m_lead = 4800;                 // samples
        int m_fallback = 0;
        static MsgStart* create() { return new MsgStart(); }

    private:
        MsgStart() : Message() { }
    };

    CellularWorker();
    ~CellularWorker() override;
    MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; }

public slots:
    // stops the network and takes the carriers off both radios (blocking call from the feature)
    void stopWork();

private slots:
    void handleInputMessages();

private:
    void startWork(const MsgStart& start);
    void handlePipe(MessageQueue *queue, bool fromTx);
    void handlePacket(const QByteArray& bytes, bool fromTx);
    void tryBind();
    void processBatch();
    void reportStatus();
    void sendTo(ChannelAPI *channel, const std::vector<uint8_t>& wire);
    void unbind();
    void log(const QString& text);

    MessageQueue m_inputMessageQueue;
    modemnmt::NmtNetworkConfig m_config;
    ChannelAPI *m_rxChannel;
    ChannelAPI *m_txChannel;
    MessageQueue *m_rxPipe;
    MessageQueue *m_txPipe;
    MessageQueue *m_reports;
    uint32_t m_lead;
    int m_fallback;
    bool m_running;

    modemcellular::WireSpan m_rxSpan;
    modemcellular::WireSpan m_txSpan;
    bool m_haveRxSpan;
    bool m_haveTxSpan;
    modemcellular::WireSpan m_boundRx;
    modemcellular::WireSpan m_boundTx;

    std::unique_ptr<modemnmt::NmtBaseStation> m_station;
    std::unique_ptr<modemcellular::CellularSystem> m_cellular;
    std::unique_ptr<modemnmt::NmtCellularBridge> m_bridge;

    modemcellular::CarrierBatch m_rx;
    modemcellular::CarrierBatch m_tx;
    std::vector<float> m_levels;                // per carrier on air
    modemcellular::WireTxStatus m_txStatus;
    uint64_t m_batches;
    uint64_t m_gaps;                            // batches of the receiver that did not arrive
    uint64_t m_nextSample;
};

#endif // INCLUDE_FEATURE_CELLULARWORKER_H_
