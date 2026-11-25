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
    d.readString(first + 5, &n.m_carriers, "1:cctc, 5:tc");
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
    m_y1 = -1;
    m_areaNo = 0;
    m_msPower = 1;
    m_carriers = "1:cctc, 5:tc";
    m_compandor = true;
    m_sendCallerId = false;
    m_sendClock = false;
    m_smscNumber = "";
    m_smsCodepage = "";
    m_sid = 0;
    m_dcc = 0;
    m_scc = 0;
    m_fallback = FallbackEcho;
    m_farEnd = CellularFarEnd();
    m_rxDeviceSetIndex = 0;
    m_txDeviceSetIndex = 1;
}

bool CellularFarEnd::operator==(const CellularFarEnd& o) const
{
    return m_echoDelayMs == o.m_echoDelayMs && m_echoLevelDb == o.m_echoLevelDb && m_echoAnnounce == o.m_echoAnnounce
        && m_musicSource == o.m_musicSource && m_musicFile == o.m_musicFile && m_musicInput == o.m_musicInput
        && m_musicLevelDb == o.m_musicLevelDb && m_musicLoop == o.m_musicLoop && m_callLimitS == o.m_callLimitS;
}

QByteArray CellularNetworkSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS32(1, m_id);
    s.writeString(2, m_name);
    s.writeBool(3, m_enabled);
    s.writeS32(4, m_y1);
    writeNetwork(s, *this, 10);
    // the far ends (keys 40..)
    s.writeS32(40, m_farEnd.m_echoDelayMs);
    s.writeS32(41, m_farEnd.m_echoLevelDb);
    s.writeBool(42, m_farEnd.m_echoAnnounce);
    s.writeS32(43, m_farEnd.m_musicSource);
    s.writeString(44, m_farEnd.m_musicFile);
    s.writeString(45, m_farEnd.m_musicInput);
    s.writeS32(46, m_farEnd.m_musicLevelDb);
    s.writeBool(47, m_farEnd.m_musicLoop);
    s.writeS32(48, m_farEnd.m_callLimitS);
    s.writeString(49, m_smsCodepage);
    // AMPS (keys 50..)
    s.writeS32(50, m_sid);
    s.writeS32(51, m_dcc);
    s.writeS32(52, m_scc);
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
    d.readS32(4, &m_y1, -1);
    readNetwork(d, *this, 10);
    const CellularFarEnd defaults;
    d.readS32(40, &m_farEnd.m_echoDelayMs, defaults.m_echoDelayMs);
    d.readS32(41, &m_farEnd.m_echoLevelDb, defaults.m_echoLevelDb);
    d.readBool(42, &m_farEnd.m_echoAnnounce, defaults.m_echoAnnounce);
    d.readS32(43, &m_farEnd.m_musicSource, defaults.m_musicSource);
    d.readString(44, &m_farEnd.m_musicFile, "");
    d.readString(45, &m_farEnd.m_musicInput, "");
    d.readS32(46, &m_farEnd.m_musicLevelDb, defaults.m_musicLevelDb);
    d.readBool(47, &m_farEnd.m_musicLoop, defaults.m_musicLoop);
    d.readS32(48, &m_farEnd.m_callLimitS, defaults.m_callLimitS);
    d.readString(49, &m_smsCodepage, "");
    d.readS32(50, &m_sid, 0);
    d.readS32(51, &m_dcc, 0);
    d.readS32(52, &m_scc, 0);
    return m_id >= 1 && m_id <= kMaxNetworkId;
}

bool CellularNetworkSettings::sameNetwork(const CellularNetworkSettings& o) const
{
    return m_id == o.m_id && m_system == o.m_system && m_country == o.m_country && m_area == o.m_area
        && m_y1 == o.m_y1 && m_areaNo == o.m_areaNo && m_msPower == o.m_msPower && m_carriers == o.m_carriers
        && m_compandor == o.m_compandor && m_sendCallerId == o.m_sendCallerId && m_sendClock == o.m_sendClock
        && m_smscNumber == o.m_smscNumber && m_smsCodepage == o.m_smsCodepage && m_fallback == o.m_fallback && m_farEnd == o.m_farEnd
        && m_sid == o.m_sid && m_dcc == o.m_dcc && m_scc == o.m_scc
        && m_rxDeviceSetIndex == o.m_rxDeviceSetIndex && m_txDeviceSetIndex == o.m_txDeviceSetIndex;
}

