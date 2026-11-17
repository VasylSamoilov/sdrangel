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
    int m_areaNo;               // area number 1..4, 0 for none
    int m_msPower;              // power level of the phones 0..3
    QString m_carriers;         // "1:cctc, 5, 6:tc:2" (see parseCarriers)
    bool m_compandor;
    bool m_sendCallerId;
    bool m_sendClock;
    QString m_smscNumber;
    int m_fallback;

    // the device sets that carry the Cellular Rx and the Cellular Tx channel of this network
    int m_rxDeviceSetIndex;
    int m_txDeviceSetIndex;

    explicit CellularNetworkSettings(int id = 1);
    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);

    // The carriers, comma separated: channel[:type[:supervisory]]. Type is cc, tc, cctc, actc
    // (NMT-900), cca, ccb (NMT-900) or test, tc if not given; the supervisory tone 1..4 on a
    // traffic channel, none if not given. False with a reason if the text cannot be read.
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

    QString m_title;
    quint32 m_rgbColor;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;
    Serializable *m_rollupState;

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
