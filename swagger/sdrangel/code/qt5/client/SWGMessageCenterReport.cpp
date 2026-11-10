// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGMessageCenterReport.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGMessageCenterReport::SWGMessageCenterReport() { init(); }
SWGMessageCenterReport::SWGMessageCenterReport(QString* json) { init(); if (json) { fromJson(*json); } }
SWGMessageCenterReport::~SWGMessageCenterReport() { cleanup(); }
void SWGMessageCenterReport::cleanup()
{
    delete m_endpoints; m_endpoints = nullptr;
    delete m_pages; m_pages = nullptr;
    delete m_log; m_log = nullptr;
    delete m_errors; m_errors = nullptr;
}
void SWGMessageCenterReport::init()
{
    cleanup();
    m_logged = 0; m_loggedIsSet = false;
    m_routed = 0; m_routedIsSet = false;
    m_endpoints = new QString(); m_endpointsIsSet = false;
    m_pages = new QString(); m_pagesIsSet = false;
    m_log = new QString(); m_logIsSet = false;
    m_errors = new QString(); m_errorsIsSet = false;
}
SWGMessageCenterReport* SWGMessageCenterReport::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGMessageCenterReport::fromJsonObject(QJsonObject& json)
{
    if (json.contains("logged")) { ::SWGSDRangel::setValue(&m_logged,json["logged"],"qint32",""); m_loggedIsSet = true; }
    if (json.contains("routed")) { ::SWGSDRangel::setValue(&m_routed,json["routed"],"qint32",""); m_routedIsSet = true; }
    if (json.contains("endpoints")) { ::SWGSDRangel::setValue(&m_endpoints,json["endpoints"],"QString","QString"); m_endpointsIsSet = true; }
    if (json.contains("pages")) { ::SWGSDRangel::setValue(&m_pages,json["pages"],"QString","QString"); m_pagesIsSet = true; }
    if (json.contains("log")) { ::SWGSDRangel::setValue(&m_log,json["log"],"QString","QString"); m_logIsSet = true; }
    if (json.contains("errors")) { ::SWGSDRangel::setValue(&m_errors,json["errors"],"QString","QString"); m_errorsIsSet = true; }
}
QString SWGMessageCenterReport::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGMessageCenterReport::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_loggedIsSet) { object->insert("logged",QJsonValue(m_logged)); }
    if (m_routedIsSet) { object->insert("routed",QJsonValue(m_routed)); }
    if (m_endpoints && (m_endpointsIsSet || !m_endpoints->isEmpty())) { toJsonValue(QString("endpoints"),m_endpoints,object,QString("QString")); }
    if (m_pages && (m_pagesIsSet || !m_pages->isEmpty())) { toJsonValue(QString("pages"),m_pages,object,QString("QString")); }
    if (m_log && (m_logIsSet || !m_log->isEmpty())) { toJsonValue(QString("log"),m_log,object,QString("QString")); }
    if (m_errors && (m_errorsIsSet || !m_errors->isEmpty())) { toJsonValue(QString("errors"),m_errors,object,QString("QString")); }
    return object;
}
bool SWGMessageCenterReport::isSet()
{
    return m_loggedIsSet || m_routedIsSet || m_endpointsIsSet || m_pagesIsSet || m_logIsSet || m_errorsIsSet;
}
qint32 SWGMessageCenterReport::getLogged() { return m_logged; }
void SWGMessageCenterReport::setLogged(qint32 value) { m_logged = value; m_loggedIsSet = true; }
qint32 SWGMessageCenterReport::getRouted() { return m_routed; }
void SWGMessageCenterReport::setRouted(qint32 value) { m_routed = value; m_routedIsSet = true; }
QString* SWGMessageCenterReport::getEndpoints() { return m_endpoints; }
void SWGMessageCenterReport::setEndpoints(QString* value) { if (m_endpoints != value) { delete m_endpoints; m_endpoints = value; } m_endpointsIsSet = true; }
QString* SWGMessageCenterReport::getPages() { return m_pages; }
void SWGMessageCenterReport::setPages(QString* value) { if (m_pages != value) { delete m_pages; m_pages = value; } m_pagesIsSet = true; }
QString* SWGMessageCenterReport::getLog() { return m_log; }
void SWGMessageCenterReport::setLog(QString* value) { if (m_log != value) { delete m_log; m_log = value; } m_logIsSet = true; }
QString* SWGMessageCenterReport::getErrors() { return m_errors; }
void SWGMessageCenterReport::setErrors(QString* value) { if (m_errors != value) { delete m_errors; m_errors = value; } m_errorsIsSet = true; }
}
