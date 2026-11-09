// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGRadioPagingDemodSettings.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGRadioPagingDemodSettings::SWGRadioPagingDemodSettings() = default;
SWGRadioPagingDemodSettings::SWGRadioPagingDemodSettings(QString* json) { init(); if (json) { fromJson(*json); } }
SWGRadioPagingDemodSettings::~SWGRadioPagingDemodSettings() { cleanup(); }
void SWGRadioPagingDemodSettings::cleanup()
{
    delete m_title; m_title = nullptr;
    delete m_reverseAPIAddress; m_reverseAPIAddress = nullptr;
    delete m_channelMarker; m_channelMarker = nullptr;
    delete m_rollupState; m_rollupState = nullptr;
}
void SWGRadioPagingDemodSettings::init()
{
    cleanup();
    m_inputFrequencyOffset = 0; m_inputFrequencyOffsetIsSet = false;
    m_rfBandwidth = 0; m_rfBandwidthIsSet = false;
    m_pocsagDeviation = 0; m_pocsagDeviationIsSet = false;
    m_flexDeviation = 0; m_flexDeviationIsSet = false;
    m_gscDeviation = 0; m_gscDeviationIsSet = false;
    m_gscNonBatterySaver = 0; m_gscNonBatterySaverIsSet = false;
    m_gscVoiceTimeout = 0; m_gscVoiceTimeoutIsSet = false;
    m_rgbColor = 0; m_rgbColorIsSet = false;
    m_title = new QString(); m_titleIsSet = false;
    m_streamIndex = 0; m_streamIndexIsSet = false;
    m_useReverseAPI = 0; m_useReverseAPIIsSet = false;
    m_reverseAPIAddress = new QString(); m_reverseAPIAddressIsSet = false;
    m_reverseAPIPort = 0; m_reverseAPIPortIsSet = false;
    m_reverseAPIDeviceIndex = 0; m_reverseAPIDeviceIndexIsSet = false;
    m_reverseAPIChannelIndex = 0; m_reverseAPIChannelIndexIsSet = false;
    m_channelMarker = new SWGChannelMarker(); m_channelMarkerIsSet = false;
    m_rollupState = new SWGRollupState(); m_rollupStateIsSet = false;
}
SWGRadioPagingDemodSettings* SWGRadioPagingDemodSettings::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGRadioPagingDemodSettings::fromJsonObject(QJsonObject& json)
{
    if (json.contains("inputFrequencyOffset")) { ::SWGSDRangel::setValue(&m_inputFrequencyOffset,json["inputFrequencyOffset"],"qint64",""); m_inputFrequencyOffsetIsSet = true; }
    if (json.contains("rfBandwidth")) { ::SWGSDRangel::setValue(&m_rfBandwidth,json["rfBandwidth"],"float",""); m_rfBandwidthIsSet = true; }
    if (json.contains("pocsagDeviation")) { ::SWGSDRangel::setValue(&m_pocsagDeviation,json["pocsagDeviation"],"float",""); m_pocsagDeviationIsSet = true; }
    if (json.contains("flexDeviation")) { ::SWGSDRangel::setValue(&m_flexDeviation,json["flexDeviation"],"float",""); m_flexDeviationIsSet = true; }
    if (json.contains("gscDeviation")) { ::SWGSDRangel::setValue(&m_gscDeviation,json["gscDeviation"],"float",""); m_gscDeviationIsSet = true; }
    if (json.contains("gscNonBatterySaver")) { ::SWGSDRangel::setValue(&m_gscNonBatterySaver,json["gscNonBatterySaver"],"qint32",""); m_gscNonBatterySaverIsSet = true; }
    if (json.contains("gscVoiceTimeout")) { ::SWGSDRangel::setValue(&m_gscVoiceTimeout,json["gscVoiceTimeout"],"qint32",""); m_gscVoiceTimeoutIsSet = true; }
    if (json.contains("rgbColor")) { ::SWGSDRangel::setValue(&m_rgbColor,json["rgbColor"],"qint32",""); m_rgbColorIsSet = true; }
    if (json.contains("title")) { ::SWGSDRangel::setValue(&m_title,json["title"],"QString","QString"); m_titleIsSet = true; }
    if (json.contains("streamIndex")) { ::SWGSDRangel::setValue(&m_streamIndex,json["streamIndex"],"qint32",""); m_streamIndexIsSet = true; }
    if (json.contains("useReverseAPI")) { ::SWGSDRangel::setValue(&m_useReverseAPI,json["useReverseAPI"],"qint32",""); m_useReverseAPIIsSet = true; }
    if (json.contains("reverseAPIAddress")) { ::SWGSDRangel::setValue(&m_reverseAPIAddress,json["reverseAPIAddress"],"QString","QString"); m_reverseAPIAddressIsSet = true; }
    if (json.contains("reverseAPIPort")) { ::SWGSDRangel::setValue(&m_reverseAPIPort,json["reverseAPIPort"],"qint32",""); m_reverseAPIPortIsSet = true; }
    if (json.contains("reverseAPIDeviceIndex")) { ::SWGSDRangel::setValue(&m_reverseAPIDeviceIndex,json["reverseAPIDeviceIndex"],"qint32",""); m_reverseAPIDeviceIndexIsSet = true; }
    if (json.contains("reverseAPIChannelIndex")) { ::SWGSDRangel::setValue(&m_reverseAPIChannelIndex,json["reverseAPIChannelIndex"],"qint32",""); m_reverseAPIChannelIndexIsSet = true; }
    if (json.contains("channelMarker")) { ::SWGSDRangel::setValue(&m_channelMarker,json["channelMarker"],"SWGChannelMarker","SWGChannelMarker"); m_channelMarkerIsSet = true; }
    if (json.contains("rollupState")) { ::SWGSDRangel::setValue(&m_rollupState,json["rollupState"],"SWGRollupState","SWGRollupState"); m_rollupStateIsSet = true; }
}
QString SWGRadioPagingDemodSettings::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGRadioPagingDemodSettings::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_inputFrequencyOffsetIsSet) { object->insert("inputFrequencyOffset",QJsonValue(m_inputFrequencyOffset)); }
    if (m_rfBandwidthIsSet) { object->insert("rfBandwidth",QJsonValue(m_rfBandwidth)); }
    if (m_pocsagDeviationIsSet) { object->insert("pocsagDeviation",QJsonValue(m_pocsagDeviation)); }
    if (m_flexDeviationIsSet) { object->insert("flexDeviation",QJsonValue(m_flexDeviation)); }
    if (m_gscDeviationIsSet) { object->insert("gscDeviation",QJsonValue(m_gscDeviation)); }
    if (m_gscNonBatterySaverIsSet) { object->insert("gscNonBatterySaver",QJsonValue(m_gscNonBatterySaver)); }
    if (m_gscVoiceTimeoutIsSet) { object->insert("gscVoiceTimeout",QJsonValue(m_gscVoiceTimeout)); }
    if (m_rgbColorIsSet) { object->insert("rgbColor",QJsonValue(m_rgbColor)); }
    if (m_title && (m_titleIsSet || !m_title->isEmpty())) { toJsonValue(QString("title"),m_title,object,QString("QString")); }
    if (m_streamIndexIsSet) { object->insert("streamIndex",QJsonValue(m_streamIndex)); }
    if (m_useReverseAPIIsSet) { object->insert("useReverseAPI",QJsonValue(m_useReverseAPI)); }
    if (m_reverseAPIAddress && (m_reverseAPIAddressIsSet || !m_reverseAPIAddress->isEmpty())) { toJsonValue(QString("reverseAPIAddress"),m_reverseAPIAddress,object,QString("QString")); }
    if (m_reverseAPIPortIsSet) { object->insert("reverseAPIPort",QJsonValue(m_reverseAPIPort)); }
    if (m_reverseAPIDeviceIndexIsSet) { object->insert("reverseAPIDeviceIndex",QJsonValue(m_reverseAPIDeviceIndex)); }
    if (m_reverseAPIChannelIndexIsSet) { object->insert("reverseAPIChannelIndex",QJsonValue(m_reverseAPIChannelIndex)); }
    if (m_channelMarker && (m_channelMarkerIsSet || m_channelMarker->isSet())) { toJsonValue(QString("channelMarker"),m_channelMarker,object,QString("SWGChannelMarker")); }
    if (m_rollupState && (m_rollupStateIsSet || m_rollupState->isSet())) { toJsonValue(QString("rollupState"),m_rollupState,object,QString("SWGRollupState")); }
    return object;
}
bool SWGRadioPagingDemodSettings::isSet()
{
    return m_inputFrequencyOffsetIsSet || m_rfBandwidthIsSet || m_pocsagDeviationIsSet || m_flexDeviationIsSet || m_gscDeviationIsSet || m_gscNonBatterySaverIsSet || m_gscVoiceTimeoutIsSet || m_rgbColorIsSet || m_titleIsSet || m_streamIndexIsSet || m_useReverseAPIIsSet || m_reverseAPIAddressIsSet || m_reverseAPIPortIsSet || m_reverseAPIDeviceIndexIsSet || m_reverseAPIChannelIndexIsSet || m_channelMarkerIsSet || m_rollupStateIsSet;
}
qint64 SWGRadioPagingDemodSettings::getInputFrequencyOffset() { return m_inputFrequencyOffset; }
void SWGRadioPagingDemodSettings::setInputFrequencyOffset(qint64 value) { m_inputFrequencyOffset = value; m_inputFrequencyOffsetIsSet = true; }
float SWGRadioPagingDemodSettings::getRfBandwidth() { return m_rfBandwidth; }
void SWGRadioPagingDemodSettings::setRfBandwidth(float value) { m_rfBandwidth = value; m_rfBandwidthIsSet = true; }
float SWGRadioPagingDemodSettings::getPocsagDeviation() { return m_pocsagDeviation; }
void SWGRadioPagingDemodSettings::setPocsagDeviation(float value) { m_pocsagDeviation = value; m_pocsagDeviationIsSet = true; }
float SWGRadioPagingDemodSettings::getFlexDeviation() { return m_flexDeviation; }
void SWGRadioPagingDemodSettings::setFlexDeviation(float value) { m_flexDeviation = value; m_flexDeviationIsSet = true; }
float SWGRadioPagingDemodSettings::getGscDeviation() { return m_gscDeviation; }
void SWGRadioPagingDemodSettings::setGscDeviation(float value) { m_gscDeviation = value; m_gscDeviationIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getGscNonBatterySaver() { return m_gscNonBatterySaver; }
void SWGRadioPagingDemodSettings::setGscNonBatterySaver(qint32 value) { m_gscNonBatterySaver = value; m_gscNonBatterySaverIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getGscVoiceTimeout() { return m_gscVoiceTimeout; }
void SWGRadioPagingDemodSettings::setGscVoiceTimeout(qint32 value) { m_gscVoiceTimeout = value; m_gscVoiceTimeoutIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getRgbColor() { return m_rgbColor; }
void SWGRadioPagingDemodSettings::setRgbColor(qint32 value) { m_rgbColor = value; m_rgbColorIsSet = true; }
QString* SWGRadioPagingDemodSettings::getTitle() { return m_title; }
void SWGRadioPagingDemodSettings::setTitle(QString* value) { if (m_title != value) { delete m_title; m_title = value; } m_titleIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getStreamIndex() { return m_streamIndex; }
void SWGRadioPagingDemodSettings::setStreamIndex(qint32 value) { m_streamIndex = value; m_streamIndexIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getUseReverseApi() { return m_useReverseAPI; }
void SWGRadioPagingDemodSettings::setUseReverseApi(qint32 value) { m_useReverseAPI = value; m_useReverseAPIIsSet = true; }
QString* SWGRadioPagingDemodSettings::getReverseApiAddress() { return m_reverseAPIAddress; }
void SWGRadioPagingDemodSettings::setReverseApiAddress(QString* value) { if (m_reverseAPIAddress != value) { delete m_reverseAPIAddress; m_reverseAPIAddress = value; } m_reverseAPIAddressIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getReverseApiPort() { return m_reverseAPIPort; }
void SWGRadioPagingDemodSettings::setReverseApiPort(qint32 value) { m_reverseAPIPort = value; m_reverseAPIPortIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getReverseApiDeviceIndex() { return m_reverseAPIDeviceIndex; }
void SWGRadioPagingDemodSettings::setReverseApiDeviceIndex(qint32 value) { m_reverseAPIDeviceIndex = value; m_reverseAPIDeviceIndexIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getReverseApiChannelIndex() { return m_reverseAPIChannelIndex; }
void SWGRadioPagingDemodSettings::setReverseApiChannelIndex(qint32 value) { m_reverseAPIChannelIndex = value; m_reverseAPIChannelIndexIsSet = true; }
SWGChannelMarker* SWGRadioPagingDemodSettings::getChannelMarker() { return m_channelMarker; }
void SWGRadioPagingDemodSettings::setChannelMarker(SWGChannelMarker* value) { if (m_channelMarker != value) { delete m_channelMarker; m_channelMarker = value; } m_channelMarkerIsSet = true; }
SWGRollupState* SWGRadioPagingDemodSettings::getRollupState() { return m_rollupState; }
void SWGRadioPagingDemodSettings::setRollupState(SWGRollupState* value) { if (m_rollupState != value) { delete m_rollupState; m_rollupState = value; } m_rollupStateIsSet = true; }
}
