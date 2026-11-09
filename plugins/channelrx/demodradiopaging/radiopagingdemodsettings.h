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

// Settings of the radio paging demodulator: POCSAG, FLEX and GSC are always all decoded,
// every speed, mode and polarity. Every message received is output with its protocol,
// speed (or mode) and polarity; selecting networks is done downstream
struct RadioPagingDemodSettings
{
    static const int m_nbPocsagSpeeds = 3;
    static const int m_pocsagSpeeds[m_nbPocsagSpeeds];     //!< 512, 1200, 2400

    qint32 m_inputFrequencyOffset;
    Real m_rfBandwidth;                         //!< Channel filter, Hz. The legs filter further
    Real m_pocsagDeviation;                     //!< Hz
    Real m_flexDeviation;                       //!< Outer symbol, Hz
    Real m_gscDeviation;                        //!< Hz
    bool m_gscNonBatterySaver;                  //!< Also detect the 1100 pattern (no preamble)
    int m_gscVoiceTimeout;                      //!< Seconds

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

    RadioPagingDemodSettings();
    void resetToDefaults();
    void setChannelMarker(Serializable *channelMarker) { m_channelMarker = channelMarker; }
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void applySettings(const QStringList& settingsKeys, const RadioPagingDemodSettings& settings);
    QString getDebugString(const QStringList& settingsKeys, bool force = false) const;

    //!< Carson bandwidths 2 * (deviation + highest modulation rate) of the legs, Hz
    double pocsagBandwidth() const;
    double flexBandwidth() const;
    double gscBandwidth() const;
};

#endif // INCLUDE_RADIOPAGINGDEMODSETTINGS_H
