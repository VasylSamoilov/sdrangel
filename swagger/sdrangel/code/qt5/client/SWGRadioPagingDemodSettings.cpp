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
    m_pocsag512Enabled = 0; m_pocsag512EnabledIsSet = false;
    m_pocsag1200Enabled = 0; m_pocsag1200EnabledIsSet = false;
    m_pocsag2400Enabled = 0; m_pocsag2400EnabledIsSet = false;
    m_pocsagDeviation = 0; m_pocsagDeviationIsSet = false;
    m_flex1600x2Enabled = 0; m_flex1600x2EnabledIsSet = false;
    m_flex3200x2Enabled = 0; m_flex3200x2EnabledIsSet = false;
    m_flex3200x4Enabled = 0; m_flex3200x4EnabledIsSet = false;
    m_flex6400x4Enabled = 0; m_flex6400x4EnabledIsSet = false;
    m_flexDeviation = 0; m_flexDeviationIsSet = false;
    m_gscEnabled = 0; m_gscEnabledIsSet = false;
    m_gscDeviation = 0; m_gscDeviationIsSet = false;
    m_gscNonBatterySaver = 0; m_gscNonBatterySaverIsSet = false;
    m_gscVoiceTimeout = 0; m_gscVoiceTimeoutIsSet = false;
    m_normalPolarity = 0; m_normalPolarityIsSet = false;
    m_invertedPolarity = 0; m_invertedPolarityIsSet = false;
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
    if (json.contains("pocsag512Enabled")) { ::SWGSDRangel::setValue(&m_pocsag512Enabled,json["pocsag512Enabled"],"qint32",""); m_pocsag512EnabledIsSet = true; }
    if (json.contains("pocsag1200Enabled")) { ::SWGSDRangel::setValue(&m_pocsag1200Enabled,json["pocsag1200Enabled"],"qint32",""); m_pocsag1200EnabledIsSet = true; }
    if (json.contains("pocsag2400Enabled")) { ::SWGSDRangel::setValue(&m_pocsag2400Enabled,json["pocsag2400Enabled"],"qint32",""); m_pocsag2400EnabledIsSet = true; }
    if (json.contains("pocsagDeviation")) { ::SWGSDRangel::setValue(&m_pocsagDeviation,json["pocsagDeviation"],"float",""); m_pocsagDeviationIsSet = true; }
    if (json.contains("flex1600x2Enabled")) { ::SWGSDRangel::setValue(&m_flex1600x2Enabled,json["flex1600x2Enabled"],"qint32",""); m_flex1600x2EnabledIsSet = true; }
    if (json.contains("flex3200x2Enabled")) { ::SWGSDRangel::setValue(&m_flex3200x2Enabled,json["flex3200x2Enabled"],"qint32",""); m_flex3200x2EnabledIsSet = true; }
    if (json.contains("flex3200x4Enabled")) { ::SWGSDRangel::setValue(&m_flex3200x4Enabled,json["flex3200x4Enabled"],"qint32",""); m_flex3200x4EnabledIsSet = true; }
    if (json.contains("flex6400x4Enabled")) { ::SWGSDRangel::setValue(&m_flex6400x4Enabled,json["flex6400x4Enabled"],"qint32",""); m_flex6400x4EnabledIsSet = true; }
    if (json.contains("flexDeviation")) { ::SWGSDRangel::setValue(&m_flexDeviation,json["flexDeviation"],"float",""); m_flexDeviationIsSet = true; }
    if (json.contains("gscEnabled")) { ::SWGSDRangel::setValue(&m_gscEnabled,json["gscEnabled"],"qint32",""); m_gscEnabledIsSet = true; }
    if (json.contains("gscDeviation")) { ::SWGSDRangel::setValue(&m_gscDeviation,json["gscDeviation"],"float",""); m_gscDeviationIsSet = true; }
    if (json.contains("gscNonBatterySaver")) { ::SWGSDRangel::setValue(&m_gscNonBatterySaver,json["gscNonBatterySaver"],"qint32",""); m_gscNonBatterySaverIsSet = true; }
    if (json.contains("gscVoiceTimeout")) { ::SWGSDRangel::setValue(&m_gscVoiceTimeout,json["gscVoiceTimeout"],"qint32",""); m_gscVoiceTimeoutIsSet = true; }
    if (json.contains("normalPolarity")) { ::SWGSDRangel::setValue(&m_normalPolarity,json["normalPolarity"],"qint32",""); m_normalPolarityIsSet = true; }
    if (json.contains("invertedPolarity")) { ::SWGSDRangel::setValue(&m_invertedPolarity,json["invertedPolarity"],"qint32",""); m_invertedPolarityIsSet = true; }
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
    if (m_pocsag512EnabledIsSet) { object->insert("pocsag512Enabled",QJsonValue(m_pocsag512Enabled)); }
    if (m_pocsag1200EnabledIsSet) { object->insert("pocsag1200Enabled",QJsonValue(m_pocsag1200Enabled)); }
    if (m_pocsag2400EnabledIsSet) { object->insert("pocsag2400Enabled",QJsonValue(m_pocsag2400Enabled)); }
    if (m_pocsagDeviationIsSet) { object->insert("pocsagDeviation",QJsonValue(m_pocsagDeviation)); }
    if (m_flex1600x2EnabledIsSet) { object->insert("flex1600x2Enabled",QJsonValue(m_flex1600x2Enabled)); }
    if (m_flex3200x2EnabledIsSet) { object->insert("flex3200x2Enabled",QJsonValue(m_flex3200x2Enabled)); }
    if (m_flex3200x4EnabledIsSet) { object->insert("flex3200x4Enabled",QJsonValue(m_flex3200x4Enabled)); }
    if (m_flex6400x4EnabledIsSet) { object->insert("flex6400x4Enabled",QJsonValue(m_flex6400x4Enabled)); }
    if (m_flexDeviationIsSet) { object->insert("flexDeviation",QJsonValue(m_flexDeviation)); }
    if (m_gscEnabledIsSet) { object->insert("gscEnabled",QJsonValue(m_gscEnabled)); }
    if (m_gscDeviationIsSet) { object->insert("gscDeviation",QJsonValue(m_gscDeviation)); }
    if (m_gscNonBatterySaverIsSet) { object->insert("gscNonBatterySaver",QJsonValue(m_gscNonBatterySaver)); }
    if (m_gscVoiceTimeoutIsSet) { object->insert("gscVoiceTimeout",QJsonValue(m_gscVoiceTimeout)); }
    if (m_normalPolarityIsSet) { object->insert("normalPolarity",QJsonValue(m_normalPolarity)); }
    if (m_invertedPolarityIsSet) { object->insert("invertedPolarity",QJsonValue(m_invertedPolarity)); }
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
    return m_inputFrequencyOffsetIsSet || m_rfBandwidthIsSet || m_pocsag512EnabledIsSet || m_pocsag1200EnabledIsSet || m_pocsag2400EnabledIsSet || m_pocsagDeviationIsSet || m_flex1600x2EnabledIsSet || m_flex3200x2EnabledIsSet || m_flex3200x4EnabledIsSet || m_flex6400x4EnabledIsSet || m_flexDeviationIsSet || m_gscEnabledIsSet || m_gscDeviationIsSet || m_gscNonBatterySaverIsSet || m_gscVoiceTimeoutIsSet || m_normalPolarityIsSet || m_invertedPolarityIsSet || m_rgbColorIsSet || m_titleIsSet || m_streamIndexIsSet || m_useReverseAPIIsSet || m_reverseAPIAddressIsSet || m_reverseAPIPortIsSet || m_reverseAPIDeviceIndexIsSet || m_reverseAPIChannelIndexIsSet || m_channelMarkerIsSet || m_rollupStateIsSet;
}
qint64 SWGRadioPagingDemodSettings::getInputFrequencyOffset() { return m_inputFrequencyOffset; }
void SWGRadioPagingDemodSettings::setInputFrequencyOffset(qint64 value) { m_inputFrequencyOffset = value; m_inputFrequencyOffsetIsSet = true; }
float SWGRadioPagingDemodSettings::getRfBandwidth() { return m_rfBandwidth; }
void SWGRadioPagingDemodSettings::setRfBandwidth(float value) { m_rfBandwidth = value; m_rfBandwidthIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getPocsag512Enabled() { return m_pocsag512Enabled; }
void SWGRadioPagingDemodSettings::setPocsag512Enabled(qint32 value) { m_pocsag512Enabled = value; m_pocsag512EnabledIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getPocsag1200Enabled() { return m_pocsag1200Enabled; }
void SWGRadioPagingDemodSettings::setPocsag1200Enabled(qint32 value) { m_pocsag1200Enabled = value; m_pocsag1200EnabledIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getPocsag2400Enabled() { return m_pocsag2400Enabled; }
void SWGRadioPagingDemodSettings::setPocsag2400Enabled(qint32 value) { m_pocsag2400Enabled = value; m_pocsag2400EnabledIsSet = true; }
float SWGRadioPagingDemodSettings::getPocsagDeviation() { return m_pocsagDeviation; }
void SWGRadioPagingDemodSettings::setPocsagDeviation(float value) { m_pocsagDeviation = value; m_pocsagDeviationIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getFlex1600x2Enabled() { return m_flex1600x2Enabled; }
void SWGRadioPagingDemodSettings::setFlex1600x2Enabled(qint32 value) { m_flex1600x2Enabled = value; m_flex1600x2EnabledIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getFlex3200x2Enabled() { return m_flex3200x2Enabled; }
void SWGRadioPagingDemodSettings::setFlex3200x2Enabled(qint32 value) { m_flex3200x2Enabled = value; m_flex3200x2EnabledIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getFlex3200x4Enabled() { return m_flex3200x4Enabled; }
void SWGRadioPagingDemodSettings::setFlex3200x4Enabled(qint32 value) { m_flex3200x4Enabled = value; m_flex3200x4EnabledIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getFlex6400x4Enabled() { return m_flex6400x4Enabled; }
void SWGRadioPagingDemodSettings::setFlex6400x4Enabled(qint32 value) { m_flex6400x4Enabled = value; m_flex6400x4EnabledIsSet = true; }
float SWGRadioPagingDemodSettings::getFlexDeviation() { return m_flexDeviation; }
void SWGRadioPagingDemodSettings::setFlexDeviation(float value) { m_flexDeviation = value; m_flexDeviationIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getGscEnabled() { return m_gscEnabled; }
void SWGRadioPagingDemodSettings::setGscEnabled(qint32 value) { m_gscEnabled = value; m_gscEnabledIsSet = true; }
float SWGRadioPagingDemodSettings::getGscDeviation() { return m_gscDeviation; }
void SWGRadioPagingDemodSettings::setGscDeviation(float value) { m_gscDeviation = value; m_gscDeviationIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getGscNonBatterySaver() { return m_gscNonBatterySaver; }
void SWGRadioPagingDemodSettings::setGscNonBatterySaver(qint32 value) { m_gscNonBatterySaver = value; m_gscNonBatterySaverIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getGscVoiceTimeout() { return m_gscVoiceTimeout; }
void SWGRadioPagingDemodSettings::setGscVoiceTimeout(qint32 value) { m_gscVoiceTimeout = value; m_gscVoiceTimeoutIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getNormalPolarity() { return m_normalPolarity; }
void SWGRadioPagingDemodSettings::setNormalPolarity(qint32 value) { m_normalPolarity = value; m_normalPolarityIsSet = true; }
qint32 SWGRadioPagingDemodSettings::getInvertedPolarity() { return m_invertedPolarity; }
void SWGRadioPagingDemodSettings::setInvertedPolarity(qint32 value) { m_invertedPolarity = value; m_invertedPolarityIsSet = true; }
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
