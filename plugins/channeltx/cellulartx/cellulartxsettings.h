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

#ifndef INCLUDE_CELLULARTXSETTINGS_H
#define INCLUDE_CELLULARTXSETTINGS_H

#include <QByteArray>
#include <QString>

class Serializable;

// The transmitter has no radio settings of its own: the carriers come from the controller (the
// Cellular feature). What is kept is the look of the channel window.
struct CellularTxSettings
{
    quint32 m_rgbColor;
    QString m_title;
    int m_streamIndex;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;
    bool m_hidden;
    Serializable *m_rollupState;

    CellularTxSettings();
    void resetToDefaults();
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
};

#endif // INCLUDE_CELLULARTXSETTINGS_H
