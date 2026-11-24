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

#include <QDateTime>
#include <QMutex>
#include <QStringList>
#include <QThread>

#include "feature/feature.h"
#include "util/message.h"

#include "cellularsettings.h"
#include "nmtphonebook.h"

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
        // which running networks start again with the new settings: the ones whose settings
        // changed (the GUI, a PATCH), all of them (a PUT)
        enum class Restart { Changed, All };
        Restart getRestart() const { return m_restart; }
        static MsgConfigureCellular* create(const CellularSettings& settings, Restart restart = Restart::Changed) {
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

    // An action of the tester on a phone of a network (GUI or web API), done by the worker.
    class MsgAction : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        enum Action
        {
            Call,       // the network calls the phone (registered or not); the far end is echo or music
            HangUp,     // ends the call of the network to the phone
            Release,    // releases any call of the phone
            Sms,        // a short message to the phone (text)
            Mwi,        // message waiting indicators (value: flags)
            Power,      // power level of the phone in its call (value: 0..3)
            Move,       // the call of the phone to another traffic channel (value: channel, 0 any)
            Hold,       // a short message (text) the centre keeps until the phone fetches it
            Forget      // the record of the phone in the book (the system of the network)
        };
        static bool parse(const QString& name, Action& action);
        static const char* name(Action action);

        int getNetwork() const { return m_network; }
        Action getAction() const { return m_action; }
        const QString& getNumber() const { return m_number; }
        const QString& getText() const { return m_text; }
        int getValue() const { return m_value; }
        static MsgAction* create(int network, Action action, const QString& number, const QString& text, int value) {
            return new MsgAction(network, action, number, text, value);
        }

    private:
        int m_network;
        Action m_action;
        QString m_number;
        QString m_text;
        int m_value;
        MsgAction(int network, Action action, const QString& number, const QString& text, int value) :
            Message(), m_network(network), m_action(action), m_number(number), m_text(text), m_value(value) { }
    };

    // The book of phones. The feature keeps it; the worker tells what it learned, the GUI keeps a
    // copy (for presets) and sends the user's edits.
    // worker -> feature: an observation of a phone
    class MsgBookLearn : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        enum Kind { Password, Dialled, SmsCentre, Area };
        Kind m_kind;
        modemnmt::System m_system;
        std::string m_number;
        std::string m_value;
        int m_y1;
        int m_area;
        static MsgBookLearn* create(Kind kind, modemnmt::System system, const std::string& number, const std::string& value,
                int y1 = 0, int area = 0) {
            return new MsgBookLearn(kind, system, number, value, y1, area);
        }

    private:
        MsgBookLearn(Kind kind, modemnmt::System system, const std::string& number, const std::string& value, int y1, int area) :
            Message(), m_kind(kind), m_system(system), m_number(number), m_value(value), m_y1(y1), m_area(area) { }
    };
    // feature -> GUI: a record as it is now (or that it is gone)
    class MsgBookRecord : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        modemnmt::NmtPhoneRecord m_record;
        bool m_removed;
        static MsgBookRecord* create(const modemnmt::NmtPhoneRecord& record, bool removed = false) {
            return new MsgBookRecord(record, removed);
        }

    private:
        MsgBookRecord(const modemnmt::NmtPhoneRecord& record, bool removed) : Message(), m_record(record), m_removed(removed) { }
    };
    // GUI -> feature: the user's value of one field, or (field -1) forget the phone
    class MsgBookEdit : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        modemnmt::System m_system;
        std::string m_number;
        int m_field;
        std::string m_value;
        static MsgBookEdit* create(modemnmt::System system, const std::string& number, int field, const std::string& value) {
            return new MsgBookEdit(system, number, field, value);
        }

    private:
        MsgBookEdit(modemnmt::System system, const std::string& number, int field, const std::string& value) :
            Message(), m_system(system), m_number(number), m_field(field), m_value(value) { }
    };
    // GUI -> feature: the book of loaded settings replaces the feature's (CellularBook bytes);
    // empty: send every record to the GUI
    class MsgBookReplace : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        QByteArray m_bytes;
        static MsgBookReplace* create(const QByteArray& bytes) { return new MsgBookReplace(bytes); }

    private:
        explicit MsgBookReplace(const QByteArray& bytes) : Message(), m_bytes(bytes) { }
    };

    // a line for the event log of a network (0: the controller)
    class MsgReportLog : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        // what the line is about (a badge in the log)
        enum Kind { Event, Frame, Call, Sms, Mwi, Power, Phone, Tone, Network, Warning };
        int getNetwork() const { return m_network; }
        const QString& getText() const { return m_text; }
        Kind getKind() const { return m_kind; }
        static MsgReportLog* create(int network, const QString& text, Kind kind = Event) { return new MsgReportLog(network, text, kind); }

    private:
        int m_network;
        QString m_text;
        Kind m_kind;
        MsgReportLog(int network, const QString& text, Kind kind) : Message(), m_network(network), m_text(text), m_kind(kind) { }
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
            QString phone;              // number of the phone the channel serves, empty for none
            quint64 framesReceived = 0; // frames received since the network started (any phone, any type)
        };
        // A phone the network has heard (its identity in a frame the channel took) or registered.
        struct Phone
        {
            QString number;             // country digit and six digits
            bool registered = false;    // roaming update done (NMT Doc 450/900-2 5.2.1)
            int channel = 0;            // channel it was last heard on
            QString state;              // what it does now on the channels, empty if nothing
            QDateTime lastHeard;        // invalid: not heard since the network started
            float level = -100.0f;      // dB of full scale of the channel then
            float quality = 0.0f;       // of the last frame, 0..1
            int power = -1;             // power level 0..3 in its frames, -1 not known
            QString lastFrame;          // type of its last frame
            // what it showed of itself (kept over restarts of the network), empty if not known
            QString password;           // K, three digits
            QString dialled;            // the numbers it dialled, the newest first
            QString smsCentre;          // the numbers it sent messages or asked for them on, with the traffic area
            int held = 0;               // short messages the centre keeps for it
            int pendingMwi = 0;         // indicators postponed until it is free (flags)
        };

        int m_network;
        bool m_onAir = false;
        int m_registered = 0;
        int m_calls = 0;
        QString m_summary;
        QList<Carrier> m_carriers;
        QList<Phone> m_phones;
        static MsgReportStatus* create(int network) { return new MsgReportStatus(network); }
        MsgReportStatus* clone() const
        {
            MsgReportStatus *copy = new MsgReportStatus(m_network);
            copy->m_onAir = m_onAir;
            copy->m_registered = m_registered;
            copy->m_calls = m_calls;
            copy->m_summary = m_summary;
            copy->m_carriers = m_carriers;
            copy->m_phones = m_phones;
            return copy;
        }

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

    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;

    int webapiRun(bool run, SWGSDRangel::SWGDeviceState& response, QString& errorMessage) override;
    int webapiSettingsGet(SWGSDRangel::SWGFeatureSettings& response, QString& errorMessage) override;
    int webapiSettingsPutPatch(bool force, const QStringList& featureSettingsKeys,
        SWGSDRangel::SWGFeatureSettings& response, QString& errorMessage) override;
    int webapiReportGet(SWGSDRangel::SWGFeatureReport& response, QString& errorMessage) override;
    int webapiActionsPost(const QStringList& featureActionsKeys, SWGSDRangel::SWGFeatureActions& query,
                          QString& errorMessage) override;

    static void webapiFormatFeatureSettings(SWGSDRangel::SWGFeatureSettings& response, const CellularSettings& settings);
    void webapiFormatMobiles(SWGSDRangel::SWGFeatureSettings& response) const;
    void webapiUpdateMobiles(const QStringList& featureSettingsKeys, SWGSDRangel::SWGFeatureSettings& response);
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
    // the book of phones: changed in the thread of the feature, read by the web API
    modemnmt::NmtPhoneBook m_book;
    mutable QMutex m_bookMutex;

    void start();
    void sendBookRecord(const modemnmt::NmtPhoneRecord& record, bool removed = false);
    void sendSmsCentres();
    void stop();
    void startNetwork(const CellularNetworkSettings& network);
    void stopNetwork(int id);
    void restartNetworks(bool all);
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
