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

#ifndef INCLUDE_RADIOPAGINGDEMODSETTINGS_H
#define INCLUDE_RADIOPAGINGDEMODSETTINGS_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "dsp/dsptypes.h"

class Serializable;

// Settings of the radio paging demodulator: POCSAG, FLEX and GSC are decoded at the same
// time, each protocol can be switched off. A protocol that is on is decoded at every
// speed, mode and polarity; every message received is output with its protocol, speed
// (or mode) and polarity, and selecting networks is done downstream
struct RadioPagingDemodSettings
{
    static const int m_nbPocsagSpeeds = 3;
    static const int m_pocsagSpeeds[m_nbPocsagSpeeds];     //!< 512, 1200, 2400

    qint32 m_inputFrequencyOffset;
    Real m_rfBandwidth;                         //!< Channel filter, Hz. The legs filter further
    bool m_pocsagEnabled;                       //!< Decode POCSAG
    Real m_pocsagDeviation;                     //!< Hz
    bool m_flexEnabled;                         //!< Decode FLEX
    Real m_flexDeviation;                       //!< Outer symbol, Hz
    bool m_gscEnabled;                          //!< Decode GSC
    Real m_gscDeviation;                        //!< Hz
    int m_gscVoiceTimeout;                      //!< Longest voice message, seconds
    bool m_gscAudio;                            //!< Play the voice of GSC voice pages
    bool m_gscVoiceRecord;                      //!< Record the voice of GSC voice pages to a WAV file
    QString m_gscVoiceDirectory;                //!< Directory of the voice files, empty for the default (see voiceDirectory)
    int m_gscVoiceKeepDays;                     //!< Voice files older than this many days may be deleted, 0 to keep them all
    int m_gscVoiceKeepMin;                      //!< The newest voice files this many are not deleted
    QString m_channelAlias;                     //!< Name of the demodulator in the message records and voice file names, empty for its frequency
    Real m_volume;                              //!< Voice audio gain
    QString m_audioDeviceName;                  //!< Voice audio output

    quint32 m_rgbColor;
    QString m_title;
    Serializable *m_channelMarker;
    int m_streamIndex;                          //!< MIMO channel. Not relevant when connected to SI (single Rx)
    bool m_useReverseAPI;
    QString m_reverseAPIAddress;
    uint16_t m_reverseAPIPort;
    uint16_t m_reverseAPIDeviceIndex;
    uint16_t m_reverseAPIChannelIndex;
    Serializable *m_rollupState;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;
    bool m_hidden;

    static const int m_channelSampleRate = 38400;   //!< Multiple of 512, 1200, 2400, 1600, 3200 and 600 baud
    static double m_legOffsetAllowance;             //!< Offset room each side of a leg, Hz
    static bool m_legAfc;                           //!< Legs follow the measured carrier offset

    RadioPagingDemodSettings();
    void resetToDefaults();
    void setChannelMarker(Serializable *channelMarker) { m_channelMarker = channelMarker; }
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void applySettings(const QStringList& settingsKeys, const RadioPagingDemodSettings& settings);
    QString getDebugString(const QStringList& settingsKeys, bool force = false) const;

    //!< Carson bandwidths 2 * (deviation + highest modulation rate): the legs while receiving, Hz
    double pocsagCarson() const;
    double flexCarson() const;
    double gscCarson() const;
    //!< Leg bandwidths while hunting: Carson and the offset room each side, Hz
    double pocsagBandwidth() const;
    double flexBandwidth() const;
    double gscBandwidth() const;

    //!< The directory of the voice files: the one set, or radiopaging/voice in the application data
    QString voiceDirectory() const;
    //!< The name of the demodulator: the alias set, or the frequency of the channel in Hz
    QString effectiveChannelAlias(qint64 frequency) const;
};

#endif // INCLUDE_RADIOPAGINGDEMODSETTINGS_H
