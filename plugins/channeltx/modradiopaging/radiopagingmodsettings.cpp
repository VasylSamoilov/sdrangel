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

#include <QColor>

#include <algorithm>
#include <sstream>

#include "util/simpleserializer.h"
#include "settings/serializable.h"
#include "radiopagingflex.h"
#include "radiopagingmodsettings.h"

const int RadioPagingModSettings::m_nbPocsagSpeeds;
const int RadioPagingModSettings::m_pocsagSpeeds[m_nbPocsagSpeeds] = { 512, 1200, 2400 };

RadioPagingModSettings::RadioPagingModSettings() :
    m_channelMarker(nullptr),
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void RadioPagingModSettings::resetToDefaults()
{
    m_inputFrequencyOffset = 0;
    m_protocol = Pocsag;
    m_mode = OneShot;
    m_deviation = 4500.0f;
    m_gain = 0.0f;
    m_channelMute = false;
    m_rampUpMs = 5.0f;
    m_rampDownMs = 5.0f;
    m_rampRange = 60;
    m_pocsagSpeed = 1200;
    m_pocsagInverted = false;
    m_flexMode = (int) RadioPagingFlex::Mode1600_2;
    m_flexInverted = false;
    m_flexCollapse = 0;
    m_flexMixPocsag = false;
    m_pocsagDeviation = 4500.0f;
    m_mixFramesPerMinute = 0;
    m_mixShared = false;
    m_mixSwitchMs[0] = 400;
    m_mixSwitchMs[1] = 200;
    m_mixSwitchMs[2] = 100;

    m_rgbColor = QColor(255, 170, 0).rgb();
    m_title = "Radio Paging Modulator";
    m_streamIndex = 0;
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIDeviceIndex = 0;
    m_reverseAPIChannelIndex = 0;
    m_workspaceIndex = 0;
    m_hidden = false;
}

void RadioPagingModSettings::validate()
{
    // The ranges of the GUI
    m_rampUpMs = std::max(0.0f, std::min(100.0f, m_rampUpMs));
    m_rampDownMs = std::max(0.0f, std::min(100.0f, m_rampDownMs));
    m_rampRange = std::max(0, std::min(120, m_rampRange));
    m_deviation = std::max(500.0f, std::min(10000.0f, m_deviation));
    m_gain = std::max(-60.0f, std::min(0.0f, m_gain));

    if (!isPocsagSpeed(m_pocsagSpeed)) {
        m_pocsagSpeed = 1200;
    }
    if ((m_flexMode < (int) RadioPagingFlex::Mode1600_2) || (m_flexMode > (int) RadioPagingFlex::Mode6400_4)) {
        m_flexMode = (int) RadioPagingFlex::Mode1600_2;
    }

    m_flexCollapse = std::max(0, std::min(RadioPagingFlex::m_maxCollapse, m_flexCollapse));
    m_pocsagDeviation = std::max(500.0f, std::min(10000.0f, m_pocsagDeviation));
    m_mixFramesPerMinute = std::max(0, std::min(m_mixShared ? 128 : 32, m_mixFramesPerMinute));

    for (int i = 0; i < 3; i++) {
        m_mixSwitchMs[i] = std::max(0, std::min(1000, m_mixSwitchMs[i]));
    }
}

float RadioPagingModSettings::defaultDeviation(Protocol protocol)
{
    return (protocol == Flex) ? 4800.0f : 4500.0f;
}

int RadioPagingModSettings::flexModeOf(const QString& name)
{
    for (int mode = (int) RadioPagingFlex::Mode1600_2; mode <= (int) RadioPagingFlex::Mode6400_4; mode++)
    {
        if (name == RadioPagingFlex::modeName((RadioPagingFlex::Mode) mode)) {
            return mode;
        }
    }

    return -1;
}

bool RadioPagingModSettings::isPocsagSpeed(int baud)
{
    for (int i = 0; i < m_nbPocsagSpeeds; i++)
    {
        if (m_pocsagSpeeds[i] == baud) {
            return true;
        }
    }

    return false;
}

QString RadioPagingModSettings::protocolName(Protocol protocol)
{
    switch (protocol)
    {
    case Pocsag:
        return "POCSAG";
    case Flex:
        return "FLEX";
    case Gsc:
        return "GSC";
    }

    return "";
}

