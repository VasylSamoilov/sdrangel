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

#include <sstream>

#include "util/simpleserializer.h"
#include "settings/serializable.h"
#include "audio/audiodevicemanager.h"
#include "radiopagingdemodsettings.h"

const int RadioPagingDemodSettings::m_nbPocsagSpeeds;
const int RadioPagingDemodSettings::m_pocsagSpeeds[m_nbPocsagSpeeds] = { 512, 1200, 2400 };
const int RadioPagingDemodSettings::m_channelSampleRate;
// Leg configuration, fixed (the sink test compares others): carrier offset room on each side
// of a leg, and legs following the measured carrier offset
double RadioPagingDemodSettings::m_legOffsetAllowance = 2000.0;
bool RadioPagingDemodSettings::m_legAfc = true;

namespace {

// Highest modulation rate of the legs (Carson bandwidth), bits per second
const int flexMaxRate = 3200;
const int gscRate = 600;


} // namespace

RadioPagingDemodSettings::RadioPagingDemodSettings() :
    m_channelMarker(nullptr),
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void RadioPagingDemodSettings::resetToDefaults()
{
    m_inputFrequencyOffset = 0;
    m_rfBandwidth = 20000.0f;           // FLEX 16 kHz and room for the oscillator offsets
    m_pocsagEnabled = true;
    m_pocsagDeviation = 4500.0f;
    m_flexEnabled = true;
    m_flexDeviation = 4800.0f;
    m_gscEnabled = true;
    m_gscDeviation = 4500.0f;
    m_gscVoiceTimeout = 50;
    m_gscAudio = false;
    m_volume = 1.0f;
    m_audioDeviceName = AudioDeviceManager::m_defaultDeviceName;

    m_rgbColor = QColor(255, 170, 0).rgb();
    m_title = "Radio Paging Demodulator";
    m_streamIndex = 0;
    m_useReverseAPI = false;
    m_reverseAPIAddress = "127.0.0.1";
    m_reverseAPIPort = 8888;
    m_reverseAPIDeviceIndex = 0;
    m_reverseAPIChannelIndex = 0;
    m_workspaceIndex = 0;
    m_hidden = false;
}

// Tags 12 to 20 and 27 to 30 are the usual channel tags, 21 the audio device. New tags are only appended
QByteArray RadioPagingDemodSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeS32(1, m_inputFrequencyOffset);
    s.writeFloat(2, m_rfBandwidth);
    s.writeBool(3, m_pocsagEnabled);
    s.writeFloat(4, m_pocsagDeviation);
    s.writeBool(5, m_flexEnabled);
    s.writeFloat(6, m_flexDeviation);
    s.writeBool(7, m_gscEnabled);
    s.writeFloat(8, m_gscDeviation);
    s.writeS32(9, m_gscVoiceTimeout);
    s.writeBool(10, m_gscAudio);
    s.writeFloat(11, m_volume);
    s.writeU32(12, m_rgbColor);
    s.writeString(13, m_title);

    if (m_channelMarker) {
        s.writeBlob(14, m_channelMarker->serialize());
    }

    s.writeS32(15, m_streamIndex);
    s.writeBool(16, m_useReverseAPI);
    s.writeString(17, m_reverseAPIAddress);
    s.writeU32(18, m_reverseAPIPort);
    s.writeU32(19, m_reverseAPIDeviceIndex);
    s.writeU32(20, m_reverseAPIChannelIndex);
    s.writeString(21, m_audioDeviceName);

    if (m_rollupState) {
        s.writeBlob(27, m_rollupState->serialize());
    }

    s.writeS32(28, m_workspaceIndex);
    s.writeBlob(29, m_geometryBytes);
    s.writeBool(30, m_hidden);

    return s.final();
}

bool RadioPagingDemodSettings::deserialize(const QByteArray& data)
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

        d.readS32(1, &m_inputFrequencyOffset, 0);
        d.readFloat(2, &m_rfBandwidth, 20000.0f);
        d.readBool(3, &m_pocsagEnabled, true);
        d.readFloat(4, &m_pocsagDeviation, 4500.0f);
        d.readBool(5, &m_flexEnabled, true);
        d.readFloat(6, &m_flexDeviation, 4800.0f);
        d.readBool(7, &m_gscEnabled, true);
        d.readFloat(8, &m_gscDeviation, 4500.0f);
        d.readS32(9, &m_gscVoiceTimeout, 50);
        d.readBool(10, &m_gscAudio, false);
        d.readFloat(11, &m_volume, 1.0f);
        d.readU32(12, &m_rgbColor, QColor(255, 170, 0).rgb());
        d.readString(13, &m_title, "Radio Paging Demodulator");

        if (m_channelMarker)
        {
            d.readBlob(14, &bytetmp);
            m_channelMarker->deserialize(bytetmp);
        }

        d.readS32(15, &m_streamIndex, 0);
        d.readBool(16, &m_useReverseAPI, false);
        d.readString(17, &m_reverseAPIAddress, "127.0.0.1");
        d.readU32(18, &utmp, 0);

        if ((utmp > 1023) && (utmp < 65535)) {
            m_reverseAPIPort = utmp;
        } else {
            m_reverseAPIPort = 8888;
        }

        d.readU32(19, &utmp, 0);
        m_reverseAPIDeviceIndex = utmp > 99 ? 99 : utmp;
        d.readU32(20, &utmp, 0);
        m_reverseAPIChannelIndex = utmp > 99 ? 99 : utmp;
        d.readString(21, &m_audioDeviceName, AudioDeviceManager::m_defaultDeviceName);

        if (m_rollupState)
        {
            d.readBlob(27, &bytetmp);
            m_rollupState->deserialize(bytetmp);
        }

        d.readS32(28, &m_workspaceIndex, 0);
        d.readBlob(29, &m_geometryBytes);
        d.readBool(30, &m_hidden, false);

        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

