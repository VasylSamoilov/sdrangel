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

#include <QStringList>
#include <QThread>

#include "feature/feature.h"
#include "util/message.h"

#include "cellularsettings.h"

class WebAPIAdapterInterface;
class ChannelAPI;
class ObjectPipe;
class CellularWorker;

// The controller of the cellular base stations: it holds the network and all its protocol state.
// It finds the Cellular Rx channel of the receiving device set and the Cellular Tx channel of the
// transmitting one, takes the received batches of the receiver, runs the network on them and
// hands the audio to send to the transmitter. The network runs in a thread of its own (the worker).
class Cellular : public Feature
{
    Q_OBJECT
public:
    class MsgConfigureCellular : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const CellularSettings& getSettings() const { return m_settings; }
        static MsgConfigureCellular* create(const CellularSettings& settings) { return new MsgConfigureCellular(settings); }

    private:
        CellularSettings m_settings;
        explicit MsgConfigureCellular(const CellularSettings& settings) : Message(), m_settings(settings) { }
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

    // a line for the event log of the network
    class MsgReportLog : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const QString& getText() const { return m_text; }
        static MsgReportLog* create(const QString& text) { return new MsgReportLog(text); }

    private:
        QString m_text;
        explicit MsgReportLog(const QString& text) : Message(), m_text(text) { }
    };

    // the state of the network, once a second while it runs
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

        QString m_summary;
        QList<Carrier> m_carriers;
        static MsgReportStatus* create() { return new MsgReportStatus(); }

    private:
        MsgReportStatus() : Message() { }
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

    static const char* const m_featureIdURI;
    static const char* const m_featureId;

private:
    CellularSettings m_settings;
    QThread *m_thread;
    CellularWorker *m_worker;
    ChannelAPI *m_rxChannel;
    ChannelAPI *m_txChannel;
    bool m_running;

    void start();
    void stop();
    ChannelAPI *findChannel(int deviceSetIndex, const QString& uri, QString& error) const;
    void unregisterPipes();
    void report(const QString& text);

private slots:
    void handlePipeToBeDeleted(int reason, QObject *object);
};

#endif // INCLUDE_FEATURE_CELLULAR_H_