// Tags 1 to 14 and 33 to 39 are the modulator settings, 20 to 32 the usual channel tags. New tags are only appended
QByteArray RadioPagingModSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS64(1, m_inputFrequencyOffset);
    s.writeS32(2, (int) m_protocol);
    s.writeS32(3, (int) m_mode);
    s.writeFloat(4, m_deviation);
    s.writeFloat(5, m_gain);
    s.writeBool(6, m_channelMute);
    s.writeFloat(7, m_rampUpMs);
    s.writeFloat(8, m_rampDownMs);
    s.writeS32(9, m_rampRange);
    s.writeS32(10, m_pocsagSpeed);
    s.writeBool(11, m_pocsagInverted);
    s.writeS32(12, m_flexMode);
    s.writeBool(13, m_flexInverted);
    s.writeS32(14, m_flexCollapse);

    s.writeU32(20, m_rgbColor);
    s.writeString(21, m_title);

    if (m_channelMarker) {
        s.writeBlob(22, m_channelMarker->serialize());
    }

    s.writeS32(23, m_streamIndex);
    s.writeBool(24, m_useReverseAPI);
    s.writeString(25, m_reverseAPIAddress);
    s.writeU32(26, m_reverseAPIPort);
    s.writeU32(27, m_reverseAPIDeviceIndex);
    s.writeU32(28, m_reverseAPIChannelIndex);

    if (m_rollupState) {
        s.writeBlob(29, m_rollupState->serialize());
    }

    s.writeS32(30, m_workspaceIndex);
    s.writeBlob(31, m_geometryBytes);
    s.writeBool(32, m_hidden);
    s.writeBool(33, m_flexMixPocsag);
    s.writeFloat(34, m_pocsagDeviation);
    s.writeS32(35, m_mixFramesPerMinute);
    s.writeBool(36, m_mixShared);
    s.writeS32(37, m_mixSwitchMs[0]);
    s.writeS32(38, m_mixSwitchMs[1]);
    s.writeS32(39, m_mixSwitchMs[2]);

    return s.final();
}