void RadioPagingDemodSettings::applySettings(const QStringList& settingsKeys, const RadioPagingDemodSettings& settings)
{
    if (settingsKeys.contains("inputFrequencyOffset")) {
        m_inputFrequencyOffset = settings.m_inputFrequencyOffset;
    }
    if (settingsKeys.contains("rfBandwidth")) {
        m_rfBandwidth = settings.m_rfBandwidth;
    }
    if (settingsKeys.contains("pocsagEnabled")) {
        m_pocsagEnabled = settings.m_pocsagEnabled;
    }
    if (settingsKeys.contains("pocsagDeviation")) {
        m_pocsagDeviation = settings.m_pocsagDeviation;
    }
    if (settingsKeys.contains("flexEnabled")) {
        m_flexEnabled = settings.m_flexEnabled;
    }
    if (settingsKeys.contains("flexDeviation")) {
        m_flexDeviation = settings.m_flexDeviation;
    }
    if (settingsKeys.contains("gscEnabled")) {
        m_gscEnabled = settings.m_gscEnabled;
    }
    if (settingsKeys.contains("gscDeviation")) {
        m_gscDeviation = settings.m_gscDeviation;
    }
    if (settingsKeys.contains("gscVoiceTimeout")) {
        m_gscVoiceTimeout = settings.m_gscVoiceTimeout;
    }
    if (settingsKeys.contains("gscAudio")) {
        m_gscAudio = settings.m_gscAudio;
    }
    if (settingsKeys.contains("volume")) {
        m_volume = settings.m_volume;
    }
    if (settingsKeys.contains("audioDeviceName")) {
        m_audioDeviceName = settings.m_audioDeviceName;
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

QString RadioPagingDemodSettings::getDebugString(const QStringList& settingsKeys, bool force) const
{
    std::ostringstream ostr;

    if (settingsKeys.contains("inputFrequencyOffset") || force) {
        ostr << " m_inputFrequencyOffset: " << m_inputFrequencyOffset;
    }
    if (settingsKeys.contains("rfBandwidth") || force) {
        ostr << " m_rfBandwidth: " << m_rfBandwidth;
    }
    if (settingsKeys.contains("pocsagEnabled") || force) {
        ostr << " m_pocsagEnabled: " << m_pocsagEnabled;
    }
    if (settingsKeys.contains("pocsagDeviation") || force) {
        ostr << " m_pocsagDeviation: " << m_pocsagDeviation;
    }
    if (settingsKeys.contains("flexEnabled") || force) {
        ostr << " m_flexEnabled: " << m_flexEnabled;
    }
    if (settingsKeys.contains("flexDeviation") || force) {
        ostr << " m_flexDeviation: " << m_flexDeviation;
    }
    if (settingsKeys.contains("gscEnabled") || force) {
        ostr << " m_gscEnabled: " << m_gscEnabled;
    }
    if (settingsKeys.contains("gscDeviation") || force) {
        ostr << " m_gscDeviation: " << m_gscDeviation;
    }
    if (settingsKeys.contains("gscVoiceTimeout") || force) {
        ostr << " m_gscVoiceTimeout: " << m_gscVoiceTimeout;
    }
    if (settingsKeys.contains("gscAudio") || force) {
        ostr << " m_gscAudio: " << m_gscAudio;
    }
    if (settingsKeys.contains("volume") || force) {
        ostr << " m_volume: " << m_volume;
    }
    if (settingsKeys.contains("audioDeviceName") || force) {
        ostr << " m_audioDeviceName: " << m_audioDeviceName.toStdString();
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

double RadioPagingDemodSettings::pocsagCarson() const
{
    return 2.0 * (m_pocsagDeviation + m_pocsagSpeeds[m_nbPocsagSpeeds - 1]);
}

double RadioPagingDemodSettings::flexCarson() const
{
    return 2.0 * (m_flexDeviation + flexMaxRate);
}

double RadioPagingDemodSettings::gscCarson() const
{
    return 2.0 * (m_gscDeviation + gscRate);
}

// Hunting, the legs pass the Carson bandwidth and the carrier offset on each side: a signal
// off centre is not cut on one side, which would bias the measured DC offset and lose symbols
double RadioPagingDemodSettings::pocsagBandwidth() const
{
    // All speeds share the leg: sized to the fastest
    return 2.0 * (m_pocsagDeviation + m_pocsagSpeeds[m_nbPocsagSpeeds - 1] + m_legOffsetAllowance);
}

double RadioPagingDemodSettings::flexBandwidth() const
{
    return 2.0 * (m_flexDeviation + flexMaxRate + m_legOffsetAllowance);
}

double RadioPagingDemodSettings::gscBandwidth() const
{
    return 2.0 * (m_gscDeviation + gscRate + m_legOffsetAllowance);
}
