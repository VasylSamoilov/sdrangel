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

#include <algorithm>

#include <QColor>
#include <QStringList>

#include "util/simpleserializer.h"
#include "settings/serializable.h"

#include "cellularsettings.h"

namespace
{

// the carrier ids are id * 10000 + channel and are int
const int kMaxNetworkId = 200000;
const int kMaxNetworks = 64;

// The fields of a network, at the keys from first on (the controller of version 1 kept its one
// network at keys 1..13).
void writeNetwork(SimpleSerializer& s, const CellularNetworkSettings& n, quint32 first)
{
    s.writeS32(first + 0, n.m_system);
    s.writeString(first + 1, n.m_country);
    s.writeS32(first + 2, n.m_area);
    s.writeS32(first + 3, n.m_areaNo);
    s.writeS32(first + 4, n.m_msPower);
    s.writeString(first + 5, n.m_carriers);
    s.writeBool(first + 6, n.m_compandor);
    s.writeBool(first + 7, n.m_sendCallerId);
    s.writeBool(first + 8, n.m_sendClock);
    s.writeString(first + 9, n.m_smscNumber);
    s.writeS32(first + 10, n.m_fallback);
    s.writeS32(first + 11, n.m_rxDeviceSetIndex);
    s.writeS32(first + 12, n.m_txDeviceSetIndex);
}

void readNetwork(SimpleDeserializer& d, CellularNetworkSettings& n, quint32 first)
{
    d.readS32(first + 0, &n.m_system, 0);
    d.readString(first + 1, &n.m_country, "DK");
    d.readS32(first + 2, &n.m_area, 1);
    d.readS32(first + 3, &n.m_areaNo, 0);
    d.readS32(first + 4, &n.m_msPower, 1);
    d.readString(first + 5, &n.m_carriers, "1:cctc, 5");
    d.readBool(first + 6, &n.m_compandor, true);
    d.readBool(first + 7, &n.m_sendCallerId, false);
    d.readBool(first + 8, &n.m_sendClock, false);
    d.readString(first + 9, &n.m_smscNumber, "");
    d.readS32(first + 10, &n.m_fallback, CellularNetworkSettings::FallbackEcho);
    d.readS32(first + 11, &n.m_rxDeviceSetIndex, 0);
    d.readS32(first + 12, &n.m_txDeviceSetIndex, 1);
}

} // namespace

// ---- one network ----

CellularNetworkSettings::CellularNetworkSettings(int id) :
    m_id(id)
{
    resetToDefaults();
}

void CellularNetworkSettings::resetToDefaults()
{
    m_name = QString("Network %1").arg(m_id);
    m_enabled = true;
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
}

QByteArray CellularNetworkSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS32(1, m_id);
    s.writeString(2, m_name);
    s.writeBool(3, m_enabled);
    writeNetwork(s, *this, 10);
    return s.final();
}

bool CellularNetworkSettings::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid() || d.getVersion() != 1) {
        return false;
    }
    d.readS32(1, &m_id, 1);
    d.readString(2, &m_name, QString("Network %1").arg(m_id));
    d.readBool(3, &m_enabled, true);
    readNetwork(d, *this, 10);
    return m_id >= 1 && m_id <= kMaxNetworkId;
}

bool CellularNetworkSettings::sameNetwork(const CellularNetworkSettings& o) const
{
    return m_id == o.m_id && m_system == o.m_system && m_country == o.m_country && m_area == o.m_area
        && m_areaNo == o.m_areaNo && m_msPower == o.m_msPower && m_carriers == o.m_carriers
        && m_compandor == o.m_compandor && m_sendCallerId == o.m_sendCallerId && m_sendClock == o.m_sendClock
        && m_smscNumber == o.m_smscNumber && m_fallback == o.m_fallback
        && m_rxDeviceSetIndex == o.m_rxDeviceSetIndex && m_txDeviceSetIndex == o.m_txDeviceSetIndex;
}

bool CellularNetworkSettings::parseCarriers(const QString& text, std::vector<modemnmt::NmtCarrierConfig>& carriers, QString& error)
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
        if (!ok || c.channel <= 0 || c.channel >= modemnmt::NmtBaseStation::kIdStride)
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

bool CellularNetworkSettings::networkConfig(modemnmt::NmtNetworkConfig& config, QString& error) const
{
    config = modemnmt::NmtNetworkConfig();
    config.id = m_id;
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

// ---- the controller ----

CellularSettings::CellularSettings() :
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void CellularSettings::resetToDefaults()
{
    m_networks.clear();
    m_networks.append(CellularNetworkSettings(1));
    m_leadMs = 100;
    m_title = "Cellular";
    m_rgbColor = QColor(0, 160, 120).rgb();
    m_workspaceIndex = 0;
}

CellularNetworkSettings& CellularSettings::addNetwork()
{
    int id = 1;

    for (const CellularNetworkSettings& n : m_networks) {
        id = std::max(id, n.m_id + 1);
    }
    m_networks.append(CellularNetworkSettings(id));
    m_networks.last().m_enabled = false;
    return m_networks.last();
}

const CellularNetworkSettings *CellularSettings::network(int id) const
{
    for (const CellularNetworkSettings& n : m_networks)
    {
        if (n.m_id == id) {
            return &n;
        }
    }
    return nullptr;
}

QByteArray CellularSettings::serialize() const
{
    SimpleSerializer s(2);

    s.writeS32(1, m_leadMs);
    s.writeS32(2, m_networks.size());
    s.writeString(20, m_title);
    s.writeU32(21, m_rgbColor);
    s.writeS32(22, m_workspaceIndex);
    s.writeBlob(23, m_geometryBytes);
    if (m_rollupState) {
        s.writeBlob(24, m_rollupState->serialize());
    }
    for (int i = 0; i < m_networks.size(); i++) {
        s.writeBlob(static_cast<quint32>(100 + i), m_networks[i].serialize());
    }

    return s.final();
}

bool CellularSettings::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid() || (d.getVersion() != 1 && d.getVersion() != 2))
    {
        resetToDefaults();
        return false;
    }

    QByteArray bytetmp;

    m_networks.clear();
    if (d.getVersion() == 1)
    {
        // one network
        CellularNetworkSettings n(1);
        readNetwork(d, n, 1);
        m_networks.append(n);
        d.readS32(14, &m_leadMs, 100);
    }
    else
    {
        int count = 0;

        d.readS32(1, &m_leadMs, 100);
        d.readS32(2, &count, 0);
        for (int i = 0; i < count && i < kMaxNetworks; i++)
        {
            CellularNetworkSettings n;

            d.readBlob(static_cast<quint32>(100 + i), &bytetmp);
            if (n.deserialize(bytetmp) && !network(n.m_id)) {
                m_networks.append(n);
            }
        }
    }

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