bool RadioPagingModSettings::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid())
    {
        resetToDefaults();
        return false;
    }

    if (d.getVersion() == 1)
    {
        QByteArray bytetmp;
        uint32_t utmp;
        int itmp;

        d.readS64(1, &m_inputFrequencyOffset, 0);
        d.readS32(2, &itmp, (int) Pocsag);
        m_protocol = ((itmp >= (int) Pocsag) && (itmp <= (int) Gsc)) ? (Protocol) itmp : Pocsag;
        d.readS32(3, &itmp, (int) OneShot);
        m_mode = ((itmp >= (int) OneShot) && (itmp <= (int) Network)) ? (Mode) itmp : OneShot;
        d.readFloat(4, &m_deviation, 4500.0f);
        d.readFloat(5, &m_gain, 0.0f);
        d.readBool(6, &m_channelMute, false);
        d.readFloat(7, &m_rampUpMs, 5.0f);
        d.readFloat(8, &m_rampDownMs, 5.0f);
        d.readS32(9, &m_rampRange, 60);
        d.readS32(10, &m_pocsagSpeed, 1200);

        if (!isPocsagSpeed(m_pocsagSpeed)) {
            m_pocsagSpeed = 1200;
        }

        d.readBool(11, &m_pocsagInverted, false);
        d.readS32(12, &m_flexMode, (int) RadioPagingFlex::Mode1600_2);
        d.readBool(13, &m_flexInverted, false);
        d.readS32(14, &m_flexCollapse, 0);

        d.readU32(20, &m_rgbColor, QColor(255, 170, 0).rgb());
        d.readString(21, &m_title, "Radio Paging Modulator");

        if (m_channelMarker)
        {
            d.readBlob(22, &bytetmp);
            m_channelMarker->deserialize(bytetmp);
        }

        d.readS32(23, &m_streamIndex, 0);
        d.readBool(24, &m_useReverseAPI, false);
        d.readString(25, &m_reverseAPIAddress, "127.0.0.1");
        d.readU32(26, &utmp, 0);

        if ((utmp > 1023) && (utmp < 65535)) {
            m_reverseAPIPort = utmp;
        } else {
            m_reverseAPIPort = 8888;
        }

        d.readU32(27, &utmp, 0);
        m_reverseAPIDeviceIndex = utmp > 99 ? 99 : utmp;
        d.readU32(28, &utmp, 0);
        m_reverseAPIChannelIndex = utmp > 99 ? 99 : utmp;

        if (m_rollupState)
        {
            d.readBlob(29, &bytetmp);
            m_rollupState->deserialize(bytetmp);
        }

        d.readS32(30, &m_workspaceIndex, 0);
        d.readBlob(31, &m_geometryBytes);
        d.readBool(32, &m_hidden, false);
        d.readBool(33, &m_flexMixPocsag, false);
        d.readFloat(34, &m_pocsagDeviation, 4500.0f);
        d.readS32(35, &m_mixFramesPerMinute, 0);
        d.readBool(36, &m_mixShared, false);
        d.readS32(37, &m_mixSwitchMs[0], 400);
        d.readS32(38, &m_mixSwitchMs[1], 200);
        d.readS32(39, &m_mixSwitchMs[2], 100);

        validate();

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

void RadioPagingModSettings::applySettings(const QStringList& settingsKeys, const RadioPagingModSettings& settings)
{
    if (settingsKeys.contains("inputFrequencyOffset")) {
        m_inputFrequencyOffset = settings.m_inputFrequencyOffset;
    }
    if (settingsKeys.contains("protocol")) {
        m_protocol = settings.m_protocol;
    }
    if (settingsKeys.contains("mode")) {
        m_mode = settings.m_mode;
    }
    if (settingsKeys.contains("deviation")) {
        m_deviation = settings.m_deviation;
    }
    if (settingsKeys.contains("gain")) {
        m_gain = settings.m_gain;
    }
    if (settingsKeys.contains("channelMute")) {
        m_channelMute = settings.m_channelMute;
    }
    if (settingsKeys.contains("rampUpMs")) {
        m_rampUpMs = settings.m_rampUpMs;
    }
    if (settingsKeys.contains("rampDownMs")) {
        m_rampDownMs = settings.m_rampDownMs;
    }
    if (settingsKeys.contains("rampRange")) {
        m_rampRange = settings.m_rampRange;
    }
    if (settingsKeys.contains("pocsagSpeed")) {
        m_pocsagSpeed = settings.m_pocsagSpeed;
    }
    if (settingsKeys.contains("pocsagInverted")) {
        m_pocsagInverted = settings.m_pocsagInverted;
    }
    if (settingsKeys.contains("flexMode")) {
        m_flexMode = settings.m_flexMode;
    }
    if (settingsKeys.contains("flexInverted")) {
        m_flexInverted = settings.m_flexInverted;
    }
    if (settingsKeys.contains("flexCollapse")) {
        m_flexCollapse = settings.m_flexCollapse;
    }
    if (settingsKeys.contains("mixPocsag")) {
        m_flexMixPocsag = settings.m_flexMixPocsag;
    }
    if (settingsKeys.contains("pocsagDeviation")) {
        m_pocsagDeviation = settings.m_pocsagDeviation;
    }
    if (settingsKeys.contains("mixFramesPerMinute")) {
        m_mixFramesPerMinute = settings.m_mixFramesPerMinute;
    }
    if (settingsKeys.contains("mixShared")) {
        m_mixShared = settings.m_mixShared;
    }
    if (settingsKeys.contains("mixSwitchMs512")) {
        m_mixSwitchMs[0] = settings.m_mixSwitchMs[0];
    }
    if (settingsKeys.contains("mixSwitchMs1200")) {
        m_mixSwitchMs[1] = settings.m_mixSwitchMs[1];
    }
    if (settingsKeys.contains("mixSwitchMs2400")) {
        m_mixSwitchMs[2] = settings.m_mixSwitchMs[2];
    }
    if (settingsKeys.contains("rgbColor")) {
        m_rgbColor = settings.m_rgbColor;
    }
    if (settingsKeys.contains("title")) {
        m_title = settings.m_title;
    }
    if (settingsKeys.contains("streamIndex")) {
        m_streamIndex = settings.m_streamIndex;
    }
    if (settingsKeys.contains("useReverseAPI")) {
        m_useReverseAPI = settings.m_useReverseAPI;
    }
    if (settingsKeys.contains("reverseAPIAddress")) {
        m_reverseAPIAddress = settings.m_reverseAPIAddress;
    }
    if (settingsKeys.contains("reverseAPIPort")) {
        m_reverseAPIPort = settings.m_reverseAPIPort;
    }
    if (settingsKeys.contains("reverseAPIDeviceIndex")) {
        m_reverseAPIDeviceIndex = settings.m_reverseAPIDeviceIndex;
    }
    if (settingsKeys.contains("reverseAPIChannelIndex")) {
        m_reverseAPIChannelIndex = settings.m_reverseAPIChannelIndex;
    }
    if (settingsKeys.contains("workspaceIndex")) {
        m_workspaceIndex = settings.m_workspaceIndex;
    }
    if (settingsKeys.contains("hidden")) {
        m_hidden = settings.m_hidden;
    }
}

QString RadioPagingModSettings::getDebugString(const QStringList& settingsKeys, bool force) const
{
    std::ostringstream ostr;

    if (settingsKeys.contains("inputFrequencyOffset") || force) {
        ostr << " m_inputFrequencyOffset: " << m_inputFrequencyOffset;
    }
    if (settingsKeys.contains("protocol") || force) {
        ostr << " m_protocol: " << (int) m_protocol;
    }
    if (settingsKeys.contains("mode") || force) {
        ostr << " m_mode: " << (int) m_mode;
    }
    if (settingsKeys.contains("deviation") || force) {
        ostr << " m_deviation: " << m_deviation;
    }
    if (settingsKeys.contains("gain") || force) {
        ostr << " m_gain: " << m_gain;
    }
    if (settingsKeys.contains("channelMute") || force) {
        ostr << " m_channelMute: " << m_channelMute;
    }
    if (settingsKeys.contains("rampUpMs") || force) {
        ostr << " m_rampUpMs: " << m_rampUpMs;
    }
    if (settingsKeys.contains("rampDownMs") || force) {
        ostr << " m_rampDownMs: " << m_rampDownMs;
    }
    if (settingsKeys.contains("rampRange") || force) {
        ostr << " m_rampRange: " << m_rampRange;
    }
    if (settingsKeys.contains("pocsagSpeed") || force) {
        ostr << " m_pocsagSpeed: " << m_pocsagSpeed;
    }
    if (settingsKeys.contains("pocsagInverted") || force) {
        ostr << " m_pocsagInverted: " << m_pocsagInverted;
    }
    if (settingsKeys.contains("flexMode") || force) {
        ostr << " m_flexMode: " << m_flexMode;
    }
    if (settingsKeys.contains("flexInverted") || force) {
        ostr << " m_flexInverted: " << m_flexInverted;
    }
    if (settingsKeys.contains("flexCollapse") || force) {
        ostr << " m_flexCollapse: " << m_flexCollapse;
    }
    if (settingsKeys.contains("mixPocsag") || force) {
        ostr << " m_flexMixPocsag: " << m_flexMixPocsag;
    }
    if (settingsKeys.contains("pocsagDeviation") || force) {
        ostr << " m_pocsagDeviation: " << m_pocsagDeviation;
    }
    if (settingsKeys.contains("mixFramesPerMinute") || force) {
        ostr << " m_mixFramesPerMinute: " << m_mixFramesPerMinute;
    }
    if (settingsKeys.contains("mixShared") || force) {
        ostr << " m_mixShared: " << m_mixShared;
    }
    if (settingsKeys.contains("mixSwitchMs512") || force) {
        ostr << " m_mixSwitchMs512: " << m_mixSwitchMs[0];
    }
    if (settingsKeys.contains("mixSwitchMs1200") || force) {
        ostr << " m_mixSwitchMs1200: " << m_mixSwitchMs[1];
    }
    if (settingsKeys.contains("mixSwitchMs2400") || force) {
        ostr << " m_mixSwitchMs2400: " << m_mixSwitchMs[2];
    }
    if (settingsKeys.contains("rgbColor") || force) {
        ostr << " m_rgbColor: " << m_rgbColor;
    }
    if (settingsKeys.contains("title") || force) {
        ostr << " m_title: " << m_title.toStdString();
    }
    if (settingsKeys.contains("streamIndex") || force) {
        ostr << " m_streamIndex: " << m_streamIndex;
    }
    if (settingsKeys.contains("useReverseAPI") || force) {
        ostr << " m_useReverseAPI: " << m_useReverseAPI;
    }
    if (settingsKeys.contains("reverseAPIAddress") || force) {
        ostr << " m_reverseAPIAddress: " << m_reverseAPIAddress.toStdString();
    }
    if (settingsKeys.contains("reverseAPIPort") || force) {
        ostr << " m_reverseAPIPort: " << m_reverseAPIPort;
    }
    if (settingsKeys.contains("reverseAPIDeviceIndex") || force) {
        ostr << " m_reverseAPIDeviceIndex: " << m_reverseAPIDeviceIndex;
    }
    if (settingsKeys.contains("reverseAPIChannelIndex") || force) {
        ostr << " m_reverseAPIChannelIndex: " << m_reverseAPIChannelIndex;
    }
    if (settingsKeys.contains("workspaceIndex") || force) {
        ostr << " m_workspaceIndex: " << m_workspaceIndex;
    }
    if (settingsKeys.contains("hidden") || force) {
        ostr << " m_hidden: " << m_hidden;
    }

    return QString(ostr.str().c_str());
}
