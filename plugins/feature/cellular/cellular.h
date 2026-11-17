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

#ifndef INCLUDE_FEATURE_CELLULAR_H_
#define INCLUDE_FEATURE_CELLULAR_H_

#include <map>
#include <memory>

#include <QMutex>
#include <QStringList>
#include <QThread>

#include "feature/feature.h"
#include "util/message.h"

#include "cellularsettings.h"

class WebAPIAdapterInterface;
class ChannelAPI;
class ObjectPipe;
class CellularWorker;

// The controller of the cellular base stations: it holds the networks and all their protocol
// state. For each network it finds the Cellular Rx channel of its receiving device set and the
// Cellular Tx channel of its transmitting one; the networks run in a thread of their own (the
// worker) on the batches of the receivers. While the controller runs, a network that is switched
// on starts and one that is switched off stops, the others go on. A network that runs keeps the
// settings it started with.
class Cellular : public Feature
{
    Q_OBJECT
public:
    class MsgConfigureCellular : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const CellularSettings& getSettings() const { return m_settings; }
        // which running networks start again with the new settings: none (the GUI shows them
        // as stale), the ones whose settings changed (a PATCH), all of them (a PUT)
        enum class Restart { None, Changed, All };
        Restart getRestart() const { return m_restart; }
        static MsgConfigureCellular* create(const CellularSettings& settings, Restart restart = Restart::None) {
            return new MsgConfigureCellular(settings, restart);
        }

    private:
        CellularSettings m_settings;
        Restart m_restart;
        MsgConfigureCellular(const CellularSettings& settings, Restart restart) :
            Message(), m_settings(settings), m_restart(restart) { }
    };

    class MsgStartStop : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        bool getStartStop() const { return m_startStop; }
        static MsgStartStop* create(bool startStop) { return new MsgStartStop(startStop); }

    private:
        bool m_startStop;
        explicit MsgStartStop(bool startStop) : Message(), m_startStop(startStop) { }
    };

    // from the worker: a packet for a radio channel, delivered by the feature in its thread (the
    // thread that removes channels) and only to a channel that is still there
    class MsgToRadio : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        ChannelAPI *getChannel() const { return m_channel; }
        const QObject *getSource() const { return m_source; }
        const QByteArray& getPacket() const { return m_packet; }
        static MsgToRadio* create(ChannelAPI *channel, const QObject *source, const QByteArray& packet) {
            return new MsgToRadio(channel, source, packet);
        }

    private:
        ChannelAPI *m_channel;
        const QObject *m_source;
        QByteArray m_packet;
        MsgToRadio(ChannelAPI *channel, const QObject *source, const QByteArray& packet) :
            Message(), m_channel(channel), m_source(source), m_packet(packet) { }
    };

    // a line for the event log of a network (0: the controller)
    class MsgReportLog : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        int getNetwork() const { return m_network; }
        const QString& getText() const { return m_text; }
        static MsgReportLog* create(int network, const QString& text) { return new MsgReportLog(network, text); }

    private:
        int m_network;
        QString m_text;
        MsgReportLog(int network, const QString& text) : Message(), m_network(network), m_text(text) { }
    };

    // the state of a network, once a second while it runs
    class MsgReportStatus : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        struct Carrier
        {
            int channel = 0;
            QString type;
            double downlink = 0.0;      // Hz
            double uplink = 0.0;
            QString state;              // "off: reason" if it is not on the air
            float rxLevel = -100.0f;    // dB of full scale
            double txFillMs = 0.0;
            quint64 underruns = 0;
        };

        int m_network;
        bool m_stale = false;           // the network runs with other settings than the current ones
        bool m_onAir = false;
        int m_registered = 0;
        int m_calls = 0;
        QString m_summary;
        QList<Carrier> m_carriers;
        static MsgReportStatus* create(int network) { return new MsgReportStatus(network); }

    private:
        explicit MsgReportStatus(int network) : Message(), m_network(network) { }
    };

    Cellular(WebAPIAdapterInterface *webAPIAdapterInterface);
    ~Cellular() override;
    void destroy() override { delete this; }
    bool handleMessage(const Message& cmd) override;

    void getIdentifier(QString& id) const override { id = objectName(); }
    QString getIdentifier() const override { return objectName(); }
    void getTitle(QString& title) const override { title = m_settings.m_title; }

    QByteArray serialize() const override { return m_settings.serialize(); }
    bool deserialize(const QByteArray& data) override;

    int webapiRun(bool run, SWGSDRangel::SWGDeviceState& response, QString& errorMessage) override;
    int webapiSettingsGet(SWGSDRangel::SWGFeatureSettings& response, QString& errorMessage) override;
    int webapiSettingsPutPatch(bool force, const QStringList& featureSettingsKeys,
        SWGSDRangel::SWGFeatureSettings& response, QString& errorMessage) override;
    int webapiReportGet(SWGSDRangel::SWGFeatureReport& response, QString& errorMessage) override;

    static void webapiFormatFeatureSettings(SWGSDRangel::SWGFeatureSettings& response, const CellularSettings& settings);
    // The keys are those of the top level. A network in the list takes the fields it has; the
    // others come from the network with its id, or from the defaults for a new id.
    static void webapiUpdateFeatureSettings(CellularSettings& settings, const QStringList& featureSettingsKeys,
        SWGSDRangel::SWGFeatureSettings& response);

    static const char* const m_featureIdURI;
    static const char* const m_featureId;

private:
    struct Active
    {
        CellularNetworkSettings settings;       // as it started
        ChannelAPI *rx = nullptr;
        ChannelAPI *tx = nullptr;
    };
    struct Pipe
    {
        ObjectPipe *pipe = nullptr;
        int users = 0;
    };

    CellularSettings m_settings;
    // m_settings changes in the thread of the feature; the web API reads it in the threads of
    // the HTTP server
    mutable QMutex m_settingsMutex;
    QThread *m_thread;
    CellularWorker *m_worker;
    bool m_running;
    std::map<int, Active> m_active;             // the networks that run, by id
    std::map<ChannelAPI*, Pipe> m_pipes;        // the radios in use
    mutable QMutex m_reportMutex;
    std::map<int, std::unique_ptr<MsgReportStatus>> m_reports;  // the last state of each network that runs

    void start();
    void stop();
    void startNetwork(const CellularNetworkSettings& network);
    void stopNetwork(int id);
    void applyNetworks();
    ChannelAPI *findChannel(int deviceSetIndex, const QString& uri, QString& error) const;
    MessageQueue *usePipe(ChannelAPI *channel);
    void releasePipe(ChannelAPI *channel);
    void report(int network, const QString& text);
    void keepReport(const MsgReportStatus& report);
    void dropReport(int network);

private slots:
    void handlePipeToBeDeleted(int reason, QObject *object);
};

#endif // INCLUDE_FEATURE_CELLULAR_H_
