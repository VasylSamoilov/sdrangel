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

#ifndef INCLUDE_FEATURE_CELLULARSETTINGS_H_
#define INCLUDE_FEATURE_CELLULARSETTINGS_H_

#include <QByteArray>
#include <QList>
#include <QString>

#include "nmtbasestation.h"

class Serializable;

// What answers the calls of a network to numbers that are no phone: echo or music, and how.
struct CellularFarEnd
{
    enum MusicSource
    {
        MusicMelody = 0,        // the built-in melody
        MusicFile = 1,          // a sound file (WAV)
        MusicInput = 2          // an SDRangel audio input
    };

    int m_echoDelayMs = 1000;   // 200..3000
    int m_echoLevelDb = 0;      // at most 0
    bool m_echoAnnounce = false;    // a short tone when the echo answers
    int m_musicSource = MusicMelody;
    QString m_musicFile;
    QString m_musicInput;       // the audio input device by name, empty: the default
    int m_musicLevelDb = 0;     // at most 0
    bool m_musicLoop = true;    // false: plays once, then the call ends
    int m_callLimitS = 0;       // the longest call to echo or music in seconds, 0: no limit

    bool operator==(const CellularFarEnd& o) const;
    bool operator!=(const CellularFarEnd& o) const { return !(*this == o); }
};

// One NMT network (one base station) and the radios it runs on.
struct CellularNetworkSettings
{
    enum Fallback
    {
        FallbackEcho = 0,       // calls to numbers that are not registered: echo
        FallbackMusic = 1       // or music
    };

    int m_id;                   // 1..: stable, the carrier ids are id * 10000 + channel
    QString m_name;
    bool m_enabled;             // on the air while the controller runs

    int m_system;               // 0: NMT-450, 1: NMT-900
    QString m_country;          // short name of the plan: DK, SE, NO, FI, NL, HU ...
    int m_area;                 // traffic area Y2 within the country
    int m_y1;                   // country digit Y1 in place of the country's (test network), -1: the country's
    int m_areaNo;               // area number 1..4, 0 for none
    int m_msPower;              // power level of the phones 0..3
    QString m_carriers;         // "1:cctc:1, 5:tc:2, 6:tc:3" (see parseCarriers)
    bool m_compandor;
    bool m_sendCallerId;
    bool m_sendClock;
    QString m_smscNumber;
    int m_fallback;
    CellularFarEnd m_farEnd;

    // the device sets that carry the Cellular Rx and the Cellular Tx channel of this network
    int m_rxDeviceSetIndex;
    int m_txDeviceSetIndex;

    explicit CellularNetworkSettings(int id = 1);
    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);

    // The carriers, comma separated: channel:type[:supervisory]. The type has to be given: cc, tc,
    // cctc, actc (NMT-900), cca, ccb (NMT-900) or test. The supervisory tone is 1..4 on a traffic
    // channel; when no carrier gives one, the traffic channels get 1, 2, 3, 4, 1 ... in turn.
    // False with a reason if the text cannot be read.
    static bool parseCarriers(const QString& text, std::vector<modemnmt::NmtCarrierConfig>& carriers, QString& error);
    // The network as the base station takes it; false with a reason if the carriers cannot be read.
    bool networkConfig(modemnmt::NmtNetworkConfig& config, QString& error) const;
    // What the base station has to start again for (everything but the name and the switch).
    bool sameNetwork(const CellularNetworkSettings& other) const;
};

// The networks of the controller.
struct CellularSettings
{
    QList<CellularNetworkSettings> m_networks;
    int m_leadMs;               // lead of the transmitters over the receivers
    bool m_logEnabled;          // the log also into the .csv file m_logFilename
    QString m_logFilename;

    QString m_title;
    quint32 m_rgbColor;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;
    // The book of phones (CellularBook) as it was saved. Read only when settings are loaded (a
    // preset): the feature keeps the book itself and ignores this in ordinary settings messages.
    QByteArray m_book;
    Serializable *m_rollupState;
    // order and sizes of the columns of the networks and carriers tables (size 0: hidden, -1: auto)
    static constexpr int kNetworkColumns = 7;
    static constexpr int kCarrierColumns = 10;
    QList<int> m_networkColumnIndexes;
    QList<int> m_networkColumnSizes;
    QList<int> m_carrierColumnIndexes;
    QList<int> m_carrierColumnSizes;

    CellularSettings();
    void resetToDefaults();
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);

    // a new network with the next free id, not on the air
    CellularNetworkSettings& addNetwork();
    const CellularNetworkSettings *network(int id) const;
};

#endif // INCLUDE_FEATURE_CELLULARSETTINGS_H_
