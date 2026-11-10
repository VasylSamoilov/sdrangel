// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGRadioPagingModReport.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGRadioPagingModReport::SWGRadioPagingModReport() = default;
SWGRadioPagingModReport::SWGRadioPagingModReport(QString* json) { init(); if (json) { fromJson(*json); } }
SWGRadioPagingModReport::~SWGRadioPagingModReport() { cleanup(); }
void SWGRadioPagingModReport::cleanup()
{
}
void SWGRadioPagingModReport::init()
{
    cleanup();
    m_channelPowerDB = 0; m_channelPowerDBIsSet = false;
    m_channelSampleRate = 0; m_channelSampleRateIsSet = false;
    m_state = 0; m_stateIsSet = false;
    m_queued = 0; m_queuedIsSet = false;
    m_inFlight = 0; m_inFlightIsSet = false;
    m_currentId = 0; m_currentIdIsSet = false;
    m_lastSentId = 0; m_lastSentIdIsSet = false;
    m_sent = 0; m_sentIsSet = false;
    m_dropped = 0; m_droppedIsSet = false;
    m_sampleCounter = 0; m_sampleCounterIsSet = false;
    m_ers = 0; m_ersIsSet = false;
    m_ersMs = 0; m_ersMsIsSet = false;
    m_ersCount = 0; m_ersCountIsSet = false;
}
SWGRadioPagingModReport* SWGRadioPagingModReport::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGRadioPagingModReport::fromJsonObject(QJsonObject& json)
{
    if (json.contains("channelPowerDB")) { ::SWGSDRangel::setValue(&m_channelPowerDB,json["channelPowerDB"],"float",""); m_channelPowerDBIsSet = true; }
    if (json.contains("channelSampleRate")) { ::SWGSDRangel::setValue(&m_channelSampleRate,json["channelSampleRate"],"qint32",""); m_channelSampleRateIsSet = true; }
    if (json.contains("state")) { ::SWGSDRangel::setValue(&m_state,json["state"],"qint32",""); m_stateIsSet = true; }
    if (json.contains("queued")) { ::SWGSDRangel::setValue(&m_queued,json["queued"],"qint32",""); m_queuedIsSet = true; }
    if (json.contains("inFlight")) { ::SWGSDRangel::setValue(&m_inFlight,json["inFlight"],"qint32",""); m_inFlightIsSet = true; }
    if (json.contains("currentId")) { ::SWGSDRangel::setValue(&m_currentId,json["currentId"],"qint64",""); m_currentIdIsSet = true; }
    if (json.contains("lastSentId")) { ::SWGSDRangel::setValue(&m_lastSentId,json["lastSentId"],"qint64",""); m_lastSentIdIsSet = true; }
    if (json.contains("sent")) { ::SWGSDRangel::setValue(&m_sent,json["sent"],"qint64",""); m_sentIsSet = true; }
    if (json.contains("dropped")) { ::SWGSDRangel::setValue(&m_dropped,json["dropped"],"qint64",""); m_droppedIsSet = true; }
    if (json.contains("sampleCounter")) { ::SWGSDRangel::setValue(&m_sampleCounter,json["sampleCounter"],"qint64",""); m_sampleCounterIsSet = true; }
    if (json.contains("ers")) { ::SWGSDRangel::setValue(&m_ers,json["ers"],"qint32",""); m_ersIsSet = true; }
    if (json.contains("ersMs")) { ::SWGSDRangel::setValue(&m_ersMs,json["ersMs"],"qint32",""); m_ersMsIsSet = true; }
    if (json.contains("ersCount")) { ::SWGSDRangel::setValue(&m_ersCount,json["ersCount"],"qint64",""); m_ersCountIsSet = true; }
}
QString SWGRadioPagingModReport::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGRadioPagingModReport::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_channelPowerDBIsSet) { object->insert("channelPowerDB",QJsonValue(m_channelPowerDB)); }
    if (m_channelSampleRateIsSet) { object->insert("channelSampleRate",QJsonValue(m_channelSampleRate)); }
    if (m_stateIsSet) { object->insert("state",QJsonValue(m_state)); }
    if (m_queuedIsSet) { object->insert("queued",QJsonValue(m_queued)); }
    if (m_inFlightIsSet) { object->insert("inFlight",QJsonValue(m_inFlight)); }
    if (m_currentIdIsSet) { object->insert("currentId",QJsonValue(m_currentId)); }
    if (m_lastSentIdIsSet) { object->insert("lastSentId",QJsonValue(m_lastSentId)); }
    if (m_sentIsSet) { object->insert("sent",QJsonValue(m_sent)); }
    if (m_droppedIsSet) { object->insert("dropped",QJsonValue(m_dropped)); }
    if (m_sampleCounterIsSet) { object->insert("sampleCounter",QJsonValue(m_sampleCounter)); }
    if (m_ersIsSet) { object->insert("ers",QJsonValue(m_ers)); }
    if (m_ersMsIsSet) { object->insert("ersMs",QJsonValue(m_ersMs)); }
    if (m_ersCountIsSet) { object->insert("ersCount",QJsonValue(m_ersCount)); }
    return object;
}
bool SWGRadioPagingModReport::isSet()
{
    return m_channelPowerDBIsSet || m_channelSampleRateIsSet || m_stateIsSet || m_queuedIsSet || m_inFlightIsSet
        || m_currentIdIsSet || m_lastSentIdIsSet || m_sentIsSet || m_droppedIsSet || m_sampleCounterIsSet
        || m_ersIsSet || m_ersMsIsSet || m_ersCountIsSet;
}
float SWGRadioPagingModReport::getChannelPowerDb() { return m_channelPowerDB; }
void SWGRadioPagingModReport::setChannelPowerDb(float value) { m_channelPowerDB = value; m_channelPowerDBIsSet = true; }
qint32 SWGRadioPagingModReport::getChannelSampleRate() { return m_channelSampleRate; }
void SWGRadioPagingModReport::setChannelSampleRate(qint32 value) { m_channelSampleRate = value; m_channelSampleRateIsSet = true; }
qint32 SWGRadioPagingModReport::getState() { return m_state; }
void SWGRadioPagingModReport::setState(qint32 value) { m_state = value; m_stateIsSet = true; }
qint32 SWGRadioPagingModReport::getQueued() { return m_queued; }
void SWGRadioPagingModReport::setQueued(qint32 value) { m_queued = value; m_queuedIsSet = true; }
qint32 SWGRadioPagingModReport::getInFlight() { return m_inFlight; }
void SWGRadioPagingModReport::setInFlight(qint32 value) { m_inFlight = value; m_inFlightIsSet = true; }
qint64 SWGRadioPagingModReport::getCurrentId() { return m_currentId; }
void SWGRadioPagingModReport::setCurrentId(qint64 value) { m_currentId = value; m_currentIdIsSet = true; }
qint64 SWGRadioPagingModReport::getLastSentId() { return m_lastSentId; }
void SWGRadioPagingModReport::setLastSentId(qint64 value) { m_lastSentId = value; m_lastSentIdIsSet = true; }
qint64 SWGRadioPagingModReport::getSent() { return m_sent; }
void SWGRadioPagingModReport::setSent(qint64 value) { m_sent = value; m_sentIsSet = true; }
qint64 SWGRadioPagingModReport::getDropped() { return m_dropped; }
void SWGRadioPagingModReport::setDropped(qint64 value) { m_dropped = value; m_droppedIsSet = true; }
qint64 SWGRadioPagingModReport::getSampleCounter() { return m_sampleCounter; }
void SWGRadioPagingModReport::setSampleCounter(qint64 value) { m_sampleCounter = value; m_sampleCounterIsSet = true; }
qint32 SWGRadioPagingModReport::getErs() { return m_ers; }
void SWGRadioPagingModReport::setErs(qint32 value) { m_ers = value; m_ersIsSet = true; }
qint32 SWGRadioPagingModReport::getErsMs() { return m_ersMs; }
void SWGRadioPagingModReport::setErsMs(qint32 value) { m_ersMs = value; m_ersMsIsSet = true; }
qint64 SWGRadioPagingModReport::getErsCount() { return m_ersCount; }
void SWGRadioPagingModReport::setErsCount(qint64 value) { m_ersCount = value; m_ersCountIsSet = true; }
}
