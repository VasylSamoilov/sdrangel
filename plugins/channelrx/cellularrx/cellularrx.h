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

#ifndef INCLUDE_CELLULARRX_H
#define INCLUDE_CELLULARRX_H

#include <map>

#include <QMutex>
#include <QString>
#include <QThread>

#include "channel/channelapi.h"
#include "dsp/basebandsamplesink.h"
#include "util/message.h"

#include "carrierwire.h"
#include "cellularrxbaseband.h"
#include "cellularrxsettings.h"

class DeviceAPI;

// The receiver of the cellular base stations: one per receiving device, all the carriers of the
// networks whose uplinks are in the span of the device. The carriers are given by the controller
// (the Cellular feature) through the input queue; the received batches go to the controller
// through the "cellular" message pipe, and so does the span of the stream (centre and rate).
class CellularRx : public BasebandSampleSink, public ChannelAPI
{
public:
    CellularRx(DeviceAPI *deviceAPI);
    ~CellularRx() override;
    void destroy() override { delete this; }
    void setDeviceAPI(DeviceAPI *deviceAPI) override;
    DeviceAPI *getDeviceAPI() override { return m_deviceAPI; }

    using BasebandSampleSink::feed;
    void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool po) override;
    void start() override;
    void stop() override;
    void pushMessage(Message *msg) override { m_inputMessageQueue.push(msg); }
    QString getSinkName() override { return objectName(); }

    void getIdentifier(QString& id) override { id = objectName(); }
    QString getIdentifier() const override { return objectName(); }
    const QString& getURI() const { return getName(); }
    void getTitle(QString& title) override { title = m_settings.m_title; }
    qint64 getCenterFrequency() const override { return 0; }
    void setCenterFrequency(qint64) override {}

    QByteArray serialize() const override { return m_settings.serialize(); }
    bool deserialize(const QByteArray& data) override { return m_settings.deserialize(data); }

    int getNbSinkStreams() const override { return 1; }
    int getNbSourceStreams() const override { return 0; }
    int getStreamIndex() const override { return m_settings.m_streamIndex; }
    qint64 getStreamCenterFrequency(int, bool) const override { return m_centerFrequency; }

    CellularRxBaseband::Status status() const { return m_basebandSink->status(); }
    // the names of the networks on the device by id, as the controller sent them
    std::map<int, QString> networkNames() const;
    // the speech from the phones of the calls on the carriers by id, as the controller sent it
    std::map<int, modemcellular::WireSpeech::Carrier> speech() const;
    qint64 getDeviceCenterFrequency() const { return m_centerFrequency; }

    static const char * const m_channelIdURI;
    static const char * const m_channelId;

private:
    DeviceAPI *m_deviceAPI;
    QThread m_thread;
    CellularRxBaseband *m_basebandSink;
    CellularRxSettings m_settings;
    int m_basebandSampleRate;
    qint64 m_centerFrequency;
    std::map<int, QString> m_networkNames;
    std::map<int, modemcellular::WireSpeech::Carrier> m_speech;
    std::map<int, qint64> m_speechAt;       // ms since the epoch of the last report of a carrier
    mutable QMutex m_namesMutex;            // the names and the speech

    bool handleMessage(const Message& cmd) override;
    void sendSpan();
};

#endif // INCLUDE_CELLULARRX_H
