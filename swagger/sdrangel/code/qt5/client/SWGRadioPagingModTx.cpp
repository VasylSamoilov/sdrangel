// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGRadioPagingModTx.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGRadioPagingModTx::SWGRadioPagingModTx() = default;
SWGRadioPagingModTx::SWGRadioPagingModTx(QString* json) { init(); if (json) { fromJson(*json); } }
SWGRadioPagingModTx::~SWGRadioPagingModTx() { cleanup(); }
void SWGRadioPagingModTx::cleanup()
{
    delete m_protocol; m_protocol = nullptr;
    delete m_type; m_type = nullptr;
    delete m_text; m_text = nullptr;
    delete m_polarity; m_polarity = nullptr;
    delete m_id; m_id = nullptr;
}
void SWGRadioPagingModTx::init()
{
    cleanup();
    m_address = 0; m_addressIsSet = false;
    m_function = 0; m_functionIsSet = false;
    m_capcode = 0; m_capcodeIsSet = false;
    m_priority = 0; m_priorityIsSet = false;
    m_protocol = new QString(); m_protocolIsSet = false;
    m_type = new QString(); m_typeIsSet = false;
    m_text = new QString(); m_textIsSet = false;
    m_speed = 0; m_speedIsSet = false;
    m_polarity = new QString(); m_polarityIsSet = false;
    m_id = new QString(); m_idIsSet = false;
    m_shortType = 0; m_shortTypeIsSet = false;
    m_source = 0; m_sourceIsSet = false;
}
SWGRadioPagingModTx* SWGRadioPagingModTx::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGRadioPagingModTx::fromJsonObject(QJsonObject& json)
{
    if (json.contains("address")) { ::SWGSDRangel::setValue(&m_address,json["address"],"qint32",""); m_addressIsSet = true; }
    if (json.contains("function")) { ::SWGSDRangel::setValue(&m_function,json["function"],"qint32",""); m_functionIsSet = true; }
    if (json.contains("capcode")) { ::SWGSDRangel::setValue(&m_capcode,json["capcode"],"qint64",""); m_capcodeIsSet = true; }
    if (json.contains("priority")) { ::SWGSDRangel::setValue(&m_priority,json["priority"],"qint32",""); m_priorityIsSet = true; }
    if (json.contains("protocol")) { ::SWGSDRangel::setValue(&m_protocol,json["protocol"],"QString","QString"); m_protocolIsSet = true; }
    if (json.contains("type")) { ::SWGSDRangel::setValue(&m_type,json["type"],"QString","QString"); m_typeIsSet = true; }
    if (json.contains("text")) { ::SWGSDRangel::setValue(&m_text,json["text"],"QString","QString"); m_textIsSet = true; }
    if (json.contains("speed")) { ::SWGSDRangel::setValue(&m_speed,json["speed"],"qint32",""); m_speedIsSet = true; }
    if (json.contains("id")) { ::SWGSDRangel::setValue(&m_id,json["id"],"QString","QString"); m_idIsSet = true; }
    if (json.contains("polarity")) { ::SWGSDRangel::setValue(&m_polarity,json["polarity"],"QString","QString"); m_polarityIsSet = true; }
    if (json.contains("shortType")) { ::SWGSDRangel::setValue(&m_shortType,json["shortType"],"qint32",""); m_shortTypeIsSet = true; }
    if (json.contains("source")) { ::SWGSDRangel::setValue(&m_source,json["source"],"qint32",""); m_sourceIsSet = true; }
}
QString SWGRadioPagingModTx::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGRadioPagingModTx::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_addressIsSet) { object->insert("address",QJsonValue(m_address)); }
    if (m_functionIsSet) { object->insert("function",QJsonValue(m_function)); }
    if (m_capcodeIsSet) { object->insert("capcode",QJsonValue(m_capcode)); }
    if (m_priorityIsSet) { object->insert("priority",QJsonValue(m_priority)); }
    if (m_protocol && (m_protocolIsSet || !m_protocol->isEmpty())) { toJsonValue(QString("protocol"),m_protocol,object,QString("QString")); }
    if (m_type && (m_typeIsSet || !m_type->isEmpty())) { toJsonValue(QString("type"),m_type,object,QString("QString")); }
    if (m_text && (m_textIsSet || !m_text->isEmpty())) { toJsonValue(QString("text"),m_text,object,QString("QString")); }
    if (m_speedIsSet) { object->insert("speed",QJsonValue(m_speed)); }
    if (m_id && (m_idIsSet || !m_id->isEmpty())) { toJsonValue(QString("id"),m_id,object,QString("QString")); }
    if (m_polarity && (m_polarityIsSet || !m_polarity->isEmpty())) { toJsonValue(QString("polarity"),m_polarity,object,QString("QString")); }
    if (m_shortTypeIsSet) { object->insert("shortType",QJsonValue(m_shortType)); }
    if (m_sourceIsSet) { object->insert("source",QJsonValue(m_source)); }
    return object;
}
bool SWGRadioPagingModTx::isSet()
{
    return m_addressIsSet || m_functionIsSet || m_capcodeIsSet || m_priorityIsSet || m_protocolIsSet || m_typeIsSet || m_textIsSet
        || m_speedIsSet || m_polarityIsSet || m_idIsSet || m_shortTypeIsSet || m_sourceIsSet;
}
qint32 SWGRadioPagingModTx::getAddress() { return m_address; }
void SWGRadioPagingModTx::setAddress(qint32 value) { m_address = value; m_addressIsSet = true; }
qint32 SWGRadioPagingModTx::getFunction() { return m_function; }
void SWGRadioPagingModTx::setFunction(qint32 value) { m_function = value; m_functionIsSet = true; }
qint64 SWGRadioPagingModTx::getCapcode() { return m_capcode; }
void SWGRadioPagingModTx::setCapcode(qint64 value) { m_capcode = value; m_capcodeIsSet = true; }
qint32 SWGRadioPagingModTx::getPriority() { return m_priority; }
void SWGRadioPagingModTx::setPriority(qint32 value) { m_priority = value; m_priorityIsSet = true; }
QString* SWGRadioPagingModTx::getProtocol() { return m_protocol; }
void SWGRadioPagingModTx::setProtocol(QString* value) { if (m_protocol != value) { delete m_protocol; m_protocol = value; } m_protocolIsSet = true; }
QString* SWGRadioPagingModTx::getType() { return m_type; }
void SWGRadioPagingModTx::setType(QString* value) { if (m_type != value) { delete m_type; m_type = value; } m_typeIsSet = true; }
QString* SWGRadioPagingModTx::getText() { return m_text; }
void SWGRadioPagingModTx::setText(QString* value) { if (m_text != value) { delete m_text; m_text = value; } m_textIsSet = true; }
qint32 SWGRadioPagingModTx::getSpeed() { return m_speed; }
void SWGRadioPagingModTx::setSpeed(qint32 value) { m_speed = value; m_speedIsSet = true; }
QString* SWGRadioPagingModTx::getId() { return m_id; }
void SWGRadioPagingModTx::setId(QString* value) { if (m_id != value) { delete m_id; m_id = value; } m_idIsSet = true; }
QString* SWGRadioPagingModTx::getPolarity() { return m_polarity; }
void SWGRadioPagingModTx::setPolarity(QString* value) { if (m_polarity != value) { delete m_polarity; m_polarity = value; } m_polarityIsSet = true; }
qint32 SWGRadioPagingModTx::getShortType() { return m_shortType; }
void SWGRadioPagingModTx::setShortType(qint32 value) { m_shortType = value; m_shortTypeIsSet = true; }
qint32 SWGRadioPagingModTx::getSource() { return m_source; }
void SWGRadioPagingModTx::setSource(qint32 value) { m_source = value; m_sourceIsSet = true; }
}