bool CellularNetworkSettings::parseCarriers(const QString& text, std::vector<modemnmt::NmtCarrierConfig>& carriers, QString& error)
{
    carriers.clear();
    bool anySupervisory = false;
    QString untoned;                    // the first traffic channel without a tone given
    const auto isTraffic = [](modemnmt::ChannelType type) {
        return type == modemnmt::ChannelType::Tc || type == modemnmt::ChannelType::AcTc || type == modemnmt::ChannelType::CcTc;
    };

    for (const QString& item : text.split(',', Qt::SkipEmptyParts))
    {
        const QStringList parts = item.trimmed().split(':');
        modemnmt::NmtCarrierConfig c;
        bool ok = false;

        if (parts.isEmpty() || parts.size() > 3)
        {
            error = QString("\"%1\": channel:type[:supervisory]").arg(item.trimmed());
            return false;
        }
        c.channel = parts[0].trimmed().toInt(&ok);
        if (!ok || c.channel <= 0 || c.channel >= modemnmt::NmtBaseStation::kIdStride)
        {
            error = QString("\"%1\": the channel is not a number").arg(item.trimmed());
            return false;
        }
        if (parts.size() < 2 || parts[1].trimmed().isEmpty())
        {
            error = QString("\"%1\": give the type of the channel (cc, cca, ccb, tc, actc, cctc or test), e.g. %2:tc")
                .arg(item.trimmed()).arg(c.channel);
            return false;
        }
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
            anySupervisory = true;
            c.supervisory = parts[2].trimmed().toInt(&ok);
            if (!ok || c.supervisory < 0 || c.supervisory > 4)
            {
                error = QString("\"%1\": the supervisory tone is 1..4").arg(item.trimmed());
                return false;
            }
        }
        else if (isTraffic(c.type))
        {
            untoned = untoned.isEmpty() ? item.trimmed() : untoned;
        }
        carriers.push_back(c);
    }

    if (carriers.empty())
    {
        error = "no carriers";
        return false;
    }
    // every call is supervised by a tone: the tones are given for all traffic channels (0 for
    // none) or for none of them
    if (anySupervisory && !untoned.isEmpty())
    {
        error = QString("\"%1\": give the supervisory tone of every traffic channel (1..4, 0 for none), or of none").arg(untoned);
        return false;
    }

    // the supervisory tones go round 1..4 on the channels that carry calls, as in the reference,
    // when none is given
    if (!anySupervisory)
    {
        int next = 0;
        for (modemnmt::NmtCarrierConfig& c : carriers)
        {
            // the tone is for channels that carry calls
            if (isTraffic(c.type)) {
                c.supervisory = (next++ % 4) + 1;
            }
        }
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
    config.y1 = m_y1;
    config.areaNo = m_areaNo;
    config.msPower = m_msPower;
    config.compandor = m_compandor;
    config.sendCallerId = m_sendCallerId;
    config.sendClock = m_sendClock;
    config.smscNumber = m_smscNumber.trimmed().toStdString();

    return parseCarriers(m_carriers, config.carriers, error);
}

