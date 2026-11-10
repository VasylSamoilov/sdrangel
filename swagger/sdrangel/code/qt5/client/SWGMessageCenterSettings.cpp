// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGMessageCenterSettings.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGMessageCenterSettings::SWGMessageCenterSettings() { init(); }
SWGMessageCenterSettings::SWGMessageCenterSettings(QString* json) { init(); if (json) { fromJson(*json); } }
SWGMessageCenterSettings::~SWGMessageCenterSettings() { cleanup(); }
void SWGMessageCenterSettings::cleanup()
{
    delete m_title; m_title = nullptr;
    delete m_routes; m_routes = nullptr;
    delete m_sinks; m_sinks = nullptr;
    delete m_sources; m_sources = nullptr;
}
void SWGMessageCenterSettings::init()
{
    cleanup();
    m_title = new QString(); m_titleIsSet = false;
    m_rgbColor = 0; m_rgbColorIsSet = false;
    m_logSize = 0; m_logSizeIsSet = false;
    m_claimTimeoutSec = 0; m_claimTimeoutSecIsSet = false;
    m_silentSec = 0; m_silentSecIsSet = false;
    m_routes = new QString(); m_routesIsSet = false;
    m_sinks = new QString(); m_sinksIsSet = false;
    m_sources = new QString(); m_sourcesIsSet = false;
}
SWGMessageCenterSettings* SWGMessageCenterSettings::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGMessageCenterSettings::fromJsonObject(QJsonObject& json)
{
    if (json.contains("title")) { ::SWGSDRangel::setValue(&m_title,json["title"],"QString","QString"); m_titleIsSet = true; }
    if (json.contains("rgbColor")) { ::SWGSDRangel::setValue(&m_rgbColor,json["rgbColor"],"qint64",""); m_rgbColorIsSet = true; }
    if (json.contains("logSize")) { ::SWGSDRangel::setValue(&m_logSize,json["logSize"],"qint32",""); m_logSizeIsSet = true; }
    if (json.contains("claimTimeoutSec")) { ::SWGSDRangel::setValue(&m_claimTimeoutSec,json["claimTimeoutSec"],"qint32",""); m_claimTimeoutSecIsSet = true; }
    if (json.contains("silentSec")) { ::SWGSDRangel::setValue(&m_silentSec,json["silentSec"],"qint32",""); m_silentSecIsSet = true; }
    if (json.contains("routes")) { ::SWGSDRangel::setValue(&m_routes,json["routes"],"QString","QString"); m_routesIsSet = true; }
    if (json.contains("sinks")) { ::SWGSDRangel::setValue(&m_sinks,json["sinks"],"QString","QString"); m_sinksIsSet = true; }
    if (json.contains("sources")) { ::SWGSDRangel::setValue(&m_sources,json["sources"],"QString","QString"); m_sourcesIsSet = true; }
}
QString SWGMessageCenterSettings::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGMessageCenterSettings::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_title && (m_titleIsSet || !m_title->isEmpty())) { toJsonValue(QString("title"),m_title,object,QString("QString")); }
    if (m_rgbColorIsSet) { object->insert("rgbColor",QJsonValue(m_rgbColor)); }
    if (m_logSizeIsSet) { object->insert("logSize",QJsonValue(m_logSize)); }
    if (m_claimTimeoutSecIsSet) { object->insert("claimTimeoutSec",QJsonValue(m_claimTimeoutSec)); }
    if (m_silentSecIsSet) { object->insert("silentSec",QJsonValue(m_silentSec)); }
    if (m_routes && (m_routesIsSet || !m_routes->isEmpty())) { toJsonValue(QString("routes"),m_routes,object,QString("QString")); }
    if (m_sinks && (m_sinksIsSet || !m_sinks->isEmpty())) { toJsonValue(QString("sinks"),m_sinks,object,QString("QString")); }
    if (m_sources && (m_sourcesIsSet || !m_sources->isEmpty())) { toJsonValue(QString("sources"),m_sources,object,QString("QString")); }
    return object;
}
bool SWGMessageCenterSettings::isSet()
{
    return m_titleIsSet || m_rgbColorIsSet || m_logSizeIsSet || m_claimTimeoutSecIsSet || m_silentSecIsSet || m_routesIsSet
        || m_sinksIsSet || m_sourcesIsSet;
}
QString* SWGMessageCenterSettings::getTitle() { return m_title; }
void SWGMessageCenterSettings::setTitle(QString* value) { if (m_title != value) { delete m_title; m_title = value; } m_titleIsSet = true; }
qint64 SWGMessageCenterSettings::getRgbColor() { return m_rgbColor; }
void SWGMessageCenterSettings::setRgbColor(qint64 value) { m_rgbColor = value; m_rgbColorIsSet = true; }
qint32 SWGMessageCenterSettings::getLogSize() { return m_logSize; }
void SWGMessageCenterSettings::setLogSize(qint32 value) { m_logSize = value; m_logSizeIsSet = true; }
qint32 SWGMessageCenterSettings::getClaimTimeoutSec() { return m_claimTimeoutSec; }
void SWGMessageCenterSettings::setClaimTimeoutSec(qint32 value) { m_claimTimeoutSec = value; m_claimTimeoutSecIsSet = true; }
qint32 SWGMessageCenterSettings::getSilentSec() { return m_silentSec; }
void SWGMessageCenterSettings::setSilentSec(qint32 value) { m_silentSec = value; m_silentSecIsSet = true; }
QString* SWGMessageCenterSettings::getRoutes() { return m_routes; }
void SWGMessageCenterSettings::setRoutes(QString* value) { if (m_routes != value) { delete m_routes; m_routes = value; } m_routesIsSet = true; }
QString* SWGMessageCenterSettings::getSinks() { return m_sinks; }
void SWGMessageCenterSettings::setSinks(QString* value) { if (m_sinks != value) { delete m_sinks; m_sinks = value; } m_sinksIsSet = true; }
QString* SWGMessageCenterSettings::getSources() { return m_sources; }
void SWGMessageCenterSettings::setSources(QString* value) { if (m_sources != value) { delete m_sources; m_sources = value; } m_sourcesIsSet = true; }
}
