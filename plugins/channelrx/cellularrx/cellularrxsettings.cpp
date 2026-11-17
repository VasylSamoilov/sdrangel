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

#include <QColor>

#include "util/simpleserializer.h"
#include "settings/serializable.h"

#include "cellularrxsettings.h"

CellularRxSettings::CellularRxSettings() :
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void CellularRxSettings::resetToDefaults()
{
    m_rgbColor = QColor(0, 160, 120).rgb();
    m_title = "Cellular Rx";
    m_streamIndex = 0;
    m_workspaceIndex = 0;
    m_hidden = false;
}

QByteArray CellularRxSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeU32(1, m_rgbColor);
    s.writeString(2, m_title);
    s.writeS32(3, m_streamIndex);
    if (m_rollupState) {
        s.writeBlob(4, m_rollupState->serialize());
    }
    s.writeS32(5, m_workspaceIndex);
    s.writeBlob(6, m_geometryBytes);
    s.writeBool(7, m_hidden);

    return s.final();
}

bool CellularRxSettings::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid() || d.getVersion() != 1)
    {
        resetToDefaults();
        return false;
    }

    QByteArray bytetmp;

    d.readU32(1, &m_rgbColor, QColor(0, 160, 120).rgb());
    d.readString(2, &m_title, "Cellular Rx");
    d.readS32(3, &m_streamIndex, 0);
    if (m_rollupState)
    {
        d.readBlob(4, &bytetmp);
        m_rollupState->deserialize(bytetmp);
    }
    d.readS32(5, &m_workspaceIndex, 0);
    d.readBlob(6, &m_geometryBytes);
    d.readBool(7, &m_hidden, false);

    return true;
}
