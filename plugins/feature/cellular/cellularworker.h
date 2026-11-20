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
#include <QString>
#include <QStringList>

#include "util/message.h"
#include "util/messagequeue.h"

#include "carrierbatch.h"
#include "carrierwire.h"
#include "cellularsystem.h"
#include "nmtbasestation.h"
#include "nmtcellularbridge.h"

class ChannelAPI;

// The networks in the thread of the worker. Each network is one base station with its own
// subscribers, calls and event log, on one receiving and one transmitting radio (a Cellular Rx and
// a Cellular Tx channel); networks may share radios. The worker asks each radio for its span; when
// both radios of a network are known it binds the carriers of the network to them, and each radio
// is given the carriers of all networks on it. Every batch of a receiver runs the networks on it
// one block on (the received audio in, the clock on, the timers, the calls, the audio out), and
// the audio for each transmitter goes to it as one batch. All state of the networks lives here and
// is touched by this thread only.
class CellularWorker : public QObject
{
    Q_OBJECT
public:
    class MsgAddNetwork : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        int m_id = 0;
        QString m_name;
        modemnmt::NmtNetworkConfig m_config;
        int m_fallback = 0;
        ChannelAPI *m_rxChannel = nullptr;
        MessageQueue *m_rxPipe = nullptr;       // what the receiver sends us
        ChannelAPI *m_txChannel = nullptr;
        MessageQueue *m_txPipe = nullptr;       // what the transmitter sends us
        static MsgAddNetwork* create() { return new MsgAddNetwork(); }

    private:
        MsgAddNetwork() : Message() { }
    };

    // reports: the input queue of the feature; producer: the controller as the radios see it (the
    // feature, which outlives the worker): the source of the packets and the producer of the
    // audio pipes; lead: samples of lead of the transmitters
    CellularWorker(MessageQueue *reports, const QObject *producer, uint32_t lead);
    ~CellularWorker() override;
    MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; }

public slots:
    // blocking calls from the feature: after them the worker does not use the radios of the
    // network (of all networks) any more
    void removeNetwork(int id);
    void removeAll();
    // blocking call from the feature: the channel is being removed, nothing goes to it any more
    void radioGone(qulonglong channel);

private slots:
    void handleInputMessages();

private:
    struct Radio
    {
        ChannelAPI *channel = nullptr;
        MessageQueue *pipe = nullptr;
        bool tx = false;
        bool haveSpan = false;
        modemcellular::WireSpan span;
        modemcellular::WireTxStatus txStatus;
    };

    struct Network
    {
        int id = 0;
        QString name;
        modemnmt::NmtNetworkConfig config;
        Radio *rx = nullptr;
        Radio *tx = nullptr;
        std::unique_ptr<modemnmt::NmtBaseStation> station;
        std::unique_ptr<modemcellular::CellularSystem> cellular;
        std::unique_ptr<modemnmt::NmtCellularBridge> bridge;
        modemcellular::WireSpan boundRx;
        modemcellular::WireSpan boundTx;
        modemcellular::CarrierBatch txBatch;
        std::vector<float> levels;              // per carrier on air
        struct Trace
        {
            std::string last;                   // the last frame to the phone that was logged
            int repeats = 0;                    // times it was sent
        };
        std::vector<Trace> traces;              // per carrier on air
        uint64_t batches = 0;
        uint64_t gaps = 0;                      // batches of the receiver that did not arrive
        uint64_t nextSample = 0;
    };

    void addNetwork(const MsgAddNetwork& add);
    void remove(int id, bool retryOthers);
    Radio *radioFor(ChannelAPI *channel, MessageQueue *pipe, bool tx);
    void dropUnusedRadios();
    void handlePipe(Radio *radio);
    void handlePacket(Radio& radio, const QByteArray& bytes);
    void tryBind(Network& network);
    void unbind(Network& network);
    bool running(const Network& network) const;
    QString conflict(const Network& network) const;
    QStringList neighbours(const Network& network) const;
    void updateTuning(Radio& radio);
    void processBatch(Radio& radio);
    void reportStatus(const Network& network);
    void sendTo(ChannelAPI *channel, const std::vector<uint8_t>& wire);
    void sendAudio(ChannelAPI *channel, const std::vector<uint8_t>& wire);
    void log(int network, const QString& text);
    // the frames of a carrier into the event log of its network
    void traceFrame(Network& n, size_t index, bool toPhone, const modemnmt::Frame& frame, double quality, double level);
    void flushTrace(Network& n, size_t index);

    MessageQueue m_inputMessageQueue;
    MessageQueue *m_reports;
    const QObject *m_producer;
    uint32_t m_lead;
    bool m_direct;                              // in a blocking call of the feature
    std::map<ChannelAPI*, std::unique_ptr<Radio>> m_radios;
    std::vector<std::unique_ptr<Network>> m_networks;
    modemcellular::CarrierBatch m_rx;
    std::map<Radio*, modemcellular::CarrierBatch> m_txOut;
};

#endif // INCLUDE_FEATURE_CELLULARWORKER_H_
