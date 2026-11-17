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
#include <QStringList>

#include "util/simpleserializer.h"
#include "settings/serializable.h"

#include "cellularsettings.h"

CellularSettings::CellularSettings() :
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void CellularSettings::resetToDefaults()
{
    m_system = 0;
    m_country = "DK";
    m_area = 1;
    m_areaNo = 0;
    m_msPower = 1;
    m_carriers = "1:cctc, 5";
    m_compandor = true;
    m_sendCallerId = false;
    m_sendClock = false;
    m_smscNumber = "";
    m_fallback = FallbackEcho;
    m_rxDeviceSetIndex = 0;
    m_txDeviceSetIndex = 1;
    m_leadMs = 100;
    m_title = "Cellular";
    m_rgbColor = QColor(0, 160, 120).rgb();
    m_workspaceIndex = 0;
}

QByteArray CellularSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS32(1, m_system);
    s.writeString(2, m_country);
    s.writeS32(3, m_area);
    s.writeS32(4, m_areaNo);
    s.writeS32(5, m_msPower);
    s.writeString(6, m_carriers);
    s.writeBool(7, m_compandor);
    s.writeBool(8, m_sendCallerId);
    s.writeBool(9, m_sendClock);
    s.writeString(10, m_smscNumber);
    s.writeS32(11, m_fallback);
    s.writeS32(12, m_rxDeviceSetIndex);
    s.writeS32(13, m_txDeviceSetIndex);
    s.writeS32(14, m_leadMs);

    s.writeString(20, m_title);
    s.writeU32(21, m_rgbColor);
    s.writeS32(22, m_workspaceIndex);
    s.writeBlob(23, m_geometryBytes);
    if (m_rollupState) {
        s.writeBlob(24, m_rollupState->serialize());
    }

    return s.final();
}

bool CellularSettings::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid() || d.getVersion() != 1)
    {
        resetToDefaults();
        return false;
    }

    QByteArray bytetmp;

    d.readS32(1, &m_system, 0);
    d.readString(2, &m_country, "DK");
    d.readS32(3, &m_area, 1);
    d.readS32(4, &m_areaNo, 0);
    d.readS32(5, &m_msPower, 1);
    d.readString(6, &m_carriers, "1:cctc, 5");
    d.readBool(7, &m_compandor, true);
    d.readBool(8, &m_sendCallerId, false);
    d.readBool(9, &m_sendClock, false);
    d.readString(10, &m_smscNumber, "");
    d.readS32(11, &m_fallback, FallbackEcho);
    d.readS32(12, &m_rxDeviceSetIndex, 0);
    d.readS32(13, &m_txDeviceSetIndex, 1);
    d.readS32(14, &m_leadMs, 100);

    d.readString(20, &m_title, "Cellular");
    d.readU32(21, &m_rgbColor, QColor(0, 160, 120).rgb());
    d.readS32(22, &m_workspaceIndex, 0);
    d.readBlob(23, &m_geometryBytes);
    if (m_rollupState)
    {
        d.readBlob(24, &bytetmp);
        m_rollupState->deserialize(bytetmp);
    }

    return true;
}

bool CellularSettings::parseCarriers(const QString& text, std::vector<modemnmt::NmtCarrierConfig>& carriers, QString& error)
{
    carriers.clear();

    for (const QString& item : text.split(',', Qt::SkipEmptyParts))
    {
        const QStringList parts = item.trimmed().split(':');
        modemnmt::NmtCarrierConfig c;
        bool ok = false;

        if (parts.isEmpty() || parts.size() > 3)
        {
            error = QString("\"%1\": channel[:type[:supervisory]]").arg(item.trimmed());
            return false;
        }
        c.channel = parts[0].trimmed().toInt(&ok);
        if (!ok || c.channel <= 0)
        {
            error = QString("\"%1\": the channel is not a number").arg(item.trimmed());
            return false;
        }
        if (parts.size() > 1)
        {
            const QString type = parts[1].trimmed().toLower();

            if (type == "cc") {
                c.type = modemnmt::ChannelType::Cc;
            } else if (type == "cca") {
                c.type = modemnmt::ChannelType::Cca;
            } else if (type == "ccb") {
                c.type = modemnmt::ChannelType::Ccb;
            } else if (type == "tc") {
                c.type = modemnmt::ChannelType::Tc;
            } else if (type == "actc") {
                c.type = modemnmt::ChannelType::AcTc;
            } else if (type == "cctc") {
                c.type = modemnmt::ChannelType::CcTc;
            } else if (type == "test") {
                c.type = modemnmt::ChannelType::Test;
            }
            else
            {
                error = QString("\"%1\": the type is cc, cca, ccb, tc, actc, cctc or test").arg(item.trimmed());
                return false;
            }
        }
        if (parts.size() > 2)
        {
            c.supervisory = parts[2].trimmed().toInt(&ok);
            if (!ok || c.supervisory < 0 || c.supervisory > 4)
            {
                error = QString("\"%1\": the supervisory tone is 1..4").arg(item.trimmed());
                return false;
            }
        }
        carriers.push_back(c);
    }

    if (carriers.empty())
    {
        error = "no carriers";
        return false;
    }
    return true;
}

bool CellularSettings::networkConfig(modemnmt::NmtNetworkConfig& config, QString& error) const
{
    config = modemnmt::NmtNetworkConfig();
    config.system = m_system == 1 ? modemnmt::System::Nmt900 : modemnmt::System::Nmt450;
    config.country = m_country.trimmed().toStdString();
    config.area = m_area;
    config.areaNo = m_areaNo;
    config.msPower = m_msPower;
    config.compandor = m_compandor;
    config.sendCallerId = m_sendCallerId;
    config.sendClock = m_sendClock;
    config.smscNumber = m_smscNumber.trimmed().toStdString();

    return parseCarriers(m_carriers, config.carriers, error);
}
