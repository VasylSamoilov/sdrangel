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

#include <deque>
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
#include "dsp/dv/dvdtmf.h"
#include "nmtphoneinfo.h"
#include "nmttracefold.h"
#include "testcaller.h"
#include "cellular.h"

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

    // the SMS centre of each phone the book knows one of, by system and number
    class MsgSmsCentres : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        std::map<std::pair<int, std::string>, std::string> m_centres;
        static MsgSmsCentres* create() { return new MsgSmsCentres(); }

    private:
        MsgSmsCentres() : Message() { }
    };

    // the names of the networks by id: a rename does not start a network again
    class MsgNames : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        std::map<int, QString> m_names;
        static MsgNames* create() { return new MsgNames(); }

    private:
        MsgNames() : Message() { }
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
        // the trace of a channel: each direction folds its own runs of the same frame
        struct Trace
        {
            modemnmt::NmtTraceFold out;         // to the phone
            modemnmt::NmtTraceFold in;          // from the phone
            QString outType;                    // frame type of the run, for its count
            QString inType;
            qint64 outAt = 0;                   // ms of the last frame of the run
            qint64 inAt = 0;
        };
        std::vector<Trace> traces;              // per carrier on air
        // the phones heard: their identity in a frame for the channel it came on
        std::map<std::string, Cellular::MsgReportStatus::Phone> phones;
        std::unique_ptr<modemcellular::TestCaller> caller;     // calls of the tester to a phone
        bool music = false;                     // the far end of calls to no phone: music, else echo
        // message waiting indicators the protocol could not send yet (the phone was busy), by number
        struct PendingMwi
        {
            uint8_t flags = 0;
            modemnmt::MwiWhen when = modemnmt::MwiWhen::InCallAndClearing;
            QDateTime since;
        };
        std::map<std::string, PendingMwi> pendingMwi;
        // per carrier on air: the call its speech was last reported for, and whether it was on
        struct SpeechSeen
        {
            uint64_t call = 0;
            bool live = false;
        };
        std::vector<SpeechSeen> speechSeen;
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
    void sendNames(Radio& radio);
    void rename(const MsgNames& msg);
    void processBatch(Radio& radio);
    void reportStatus(const Network& network);
    // the speech of the calls (live ones, and one that ended since the last time); logLine: also
    // a line per live call in the event log (its last 2 s)
    void reportSpeech(Network& network, bool logLine);
    void sendTo(ChannelAPI *channel, const std::vector<uint8_t>& wire);
    void sendAudio(ChannelAPI *channel, const std::vector<uint8_t>& wire);
    void log(int network, const QString& text, Cellular::MsgReportLog::Kind kind = Cellular::MsgReportLog::Event);
    void sendPostponed(Network& network);
    // the frames of a carrier into the event log of its network
    void traceFrame(Network& n, size_t index, bool toPhone, const modemnmt::Frame& frame, double quality, double level);
    // the count of a run of the same frame; `quiet`: only runs with no frame for a while
    void flushTrace(Network& n, size_t index, bool toPhone, bool quiet = false);
    void flushQuietTraces(Network& n);
    // a phone in a frame of a carrier, if the frame was for that carrier and carries an identity
    void notePhone(Network& n, size_t index, const modemnmt::Frame& frame, double quality);
    // what the phones show of themselves, by number; kept while the worker runs, over restarts of networks
    modemnmt::NmtPhoneInfo& known(const std::string& number) { return m_known[number]; }
    static void trafficArea(const Network& n, int& y1, int& area);
    void action(const Cellular::MsgAction& action);
    void setHandlers(Network& n);

    MessageQueue m_inputMessageQueue;
    MessageQueue *m_reports;
    const QObject *m_producer;
    uint32_t m_lead;
    bool m_direct;                              // in a blocking call of the feature
    std::map<ChannelAPI*, std::unique_ptr<Radio>> m_radios;
    std::vector<std::unique_ptr<Network>> m_networks;
    std::map<std::string, modemnmt::NmtPhoneInfo> m_known;
    std::map<std::pair<int, std::string>, std::string> m_smsCentres;   // of the phones, from the book
    // touch tones in the speech of the phones (DTMF), by number: the keys of a group are logged
    // together when the phone sent no key for a while
    struct ToneListener
    {
        std::unique_ptr<DVDTMF::Detector> detector;
        std::string keys;
        int network = 0;
        qint64 lastMs = 0;
    };
    std::map<std::string, ToneListener> m_tones;
    void flushTones(bool all);
    // short messages the centre keeps for a phone (by its number) until the phone fetches them
    std::map<std::string, std::deque<modemnmt::SmsDeliver>> m_held;
    modemcellular::CarrierBatch m_rx;
    std::map<Radio*, modemcellular::CarrierBatch> m_txOut;
};

#endif // INCLUDE_FEATURE_CELLULARWORKER_H_
