///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

#ifndef INCLUDE_RADIOPAGINGDEMODSINK_H
#define INCLUDE_RADIOPAGINGDEMODSINK_H

#include <QMutex>
#include <QString>

#include "dsp/channelsamplesink.h"
#include "dsp/phasediscri.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "dsp/firfilter.h"
#include "util/movingaverage.h"
#include "util/messagerecord.h"

#include "radiopagingpocsagreceiver.h"
#include "radiopagingflexreceiver.h"
#include "radiopaginggscreceiver.h"

#include "radiopagingdemodsettings.h"

class ChannelAPI;
class MessageQueue;

// Channel IQ at 38400 Hz is shared by one leg per protocol. Each leg has its own filter at
// the protocol's Carson bandwidth (skipped when not narrower than the channel filter) and
// its own FM discriminator scaled to the protocol's deviation, and feeds the receivers of
// all its speeds, each with both polarities. Nothing is shared between the legs: every
// receiver hunts for its own sync. Decoded messages are sent to the channel as records
class RadioPagingDemodSink : public ChannelSampleSink {
public:
    struct Status
    {
        bool m_pocsagEnabled[RadioPagingDemodSettings::m_nbPocsagSpeeds];
        bool m_pocsagSync[RadioPagingDemodSettings::m_nbPocsagSpeeds];      //!< A decoder of the speed is synchronised
        bool m_pocsagInverted[RadioPagingDemodSettings::m_nbPocsagSpeeds];  //!< ... and it is the inverted one
        bool m_flexEnabled;
        bool m_flexSync;
        QString m_flexMode;
        bool m_flexInverted;
        float m_flexQuality;
        bool m_gscEnabled;
        bool m_gscSync;
        bool m_gscVoice;
        bool m_gscInverted;
        quint32 m_messages[3];          //!< POCSAG, FLEX, GSC records sent since start
    };

    RadioPagingDemodSink();
    ~RadioPagingDemodSink();

    virtual void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end);

    void applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force = false);
    void applySettings(const QStringList& settingsKeys, const RadioPagingDemodSettings& settings, bool force = false);
    void setMessageQueueToChannel(MessageQueue *messageQueue) { m_messageQueueToChannel = messageQueue; }
    void setChannel(ChannelAPI *channel) { m_channel = channel; }

    double getMagSq() const { return m_magsq; }
    Status getStatus() const;

    void getMagSqLevels(double& avg, double& peak, int& nbSamples)
    {
        if (m_magsqCount > 0)
        {
            m_magsq = m_magsqSum / m_magsqCount;
            m_magSqLevelStore.m_magsq = m_magsq;
            m_magSqLevelStore.m_magsqPeak = m_magsqPeak;
        }

        avg = m_magSqLevelStore.m_magsq;
        peak = m_magSqLevelStore.m_magsqPeak;
        nbSamples = m_magsqCount == 0 ? 1 : m_magsqCount;

        m_magsqSum = 0.0f;
        m_magsqPeak = 0.0f;
        m_magsqCount = 0;
    }

private:
    struct MagSqLevelsStore
    {
        MagSqLevelsStore() :
            m_magsq(1e-12),
            m_magsqPeak(1e-12)
        {}
        double m_magsq;
        double m_magsqPeak;
    };

    // Filter and FM discriminator of one protocol
    struct Leg
    {
        Lowpass<Complex> m_filter;
        bool m_filtered = false;        //!< The leg is narrower than the channel filter
        PhaseDiscriminators m_discri;

        void create(double bandwidth, double channelBandwidth, double deviation);
        Real demod(const Complex& sample);
    };

    RadioPagingDemodSettings m_settings;
    ChannelAPI *m_channel;
    MessageQueue *m_messageQueueToChannel;
    int m_channelSampleRate;
    int m_channelFrequencyOffset;

    NCO m_nco;
    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;

    double m_magsq;
    double m_magsqSum;
    double m_magsqPeak;
    int m_magsqCount;
    MagSqLevelsStore m_magSqLevelStore;
    MovingAverageUtil<Real, double, 16> m_movingAverage;

    Leg m_pocsagLeg;
    Leg m_flexLeg;
    Leg m_gscLeg;
    RadioPagingPocsagReceiver m_pocsag[RadioPagingDemodSettings::m_nbPocsagSpeeds];
    RadioPagingFlexReceiver m_flex;
    RadioPagingGscReceiver m_gsc;
    QString m_gscVoiceId[2];            //!< Record id of the voice page in progress, by polarity

    mutable QMutex m_statusMutex;
    Status m_status;
    int m_statusCount;

    void processOneSample(Complex& ci);
    void applyLegs();
    void updateStatus();
    void sendRecord(MessageRecord& record, int protocol);
    void sendPocsag(const RadioPagingPocsagDecoder::Message& message, int baud);
    void sendFlex(const RadioPagingFlexDecoder::Message& message);
    void sendGsc(const RadioPagingGscDecoder::Message& message);
    static MessageRecord::Polarity polarity(bool inverted);
    //!< Characters below 0x20 and 0x7f as <0xNN>, other bytes kept (UTF-8 passes)
    static QString escapeText(const std::string& text);
    static QByteArray toByteArray(const std::vector<uint8_t>& data);
};

#endif // INCLUDE_RADIOPAGINGDEMODSINK_H
