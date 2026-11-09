// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGRadioPagingDemodReport.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGRadioPagingDemodReport::SWGRadioPagingDemodReport() = default;
SWGRadioPagingDemodReport::SWGRadioPagingDemodReport(QString* json) { init(); if (json) { fromJson(*json); } }
SWGRadioPagingDemodReport::~SWGRadioPagingDemodReport() { cleanup(); }
void SWGRadioPagingDemodReport::cleanup()
{
    delete m_flexMode; m_flexMode = nullptr;
}
void SWGRadioPagingDemodReport::init()
{
    cleanup();
    m_channelPowerDB = 0; m_channelPowerDBIsSet = false;
    m_channelSampleRate = 0; m_channelSampleRateIsSet = false;
    m_pocsag512Sync = 0; m_pocsag512SyncIsSet = false;
    m_pocsag1200Sync = 0; m_pocsag1200SyncIsSet = false;
    m_pocsag2400Sync = 0; m_pocsag2400SyncIsSet = false;
    m_flexSync = 0; m_flexSyncIsSet = false;
    m_flexMode = new QString(); m_flexModeIsSet = false;
    m_flexQuality = 0; m_flexQualityIsSet = false;
    m_gscSync = 0; m_gscSyncIsSet = false;
    m_gscVoice = 0; m_gscVoiceIsSet = false;
    m_pocsagMessages = 0; m_pocsagMessagesIsSet = false;
    m_flexMessages = 0; m_flexMessagesIsSet = false;
    m_gscMessages = 0; m_gscMessagesIsSet = false;
}
SWGRadioPagingDemodReport* SWGRadioPagingDemodReport::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGRadioPagingDemodReport::fromJsonObject(QJsonObject& json)
{
    if (json.contains("channelPowerDB")) { ::SWGSDRangel::setValue(&m_channelPowerDB,json["channelPowerDB"],"float",""); m_channelPowerDBIsSet = true; }
    if (json.contains("channelSampleRate")) { ::SWGSDRangel::setValue(&m_channelSampleRate,json["channelSampleRate"],"qint32",""); m_channelSampleRateIsSet = true; }
    if (json.contains("pocsag512Sync")) { ::SWGSDRangel::setValue(&m_pocsag512Sync,json["pocsag512Sync"],"qint32",""); m_pocsag512SyncIsSet = true; }
    if (json.contains("pocsag1200Sync")) { ::SWGSDRangel::setValue(&m_pocsag1200Sync,json["pocsag1200Sync"],"qint32",""); m_pocsag1200SyncIsSet = true; }
    if (json.contains("pocsag2400Sync")) { ::SWGSDRangel::setValue(&m_pocsag2400Sync,json["pocsag2400Sync"],"qint32",""); m_pocsag2400SyncIsSet = true; }
    if (json.contains("flexSync")) { ::SWGSDRangel::setValue(&m_flexSync,json["flexSync"],"qint32",""); m_flexSyncIsSet = true; }
    if (json.contains("flexMode")) { ::SWGSDRangel::setValue(&m_flexMode,json["flexMode"],"QString","QString"); m_flexModeIsSet = true; }
    if (json.contains("flexQuality")) { ::SWGSDRangel::setValue(&m_flexQuality,json["flexQuality"],"float",""); m_flexQualityIsSet = true; }
    if (json.contains("gscSync")) { ::SWGSDRangel::setValue(&m_gscSync,json["gscSync"],"qint32",""); m_gscSyncIsSet = true; }
    if (json.contains("gscVoice")) { ::SWGSDRangel::setValue(&m_gscVoice,json["gscVoice"],"qint32",""); m_gscVoiceIsSet = true; }
    if (json.contains("pocsagMessages")) { ::SWGSDRangel::setValue(&m_pocsagMessages,json["pocsagMessages"],"qint64",""); m_pocsagMessagesIsSet = true; }
    if (json.contains("flexMessages")) { ::SWGSDRangel::setValue(&m_flexMessages,json["flexMessages"],"qint64",""); m_flexMessagesIsSet = true; }
    if (json.contains("gscMessages")) { ::SWGSDRangel::setValue(&m_gscMessages,json["gscMessages"],"qint64",""); m_gscMessagesIsSet = true; }
}
QString SWGRadioPagingDemodReport::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGRadioPagingDemodReport::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_channelPowerDBIsSet) { object->insert("channelPowerDB",QJsonValue(m_channelPowerDB)); }
    if (m_channelSampleRateIsSet) { object->insert("channelSampleRate",QJsonValue(m_channelSampleRate)); }
    if (m_pocsag512SyncIsSet) { object->insert("pocsag512Sync",QJsonValue(m_pocsag512Sync)); }
    if (m_pocsag1200SyncIsSet) { object->insert("pocsag1200Sync",QJsonValue(m_pocsag1200Sync)); }
    if (m_pocsag2400SyncIsSet) { object->insert("pocsag2400Sync",QJsonValue(m_pocsag2400Sync)); }
    if (m_flexSyncIsSet) { object->insert("flexSync",QJsonValue(m_flexSync)); }
    if (m_flexMode && (m_flexModeIsSet || !m_flexMode->isEmpty())) { toJsonValue(QString("flexMode"),m_flexMode,object,QString("QString")); }
    if (m_flexQualityIsSet) { object->insert("flexQuality",QJsonValue(m_flexQuality)); }
    if (m_gscSyncIsSet) { object->insert("gscSync",QJsonValue(m_gscSync)); }
    if (m_gscVoiceIsSet) { object->insert("gscVoice",QJsonValue(m_gscVoice)); }
    if (m_pocsagMessagesIsSet) { object->insert("pocsagMessages",QJsonValue(m_pocsagMessages)); }
    if (m_flexMessagesIsSet) { object->insert("flexMessages",QJsonValue(m_flexMessages)); }
    if (m_gscMessagesIsSet) { object->insert("gscMessages",QJsonValue(m_gscMessages)); }
    return object;
}
bool SWGRadioPagingDemodReport::isSet()
{
    return m_channelPowerDBIsSet || m_channelSampleRateIsSet || m_pocsag512SyncIsSet || m_pocsag1200SyncIsSet || m_pocsag2400SyncIsSet || m_flexSyncIsSet || m_flexModeIsSet || m_flexQualityIsSet || m_gscSyncIsSet || m_gscVoiceIsSet || m_pocsagMessagesIsSet || m_flexMessagesIsSet || m_gscMessagesIsSet;
}
float SWGRadioPagingDemodReport::getChannelPowerDb() { return m_channelPowerDB; }
void SWGRadioPagingDemodReport::setChannelPowerDb(float value) { m_channelPowerDB = value; m_channelPowerDBIsSet = true; }
qint32 SWGRadioPagingDemodReport::getChannelSampleRate() { return m_channelSampleRate; }
void SWGRadioPagingDemodReport::setChannelSampleRate(qint32 value) { m_channelSampleRate = value; m_channelSampleRateIsSet = true; }
qint32 SWGRadioPagingDemodReport::getPocsag512Sync() { return m_pocsag512Sync; }
void SWGRadioPagingDemodReport::setPocsag512Sync(qint32 value) { m_pocsag512Sync = value; m_pocsag512SyncIsSet = true; }
qint32 SWGRadioPagingDemodReport::getPocsag1200Sync() { return m_pocsag1200Sync; }
void SWGRadioPagingDemodReport::setPocsag1200Sync(qint32 value) { m_pocsag1200Sync = value; m_pocsag1200SyncIsSet = true; }
qint32 SWGRadioPagingDemodReport::getPocsag2400Sync() { return m_pocsag2400Sync; }
void SWGRadioPagingDemodReport::setPocsag2400Sync(qint32 value) { m_pocsag2400Sync = value; m_pocsag2400SyncIsSet = true; }
qint32 SWGRadioPagingDemodReport::getFlexSync() { return m_flexSync; }
void SWGRadioPagingDemodReport::setFlexSync(qint32 value) { m_flexSync = value; m_flexSyncIsSet = true; }
QString* SWGRadioPagingDemodReport::getFlexMode() { return m_flexMode; }
void SWGRadioPagingDemodReport::setFlexMode(QString* value) { if (m_flexMode != value) { delete m_flexMode; m_flexMode = value; } m_flexModeIsSet = true; }
float SWGRadioPagingDemodReport::getFlexQuality() { return m_flexQuality; }
void SWGRadioPagingDemodReport::setFlexQuality(float value) { m_flexQuality = value; m_flexQualityIsSet = true; }
qint32 SWGRadioPagingDemodReport::getGscSync() { return m_gscSync; }
void SWGRadioPagingDemodReport::setGscSync(qint32 value) { m_gscSync = value; m_gscSyncIsSet = true; }
qint32 SWGRadioPagingDemodReport::getGscVoice() { return m_gscVoice; }
void SWGRadioPagingDemodReport::setGscVoice(qint32 value) { m_gscVoice = value; m_gscVoiceIsSet = true; }
qint64 SWGRadioPagingDemodReport::getPocsagMessages() { return m_pocsagMessages; }
void SWGRadioPagingDemodReport::setPocsagMessages(qint64 value) { m_pocsagMessages = value; m_pocsagMessagesIsSet = true; }
qint64 SWGRadioPagingDemodReport::getFlexMessages() { return m_flexMessages; }
void SWGRadioPagingDemodReport::setFlexMessages(qint64 value) { m_flexMessages = value; m_flexMessagesIsSet = true; }
qint64 SWGRadioPagingDemodReport::getGscMessages() { return m_gscMessages; }
void SWGRadioPagingDemodReport::setGscMessages(qint64 value) { m_gscMessages = value; m_gscMessagesIsSet = true; }
}
