// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGRadioPagingModSettings.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGRadioPagingModSettings::SWGRadioPagingModSettings() = default;
SWGRadioPagingModSettings::SWGRadioPagingModSettings(QString* json) { init(); if (json) { fromJson(*json); } }
SWGRadioPagingModSettings::~SWGRadioPagingModSettings() { cleanup(); }
void SWGRadioPagingModSettings::cleanup()
{
    delete m_title; m_title = nullptr;
    delete m_channelAlias; m_channelAlias = nullptr;
    delete m_gscVoiceDirectory; m_gscVoiceDirectory = nullptr;
    delete m_reverseAPIAddress; m_reverseAPIAddress = nullptr;
    delete m_channelMarker; m_channelMarker = nullptr;
    delete m_rollupState; m_rollupState = nullptr;
}
void SWGRadioPagingModSettings::init()
{
    cleanup();
    m_inputFrequencyOffset = 0; m_inputFrequencyOffsetIsSet = false;
    m_protocol = 0; m_protocolIsSet = false;
    m_mode = 0; m_modeIsSet = false;
    m_deviation = 0; m_deviationIsSet = false;
    m_gain = 0; m_gainIsSet = false;
    m_channelMute = 0; m_channelMuteIsSet = false;
    m_rampUpMs = 0; m_rampUpMsIsSet = false;
    m_rampDownMs = 0; m_rampDownMsIsSet = false;
    m_rampRange = 0; m_rampRangeIsSet = false;
    m_pocsagSpeed = 0; m_pocsagSpeedIsSet = false;
    m_pocsagInverted = 0; m_pocsagInvertedIsSet = false;
    m_flexMode = 0; m_flexModeIsSet = false;
    m_flexInverted = 0; m_flexInvertedIsSet = false;
    m_flexCollapse = 0; m_flexCollapseIsSet = false;
    m_flexRoaming = 0; m_flexRoamingIsSet = false;
    m_ssidLocalId = 0; m_ssidLocalIdIsSet = false;
    m_ssidCoverageZone = 0; m_ssidCoverageZoneIsSet = false;
    m_ssidCountryCode = 0; m_ssidCountryCodeIsSet = false;
    m_ssidTrafficFlags = 0; m_ssidTrafficFlagsIsSet = false;
    m_flexTime = 0; m_flexTimeIsSet = false;
    m_flexTimeFrameZero = 0; m_flexTimeFrameZeroIsSet = false;
    m_flexTimeZoneMinutes = 0; m_flexTimeZoneMinutesIsSet = false;
    m_flexTimeSendZone = 0; m_flexTimeSendZoneIsSet = false;
    m_flexTimeDaylightSaving = 0; m_flexTimeDaylightSavingIsSet = false;
    m_flexTimeOffsetSeconds = 0; m_flexTimeOffsetSecondsIsSet = false;
    m_gscMode = 0; m_gscModeIsSet = false;
    m_gscInverted = 0; m_gscInvertedIsSet = false;
    m_gscVoiceSeconds = 0; m_gscVoiceSecondsIsSet = false;
    m_mixPocsag = 0; m_mixPocsagIsSet = false;
    m_pocsagDeviation = 0; m_pocsagDeviationIsSet = false;
    m_mixFramesPerMinute = 0; m_mixFramesPerMinuteIsSet = false;
    m_mixShared = 0; m_mixSharedIsSet = false;
    m_mixSwitchMs512 = 0; m_mixSwitchMs512IsSet = false;
    m_mixSwitchMs1200 = 0; m_mixSwitchMs1200IsSet = false;
    m_mixSwitchMs2400 = 0; m_mixSwitchMs2400IsSet = false;
    m_rgbColor = 0; m_rgbColorIsSet = false;
    m_title = new QString(); m_titleIsSet = false;
    m_channelAlias = new QString(); m_channelAliasIsSet = false;
    m_gscVoiceDirectory = new QString(); m_gscVoiceDirectoryIsSet = false;
    m_gscVoiceKeepDays = 0; m_gscVoiceKeepDaysIsSet = false;
    m_gscVoiceKeepMin = 0; m_gscVoiceKeepMinIsSet = false;
    m_streamIndex = 0; m_streamIndexIsSet = false;
    m_useReverseAPI = 0; m_useReverseAPIIsSet = false;
    m_reverseAPIAddress = new QString(); m_reverseAPIAddressIsSet = false;
    m_reverseAPIPort = 0; m_reverseAPIPortIsSet = false;
    m_reverseAPIDeviceIndex = 0; m_reverseAPIDeviceIndexIsSet = false;
    m_reverseAPIChannelIndex = 0; m_reverseAPIChannelIndexIsSet = false;
    m_channelMarker = new SWGChannelMarker(); m_channelMarkerIsSet = false;
    m_rollupState = new SWGRollupState(); m_rollupStateIsSet = false;
}
SWGRadioPagingModSettings* SWGRadioPagingModSettings::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGRadioPagingModSettings::fromJsonObject(QJsonObject& json)
{
    if (json.contains("inputFrequencyOffset")) { ::SWGSDRangel::setValue(&m_inputFrequencyOffset,json["inputFrequencyOffset"],"qint64",""); m_inputFrequencyOffsetIsSet = true; }
    if (json.contains("protocol")) { ::SWGSDRangel::setValue(&m_protocol,json["protocol"],"qint32",""); m_protocolIsSet = true; }
    if (json.contains("mode")) { ::SWGSDRangel::setValue(&m_mode,json["mode"],"qint32",""); m_modeIsSet = true; }
    if (json.contains("deviation")) { ::SWGSDRangel::setValue(&m_deviation,json["deviation"],"float",""); m_deviationIsSet = true; }
    if (json.contains("gain")) { ::SWGSDRangel::setValue(&m_gain,json["gain"],"float",""); m_gainIsSet = true; }
    if (json.contains("channelMute")) { ::SWGSDRangel::setValue(&m_channelMute,json["channelMute"],"qint32",""); m_channelMuteIsSet = true; }
    if (json.contains("rampUpMs")) { ::SWGSDRangel::setValue(&m_rampUpMs,json["rampUpMs"],"float",""); m_rampUpMsIsSet = true; }
    if (json.contains("rampDownMs")) { ::SWGSDRangel::setValue(&m_rampDownMs,json["rampDownMs"],"float",""); m_rampDownMsIsSet = true; }
    if (json.contains("rampRange")) { ::SWGSDRangel::setValue(&m_rampRange,json["rampRange"],"qint32",""); m_rampRangeIsSet = true; }
    if (json.contains("pocsagSpeed")) { ::SWGSDRangel::setValue(&m_pocsagSpeed,json["pocsagSpeed"],"qint32",""); m_pocsagSpeedIsSet = true; }
    if (json.contains("pocsagInverted")) { ::SWGSDRangel::setValue(&m_pocsagInverted,json["pocsagInverted"],"qint32",""); m_pocsagInvertedIsSet = true; }
    if (json.contains("flexMode")) { ::SWGSDRangel::setValue(&m_flexMode,json["flexMode"],"qint32",""); m_flexModeIsSet = true; }
    if (json.contains("flexInverted")) { ::SWGSDRangel::setValue(&m_flexInverted,json["flexInverted"],"qint32",""); m_flexInvertedIsSet = true; }
    if (json.contains("flexCollapse")) { ::SWGSDRangel::setValue(&m_flexCollapse,json["flexCollapse"],"qint32",""); m_flexCollapseIsSet = true; }
    if (json.contains("flexRoaming")) { ::SWGSDRangel::setValue(&m_flexRoaming,json["flexRoaming"],"qint32",""); m_flexRoamingIsSet = true; }
    if (json.contains("ssidLocalId")) { ::SWGSDRangel::setValue(&m_ssidLocalId,json["ssidLocalId"],"qint32",""); m_ssidLocalIdIsSet = true; }
    if (json.contains("ssidCoverageZone")) { ::SWGSDRangel::setValue(&m_ssidCoverageZone,json["ssidCoverageZone"],"qint32",""); m_ssidCoverageZoneIsSet = true; }
    if (json.contains("ssidCountryCode")) { ::SWGSDRangel::setValue(&m_ssidCountryCode,json["ssidCountryCode"],"qint32",""); m_ssidCountryCodeIsSet = true; }
    if (json.contains("ssidTrafficFlags")) { ::SWGSDRangel::setValue(&m_ssidTrafficFlags,json["ssidTrafficFlags"],"qint32",""); m_ssidTrafficFlagsIsSet = true; }
    if (json.contains("flexTime")) { ::SWGSDRangel::setValue(&m_flexTime,json["flexTime"],"qint32",""); m_flexTimeIsSet = true; }
    if (json.contains("flexTimeFrameZero")) { ::SWGSDRangel::setValue(&m_flexTimeFrameZero,json["flexTimeFrameZero"],"qint32",""); m_flexTimeFrameZeroIsSet = true; }
    if (json.contains("flexTimeZoneMinutes")) { ::SWGSDRangel::setValue(&m_flexTimeZoneMinutes,json["flexTimeZoneMinutes"],"qint32",""); m_flexTimeZoneMinutesIsSet = true; }
    if (json.contains("flexTimeSendZone")) { ::SWGSDRangel::setValue(&m_flexTimeSendZone,json["flexTimeSendZone"],"qint32",""); m_flexTimeSendZoneIsSet = true; }
    if (json.contains("flexTimeDaylightSaving")) { ::SWGSDRangel::setValue(&m_flexTimeDaylightSaving,json["flexTimeDaylightSaving"],"qint32",""); m_flexTimeDaylightSavingIsSet = true; }
    if (json.contains("flexTimeOffsetSeconds")) { ::SWGSDRangel::setValue(&m_flexTimeOffsetSeconds,json["flexTimeOffsetSeconds"],"qint64",""); m_flexTimeOffsetSecondsIsSet = true; }
    if (json.contains("gscMode")) { ::SWGSDRangel::setValue(&m_gscMode,json["gscMode"],"qint32",""); m_gscModeIsSet = true; }
    if (json.contains("gscInverted")) { ::SWGSDRangel::setValue(&m_gscInverted,json["gscInverted"],"qint32",""); m_gscInvertedIsSet = true; }
    if (json.contains("gscVoiceSeconds")) { ::SWGSDRangel::setValue(&m_gscVoiceSeconds,json["gscVoiceSeconds"],"qint32",""); m_gscVoiceSecondsIsSet = true; }
    if (json.contains("mixPocsag")) { ::SWGSDRangel::setValue(&m_mixPocsag,json["mixPocsag"],"qint32",""); m_mixPocsagIsSet = true; }
    if (json.contains("pocsagDeviation")) { ::SWGSDRangel::setValue(&m_pocsagDeviation,json["pocsagDeviation"],"float",""); m_pocsagDeviationIsSet = true; }
    if (json.contains("mixFramesPerMinute")) { ::SWGSDRangel::setValue(&m_mixFramesPerMinute,json["mixFramesPerMinute"],"qint32",""); m_mixFramesPerMinuteIsSet = true; }
    if (json.contains("mixShared")) { ::SWGSDRangel::setValue(&m_mixShared,json["mixShared"],"qint32",""); m_mixSharedIsSet = true; }
    if (json.contains("mixSwitchMs512")) { ::SWGSDRangel::setValue(&m_mixSwitchMs512,json["mixSwitchMs512"],"qint32",""); m_mixSwitchMs512IsSet = true; }
    if (json.contains("mixSwitchMs1200")) { ::SWGSDRangel::setValue(&m_mixSwitchMs1200,json["mixSwitchMs1200"],"qint32",""); m_mixSwitchMs1200IsSet = true; }
    if (json.contains("mixSwitchMs2400")) { ::SWGSDRangel::setValue(&m_mixSwitchMs2400,json["mixSwitchMs2400"],"qint32",""); m_mixSwitchMs2400IsSet = true; }
    if (json.contains("rgbColor")) { ::SWGSDRangel::setValue(&m_rgbColor,json["rgbColor"],"qint32",""); m_rgbColorIsSet = true; }
    if (json.contains("channelAlias")) { ::SWGSDRangel::setValue(&m_channelAlias,json["channelAlias"],"QString","QString"); m_channelAliasIsSet = true; }
    if (json.contains("gscVoiceDirectory")) { ::SWGSDRangel::setValue(&m_gscVoiceDirectory,json["gscVoiceDirectory"],"QString","QString"); m_gscVoiceDirectoryIsSet = true; }
    if (json.contains("gscVoiceKeepDays")) { ::SWGSDRangel::setValue(&m_gscVoiceKeepDays,json["gscVoiceKeepDays"],"qint32",""); m_gscVoiceKeepDaysIsSet = true; }
    if (json.contains("gscVoiceKeepMin")) { ::SWGSDRangel::setValue(&m_gscVoiceKeepMin,json["gscVoiceKeepMin"],"qint32",""); m_gscVoiceKeepMinIsSet = true; }
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
QString SWGRadioPagingModSettings::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGRadioPagingModSettings::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_inputFrequencyOffsetIsSet) { object->insert("inputFrequencyOffset",QJsonValue(m_inputFrequencyOffset)); }
    if (m_protocolIsSet) { object->insert("protocol",QJsonValue(m_protocol)); }
    if (m_modeIsSet) { object->insert("mode",QJsonValue(m_mode)); }
    if (m_deviationIsSet) { object->insert("deviation",QJsonValue(m_deviation)); }
    if (m_gainIsSet) { object->insert("gain",QJsonValue(m_gain)); }
    if (m_channelMuteIsSet) { object->insert("channelMute",QJsonValue(m_channelMute)); }
    if (m_rampUpMsIsSet) { object->insert("rampUpMs",QJsonValue(m_rampUpMs)); }
    if (m_rampDownMsIsSet) { object->insert("rampDownMs",QJsonValue(m_rampDownMs)); }
    if (m_rampRangeIsSet) { object->insert("rampRange",QJsonValue(m_rampRange)); }
    if (m_pocsagSpeedIsSet) { object->insert("pocsagSpeed",QJsonValue(m_pocsagSpeed)); }
    if (m_pocsagInvertedIsSet) { object->insert("pocsagInverted",QJsonValue(m_pocsagInverted)); }
    if (m_flexModeIsSet) { object->insert("flexMode",QJsonValue(m_flexMode)); }
    if (m_flexInvertedIsSet) { object->insert("flexInverted",QJsonValue(m_flexInverted)); }
    if (m_flexCollapseIsSet) { object->insert("flexCollapse",QJsonValue(m_flexCollapse)); }
    if (m_flexRoamingIsSet) { object->insert("flexRoaming",QJsonValue(m_flexRoaming)); }
    if (m_ssidLocalIdIsSet) { object->insert("ssidLocalId",QJsonValue(m_ssidLocalId)); }
    if (m_ssidCoverageZoneIsSet) { object->insert("ssidCoverageZone",QJsonValue(m_ssidCoverageZone)); }
    if (m_ssidCountryCodeIsSet) { object->insert("ssidCountryCode",QJsonValue(m_ssidCountryCode)); }
    if (m_ssidTrafficFlagsIsSet) { object->insert("ssidTrafficFlags",QJsonValue(m_ssidTrafficFlags)); }
    if (m_flexTimeIsSet) { object->insert("flexTime",QJsonValue(m_flexTime)); }
    if (m_flexTimeFrameZeroIsSet) { object->insert("flexTimeFrameZero",QJsonValue(m_flexTimeFrameZero)); }
    if (m_flexTimeZoneMinutesIsSet) { object->insert("flexTimeZoneMinutes",QJsonValue(m_flexTimeZoneMinutes)); }
    if (m_flexTimeSendZoneIsSet) { object->insert("flexTimeSendZone",QJsonValue(m_flexTimeSendZone)); }
    if (m_flexTimeDaylightSavingIsSet) { object->insert("flexTimeDaylightSaving",QJsonValue(m_flexTimeDaylightSaving)); }
    if (m_flexTimeOffsetSecondsIsSet) { object->insert("flexTimeOffsetSeconds",QJsonValue(m_flexTimeOffsetSeconds)); }
    if (m_gscModeIsSet) { object->insert("gscMode",QJsonValue(m_gscMode)); }
    if (m_gscInvertedIsSet) { object->insert("gscInverted",QJsonValue(m_gscInverted)); }
    if (m_gscVoiceSecondsIsSet) { object->insert("gscVoiceSeconds",QJsonValue(m_gscVoiceSeconds)); }
    if (m_mixPocsagIsSet) { object->insert("mixPocsag",QJsonValue(m_mixPocsag)); }
    if (m_pocsagDeviationIsSet) { object->insert("pocsagDeviation",QJsonValue(m_pocsagDeviation)); }
    if (m_mixFramesPerMinuteIsSet) { object->insert("mixFramesPerMinute",QJsonValue(m_mixFramesPerMinute)); }
    if (m_mixSharedIsSet) { object->insert("mixShared",QJsonValue(m_mixShared)); }
    if (m_mixSwitchMs512IsSet) { object->insert("mixSwitchMs512",QJsonValue(m_mixSwitchMs512)); }
    if (m_mixSwitchMs1200IsSet) { object->insert("mixSwitchMs1200",QJsonValue(m_mixSwitchMs1200)); }
    if (m_mixSwitchMs2400IsSet) { object->insert("mixSwitchMs2400",QJsonValue(m_mixSwitchMs2400)); }
    if (m_rgbColorIsSet) { object->insert("rgbColor",QJsonValue(m_rgbColor)); }
    if (m_channelAlias && (m_channelAliasIsSet || !m_channelAlias->isEmpty())) { toJsonValue(QString("channelAlias"),m_channelAlias,object,QString("QString")); }
    if (m_gscVoiceDirectory && (m_gscVoiceDirectoryIsSet || !m_gscVoiceDirectory->isEmpty())) { toJsonValue(QString("gscVoiceDirectory"),m_gscVoiceDirectory,object,QString("QString")); }
    if (m_gscVoiceKeepDaysIsSet) { object->insert("gscVoiceKeepDays",QJsonValue(m_gscVoiceKeepDays)); }
    if (m_gscVoiceKeepMinIsSet) { object->insert("gscVoiceKeepMin",QJsonValue(m_gscVoiceKeepMin)); }
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
bool SWGRadioPagingModSettings::isSet()
{
    return m_channelAliasIsSet || m_gscVoiceDirectoryIsSet || m_gscVoiceKeepDaysIsSet || m_gscVoiceKeepMinIsSet || m_inputFrequencyOffsetIsSet || m_protocolIsSet || m_modeIsSet || m_deviationIsSet || m_gainIsSet || m_channelMuteIsSet || m_rampUpMsIsSet || m_rampDownMsIsSet || m_rampRangeIsSet || m_pocsagSpeedIsSet || m_pocsagInvertedIsSet || m_flexModeIsSet || m_flexInvertedIsSet || m_flexCollapseIsSet || m_flexRoamingIsSet || m_ssidLocalIdIsSet || m_ssidCoverageZoneIsSet || m_ssidCountryCodeIsSet || m_ssidTrafficFlagsIsSet || m_flexTimeIsSet || m_flexTimeFrameZeroIsSet || m_flexTimeZoneMinutesIsSet || m_flexTimeSendZoneIsSet || m_flexTimeDaylightSavingIsSet || m_flexTimeOffsetSecondsIsSet || m_gscModeIsSet || m_gscInvertedIsSet || m_gscVoiceSecondsIsSet || m_mixPocsagIsSet || m_pocsagDeviationIsSet || m_mixFramesPerMinuteIsSet || m_mixSharedIsSet || m_mixSwitchMs512IsSet || m_mixSwitchMs1200IsSet || m_mixSwitchMs2400IsSet || m_rgbColorIsSet || m_titleIsSet || m_streamIndexIsSet || m_useReverseAPIIsSet || m_reverseAPIAddressIsSet || m_reverseAPIPortIsSet || m_reverseAPIDeviceIndexIsSet || m_reverseAPIChannelIndexIsSet || m_channelMarkerIsSet || m_rollupStateIsSet;
}
qint64 SWGRadioPagingModSettings::getInputFrequencyOffset() { return m_inputFrequencyOffset; }
void SWGRadioPagingModSettings::setInputFrequencyOffset(qint64 value) { m_inputFrequencyOffset = value; m_inputFrequencyOffsetIsSet = true; }
qint32 SWGRadioPagingModSettings::getProtocol() { return m_protocol; }
void SWGRadioPagingModSettings::setProtocol(qint32 value) { m_protocol = value; m_protocolIsSet = true; }
qint32 SWGRadioPagingModSettings::getMode() { return m_mode; }
void SWGRadioPagingModSettings::setMode(qint32 value) { m_mode = value; m_modeIsSet = true; }
float SWGRadioPagingModSettings::getDeviation() { return m_deviation; }
void SWGRadioPagingModSettings::setDeviation(float value) { m_deviation = value; m_deviationIsSet = true; }
float SWGRadioPagingModSettings::getGain() { return m_gain; }
void SWGRadioPagingModSettings::setGain(float value) { m_gain = value; m_gainIsSet = true; }
qint32 SWGRadioPagingModSettings::getChannelMute() { return m_channelMute; }
void SWGRadioPagingModSettings::setChannelMute(qint32 value) { m_channelMute = value; m_channelMuteIsSet = true; }
float SWGRadioPagingModSettings::getRampUpMs() { return m_rampUpMs; }
void SWGRadioPagingModSettings::setRampUpMs(float value) { m_rampUpMs = value; m_rampUpMsIsSet = true; }
float SWGRadioPagingModSettings::getRampDownMs() { return m_rampDownMs; }
void SWGRadioPagingModSettings::setRampDownMs(float value) { m_rampDownMs = value; m_rampDownMsIsSet = true; }
qint32 SWGRadioPagingModSettings::getRampRange() { return m_rampRange; }
void SWGRadioPagingModSettings::setRampRange(qint32 value) { m_rampRange = value; m_rampRangeIsSet = true; }
qint32 SWGRadioPagingModSettings::getPocsagSpeed() { return m_pocsagSpeed; }
void SWGRadioPagingModSettings::setPocsagSpeed(qint32 value) { m_pocsagSpeed = value; m_pocsagSpeedIsSet = true; }
qint32 SWGRadioPagingModSettings::getPocsagInverted() { return m_pocsagInverted; }
void SWGRadioPagingModSettings::setPocsagInverted(qint32 value) { m_pocsagInverted = value; m_pocsagInvertedIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexMode() { return m_flexMode; }
void SWGRadioPagingModSettings::setFlexMode(qint32 value) { m_flexMode = value; m_flexModeIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexInverted() { return m_flexInverted; }
void SWGRadioPagingModSettings::setFlexInverted(qint32 value) { m_flexInverted = value; m_flexInvertedIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexCollapse() { return m_flexCollapse; }
void SWGRadioPagingModSettings::setFlexCollapse(qint32 value) { m_flexCollapse = value; m_flexCollapseIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexRoaming() { return m_flexRoaming; }
void SWGRadioPagingModSettings::setFlexRoaming(qint32 value) { m_flexRoaming = value; m_flexRoamingIsSet = true; }
qint32 SWGRadioPagingModSettings::getSsidLocalId() { return m_ssidLocalId; }
void SWGRadioPagingModSettings::setSsidLocalId(qint32 value) { m_ssidLocalId = value; m_ssidLocalIdIsSet = true; }
qint32 SWGRadioPagingModSettings::getSsidCoverageZone() { return m_ssidCoverageZone; }
void SWGRadioPagingModSettings::setSsidCoverageZone(qint32 value) { m_ssidCoverageZone = value; m_ssidCoverageZoneIsSet = true; }
qint32 SWGRadioPagingModSettings::getSsidCountryCode() { return m_ssidCountryCode; }
void SWGRadioPagingModSettings::setSsidCountryCode(qint32 value) { m_ssidCountryCode = value; m_ssidCountryCodeIsSet = true; }
qint32 SWGRadioPagingModSettings::getSsidTrafficFlags() { return m_ssidTrafficFlags; }
void SWGRadioPagingModSettings::setSsidTrafficFlags(qint32 value) { m_ssidTrafficFlags = value; m_ssidTrafficFlagsIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexTime() { return m_flexTime; }
void SWGRadioPagingModSettings::setFlexTime(qint32 value) { m_flexTime = value; m_flexTimeIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexTimeFrameZero() { return m_flexTimeFrameZero; }
void SWGRadioPagingModSettings::setFlexTimeFrameZero(qint32 value) { m_flexTimeFrameZero = value; m_flexTimeFrameZeroIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexTimeZoneMinutes() { return m_flexTimeZoneMinutes; }
void SWGRadioPagingModSettings::setFlexTimeZoneMinutes(qint32 value) { m_flexTimeZoneMinutes = value; m_flexTimeZoneMinutesIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexTimeSendZone() { return m_flexTimeSendZone; }
void SWGRadioPagingModSettings::setFlexTimeSendZone(qint32 value) { m_flexTimeSendZone = value; m_flexTimeSendZoneIsSet = true; }
qint32 SWGRadioPagingModSettings::getFlexTimeDaylightSaving() { return m_flexTimeDaylightSaving; }
void SWGRadioPagingModSettings::setFlexTimeDaylightSaving(qint32 value) { m_flexTimeDaylightSaving = value; m_flexTimeDaylightSavingIsSet = true; }
qint64 SWGRadioPagingModSettings::getFlexTimeOffsetSeconds() { return m_flexTimeOffsetSeconds; }
void SWGRadioPagingModSettings::setFlexTimeOffsetSeconds(qint64 value) { m_flexTimeOffsetSeconds = value; m_flexTimeOffsetSecondsIsSet = true; }
qint32 SWGRadioPagingModSettings::getGscMode() { return m_gscMode; }
void SWGRadioPagingModSettings::setGscMode(qint32 value) { m_gscMode = value; m_gscModeIsSet = true; }
qint32 SWGRadioPagingModSettings::getGscInverted() { return m_gscInverted; }
void SWGRadioPagingModSettings::setGscInverted(qint32 value) { m_gscInverted = value; m_gscInvertedIsSet = true; }
qint32 SWGRadioPagingModSettings::getGscVoiceSeconds() { return m_gscVoiceSeconds; }
void SWGRadioPagingModSettings::setGscVoiceSeconds(qint32 value) { m_gscVoiceSeconds = value; m_gscVoiceSecondsIsSet = true; }
qint32 SWGRadioPagingModSettings::getMixPocsag() { return m_mixPocsag; }
void SWGRadioPagingModSettings::setMixPocsag(qint32 value) { m_mixPocsag = value; m_mixPocsagIsSet = true; }
float SWGRadioPagingModSettings::getPocsagDeviation() { return m_pocsagDeviation; }
void SWGRadioPagingModSettings::setPocsagDeviation(float value) { m_pocsagDeviation = value; m_pocsagDeviationIsSet = true; }
qint32 SWGRadioPagingModSettings::getMixFramesPerMinute() { return m_mixFramesPerMinute; }
void SWGRadioPagingModSettings::setMixFramesPerMinute(qint32 value) { m_mixFramesPerMinute = value; m_mixFramesPerMinuteIsSet = true; }
qint32 SWGRadioPagingModSettings::getMixShared() { return m_mixShared; }
void SWGRadioPagingModSettings::setMixShared(qint32 value) { m_mixShared = value; m_mixSharedIsSet = true; }
qint32 SWGRadioPagingModSettings::getMixSwitchMs512() { return m_mixSwitchMs512; }
void SWGRadioPagingModSettings::setMixSwitchMs512(qint32 value) { m_mixSwitchMs512 = value; m_mixSwitchMs512IsSet = true; }
qint32 SWGRadioPagingModSettings::getMixSwitchMs1200() { return m_mixSwitchMs1200; }
void SWGRadioPagingModSettings::setMixSwitchMs1200(qint32 value) { m_mixSwitchMs1200 = value; m_mixSwitchMs1200IsSet = true; }
qint32 SWGRadioPagingModSettings::getMixSwitchMs2400() { return m_mixSwitchMs2400; }
void SWGRadioPagingModSettings::setMixSwitchMs2400(qint32 value) { m_mixSwitchMs2400 = value; m_mixSwitchMs2400IsSet = true; }
qint32 SWGRadioPagingModSettings::getRgbColor() { return m_rgbColor; }
void SWGRadioPagingModSettings::setRgbColor(qint32 value) { m_rgbColor = value; m_rgbColorIsSet = true; }
QString* SWGRadioPagingModSettings::getChannelAlias() { return m_channelAlias; }
void SWGRadioPagingModSettings::setChannelAlias(QString* value) { if (m_channelAlias != value) { delete m_channelAlias; m_channelAlias = value; } m_channelAliasIsSet = true; }
QString* SWGRadioPagingModSettings::getGscVoiceDirectory() { return m_gscVoiceDirectory; }
void SWGRadioPagingModSettings::setGscVoiceDirectory(QString* value) { if (m_gscVoiceDirectory != value) { delete m_gscVoiceDirectory; m_gscVoiceDirectory = value; } m_gscVoiceDirectoryIsSet = true; }
qint32 SWGRadioPagingModSettings::getGscVoiceKeepDays() { return m_gscVoiceKeepDays; }
void SWGRadioPagingModSettings::setGscVoiceKeepDays(qint32 value) { m_gscVoiceKeepDays = value; m_gscVoiceKeepDaysIsSet = true; }
qint32 SWGRadioPagingModSettings::getGscVoiceKeepMin() { return m_gscVoiceKeepMin; }
void SWGRadioPagingModSettings::setGscVoiceKeepMin(qint32 value) { m_gscVoiceKeepMin = value; m_gscVoiceKeepMinIsSet = true; }
QString* SWGRadioPagingModSettings::getTitle() { return m_title; }
void SWGRadioPagingModSettings::setTitle(QString* value) { if (m_title != value) { delete m_title; m_title = value; } m_titleIsSet = true; }
qint32 SWGRadioPagingModSettings::getStreamIndex() { return m_streamIndex; }
void SWGRadioPagingModSettings::setStreamIndex(qint32 value) { m_streamIndex = value; m_streamIndexIsSet = true; }
qint32 SWGRadioPagingModSettings::getUseReverseApi() { return m_useReverseAPI; }
void SWGRadioPagingModSettings::setUseReverseApi(qint32 value) { m_useReverseAPI = value; m_useReverseAPIIsSet = true; }
QString* SWGRadioPagingModSettings::getReverseApiAddress() { return m_reverseAPIAddress; }
void SWGRadioPagingModSettings::setReverseApiAddress(QString* value) { if (m_reverseAPIAddress != value) { delete m_reverseAPIAddress; m_reverseAPIAddress = value; } m_reverseAPIAddressIsSet = true; }
qint32 SWGRadioPagingModSettings::getReverseApiPort() { return m_reverseAPIPort; }
void SWGRadioPagingModSettings::setReverseApiPort(qint32 value) { m_reverseAPIPort = value; m_reverseAPIPortIsSet = true; }
qint32 SWGRadioPagingModSettings::getReverseApiDeviceIndex() { return m_reverseAPIDeviceIndex; }
void SWGRadioPagingModSettings::setReverseApiDeviceIndex(qint32 value) { m_reverseAPIDeviceIndex = value; m_reverseAPIDeviceIndexIsSet = true; }
qint32 SWGRadioPagingModSettings::getReverseApiChannelIndex() { return m_reverseAPIChannelIndex; }
void SWGRadioPagingModSettings::setReverseApiChannelIndex(qint32 value) { m_reverseAPIChannelIndex = value; m_reverseAPIChannelIndexIsSet = true; }
SWGChannelMarker* SWGRadioPagingModSettings::getChannelMarker() { return m_channelMarker; }
void SWGRadioPagingModSettings::setChannelMarker(SWGChannelMarker* value) { if (m_channelMarker != value) { delete m_channelMarker; m_channelMarker = value; } m_channelMarkerIsSet = true; }
SWGRollupState* SWGRadioPagingModSettings::getRollupState() { return m_rollupState; }
void SWGRadioPagingModSettings::setRollupState(SWGRollupState* value) { if (m_rollupState != value) { delete m_rollupState; m_rollupState = value; } m_rollupStateIsSet = true; }
}
