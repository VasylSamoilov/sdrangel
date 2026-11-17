// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGMessageCenterRedecode.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGMessageCenterRedecode::SWGMessageCenterRedecode() { init(); }
SWGMessageCenterRedecode::SWGMessageCenterRedecode(QString* json) { init(); if (json) { fromJson(*json); } }
SWGMessageCenterRedecode::~SWGMessageCenterRedecode() { cleanup(); }
void SWGMessageCenterRedecode::cleanup()
{
    delete m_id; m_id = nullptr;
    delete m_charset; m_charset = nullptr;
    delete m_type; m_type = nullptr;
}
void SWGMessageCenterRedecode::init()
{
    cleanup();
    m_id = new QString(); m_idIsSet = false;
    m_charset = new SWGRadioPagingCharset(); m_charsetIsSet = false;
    m_type = new QString(); m_typeIsSet = false;
}
SWGMessageCenterRedecode* SWGMessageCenterRedecode::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGMessageCenterRedecode::fromJsonObject(QJsonObject& json)
{
    if (json.contains("id")) { ::SWGSDRangel::setValue(&m_id,json["id"],"QString","QString"); m_idIsSet = true; }
    if (json.contains("charset")) { ::SWGSDRangel::setValue(&m_charset,json["charset"],"SWGRadioPagingCharset","SWGRadioPagingCharset"); m_charsetIsSet = true; }
    if (json.contains("type")) { ::SWGSDRangel::setValue(&m_type,json["type"],"QString","QString"); m_typeIsSet = true; }
}
QString SWGMessageCenterRedecode::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGMessageCenterRedecode::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_id && (m_idIsSet || !m_id->isEmpty())) { toJsonValue(QString("id"),m_id,object,QString("QString")); }
    if (m_charset && (m_charsetIsSet || m_charset->isSet())) { toJsonValue(QString("charset"),m_charset,object,QString("SWGRadioPagingCharset")); }
    if (m_type && (m_typeIsSet || !m_type->isEmpty())) { toJsonValue(QString("type"),m_type,object,QString("QString")); }
    return object;
}
bool SWGMessageCenterRedecode::isSet()
{
    return m_idIsSet || m_charsetIsSet || m_typeIsSet;
}
QString* SWGMessageCenterRedecode::getId() { return m_id; }
void SWGMessageCenterRedecode::setId(QString* value) { if (m_id != value) { delete m_id; m_id = value; } m_idIsSet = true; }
SWGRadioPagingCharset* SWGMessageCenterRedecode::getCharset() { return m_charset; }
void SWGMessageCenterRedecode::setCharset(SWGRadioPagingCharset* value) { if (m_charset != value) { delete m_charset; m_charset = value; } m_charsetIsSet = true; }
QString* SWGMessageCenterRedecode::getType() { return m_type; }
void SWGMessageCenterRedecode::setType(QString* value) { if (m_type != value) { delete m_type; m_type = value; } m_typeIsSet = true; }
}