bool CellularNetworkSettings::parseAmpsCarriers(const QString& text, int scc, std::vector<modemamps::AmpsCarrierConfig>& carriers,
    QString& error)
{
    carriers.clear();

    for (const QString& item : text.split(',', Qt::SkipEmptyParts))
    {
        const QStringList parts = item.trimmed().split(':');
        modemamps::AmpsCarrierConfig c;
        bool ok = false;

        if (parts.size() < 2 || parts.size() > 3)
        {
            error = QString("\"%1\": channel:type[:scc], the type cc, vc or ccvc, e.g. 353:cc").arg(item.trimmed());
            return false;
        }
        c.channel = parts[0].trimmed().toInt(&ok);
        if (!ok || c.channel <= 0 || c.channel >= modemamps::AmpsBaseStation::kIdStride)
        {
            error = QString("\"%1\": the channel is not a number").arg(item.trimmed());
            return false;
        }
        const QString type = parts[1].trimmed().toLower();
        if (type == "cc") {
            c.type = modemamps::AmpsChannelType::ControlPaging;
        } else if (type == "vc") {
            c.type = modemamps::AmpsChannelType::Voice;
        } else if (type == "ccvc") {
            c.type = modemamps::AmpsChannelType::ControlPagingVoice;
        }
        else
        {
            error = QString("\"%1\": the type is cc (control and paging), vc (voice) or ccvc (both, one transceiver)").arg(item.trimmed());
            return false;
        }
        c.scc = scc;
        if (parts.size() > 2)
        {
            c.scc = parts[2].trimmed().toInt(&ok);
            if (!ok || c.scc < 0 || c.scc > 2)
            {
                error = QString("\"%1\": the SAT colour code is 0..2").arg(item.trimmed());
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

bool CellularNetworkSettings::ampsNetworkConfig(modemamps::AmpsNetworkConfig& config, QString& error) const
{
    config = modemamps::AmpsNetworkConfig();
    config.id = m_id;
    config.sid = static_cast<uint16_t>(std::max(0, std::min(m_sid, 32767)));
    config.dcc = std::max(0, std::min(m_dcc, 3));
    config.msPower = std::max(0, std::min(m_msPower, 7));
    config.sendCallerId = m_sendCallerId;
    if (m_scc < 0 || m_scc > 2)
    {
        error = "the SAT colour code is 0..2";
        return false;
    }
    return parseAmpsCarriers(m_carriers, m_scc, config.carriers, error);
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
    m_logEnabled = false;
    m_logFilename = "cellular_log.csv";
    m_console = CellularConsole();
    m_title = "Cellular";
    m_rgbColor = QColor(0, 160, 120).rgb();
    m_workspaceIndex = 0;
    m_networkColumnIndexes.clear();
    m_networkColumnSizes.clear();
    m_carrierColumnIndexes.clear();
    m_carrierColumnSizes.clear();
    for (int i = 0; i < kNetworkColumns; i++)
    {
        m_networkColumnIndexes.append(i);
        m_networkColumnSizes.append(-1);
    }
    for (int i = 0; i < kCarrierColumns; i++)
    {
        m_carrierColumnIndexes.append(i);
        m_carrierColumnSizes.append(-1);
    }
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
    s.writeBool(3, m_logEnabled);
    s.writeString(4, m_logFilename);
    // the console (keys 5..10)
    s.writeString(5, m_console.m_number);
    s.writeString(6, m_console.m_speaker);
    s.writeString(7, m_console.m_microphone);
    s.writeS32(8, m_console.m_volumeDb);
    s.writeS32(9, m_console.m_micGainDb);
    s.writeBool(10, m_console.m_autoAnswer);
    s.writeBool(11, m_console.m_speakerMute);
    s.writeBool(12, m_console.m_micMute);
    s.writeString(20, m_title);
    s.writeU32(21, m_rgbColor);
    s.writeS32(22, m_workspaceIndex);
    s.writeBlob(23, m_geometryBytes);
    s.writeBlob(25, m_book);
    if (m_rollupState) {
        s.writeBlob(24, m_rollupState->serialize());
    }
    for (int i = 0; i < m_networks.size(); i++) {
        s.writeBlob(static_cast<quint32>(100 + i), m_networks[i].serialize());
    }
    for (int i = 0; i < kNetworkColumns; i++)
    {
        s.writeS32(static_cast<quint32>(30 + i), m_networkColumnIndexes[i]);
        s.writeS32(static_cast<quint32>(40 + i), m_networkColumnSizes[i]);
    }
    for (int i = 0; i < kCarrierColumns; i++)
    {
        s.writeS32(static_cast<quint32>(50 + i), m_carrierColumnIndexes[i]);
        s.writeS32(static_cast<quint32>(60 + i), m_carrierColumnSizes[i]);
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
        m_logEnabled = false;
        m_logFilename = "cellular_log.csv";
    }
    else
    {
        int count = 0;

        d.readS32(1, &m_leadMs, 100);
        d.readS32(2, &count, 0);
        d.readBool(3, &m_logEnabled, false);
        d.readString(4, &m_logFilename, "cellular_log.csv");
        const CellularConsole console;
        d.readString(5, &m_console.m_number, console.m_number);
        d.readString(6, &m_console.m_speaker, "");
        d.readString(7, &m_console.m_microphone, "");
        d.readS32(8, &m_console.m_volumeDb, console.m_volumeDb);
        d.readS32(9, &m_console.m_micGainDb, console.m_micGainDb);
        d.readBool(10, &m_console.m_autoAnswer, console.m_autoAnswer);
        d.readBool(11, &m_console.m_speakerMute, console.m_speakerMute);
        d.readBool(12, &m_console.m_micMute, console.m_micMute);
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
    d.readBlob(25, &m_book);
    for (int i = 0; i < kNetworkColumns; i++)
    {
        d.readS32(static_cast<quint32>(30 + i), &m_networkColumnIndexes[i], i);
        d.readS32(static_cast<quint32>(40 + i), &m_networkColumnSizes[i], -1);
    }
    for (int i = 0; i < kCarrierColumns; i++)
    {
        d.readS32(static_cast<quint32>(50 + i), &m_carrierColumnIndexes[i], i);
        d.readS32(static_cast<quint32>(60 + i), &m_carrierColumnSizes[i], -1);
    }
    if (m_rollupState)
    {
        d.readBlob(24, &bytetmp);
        m_rollupState->deserialize(bytetmp);
    }

    return true;
}
