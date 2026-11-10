// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGMessageCenterPost.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGMessageCenterPost::SWGMessageCenterPost() { init(); }
SWGMessageCenterPost::SWGMessageCenterPost(QString* json) { init(); if (json) { fromJson(*json); } }
SWGMessageCenterPost::~SWGMessageCenterPost() { cleanup(); }
void SWGMessageCenterPost::cleanup()
{
    delete m_id; m_id = nullptr;
    delete m_transmitter; m_transmitter = nullptr;
    delete m_protocol; m_protocol = nullptr;
    delete m_address; m_address = nullptr;
    delete m_type; m_type = nullptr;
    delete m_text; m_text = nullptr;
    delete m_voiceFile; m_voiceFile = nullptr;
    delete m_polarity; m_polarity = nullptr;
}
void SWGMessageCenterPost::init()
{
    cleanup();
    m_id = new QString(); m_idIsSet = false;
    m_transmitter = new QString(); m_transmitterIsSet = false;
    m_protocol = new QString(); m_protocolIsSet = false;
    m_address = new QString(); m_addressIsSet = false;
    m_function = 0; m_functionIsSet = false;
    m_priority = 0; m_priorityIsSet = false;
    m_type = new QString(); m_typeIsSet = false;
    m_text = new QString(); m_textIsSet = false;
    m_voiceFile = new QString(); m_voiceFileIsSet = false;
    m_speed = 0; m_speedIsSet = false;
    m_polarity = new QString(); m_polarityIsSet = false;
    m_shortType = 0; m_shortTypeIsSet = false;
    m_source = 0; m_sourceIsSet = false;
}
SWGMessageCenterPost* SWGMessageCenterPost::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGMessageCenterPost::fromJsonObject(QJsonObject& json)
{
    if (json.contains("id")) { ::SWGSDRangel::setValue(&m_id,json["id"],"QString","QString"); m_idIsSet = true; }
    if (json.contains("transmitter")) { ::SWGSDRangel::setValue(&m_transmitter,json["transmitter"],"QString","QString"); m_transmitterIsSet = true; }
    if (json.contains("protocol")) { ::SWGSDRangel::setValue(&m_protocol,json["protocol"],"QString","QString"); m_protocolIsSet = true; }
    if (json.contains("address")) { ::SWGSDRangel::setValue(&m_address,json["address"],"QString","QString"); m_addressIsSet = true; }
    if (json.contains("function")) { ::SWGSDRangel::setValue(&m_function,json["function"],"qint32",""); m_functionIsSet = true; }
    if (json.contains("priority")) { ::SWGSDRangel::setValue(&m_priority,json["priority"],"qint32",""); m_priorityIsSet = true; }
    if (json.contains("type")) { ::SWGSDRangel::setValue(&m_type,json["type"],"QString","QString"); m_typeIsSet = true; }
    if (json.contains("text")) { ::SWGSDRangel::setValue(&m_text,json["text"],"QString","QString"); m_textIsSet = true; }
    if (json.contains("voiceFile")) { ::SWGSDRangel::setValue(&m_voiceFile,json["voiceFile"],"QString","QString"); m_voiceFileIsSet = true; }
    if (json.contains("speed")) { ::SWGSDRangel::setValue(&m_speed,json["speed"],"qint32",""); m_speedIsSet = true; }
    if (json.contains("polarity")) { ::SWGSDRangel::setValue(&m_polarity,json["polarity"],"QString","QString"); m_polarityIsSet = true; }
    if (json.contains("shortType")) { ::SWGSDRangel::setValue(&m_shortType,json["shortType"],"qint32",""); m_shortTypeIsSet = true; }
    if (json.contains("source")) { ::SWGSDRangel::setValue(&m_source,json["source"],"qint32",""); m_sourceIsSet = true; }
}
QString SWGMessageCenterPost::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGMessageCenterPost::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_id && (m_idIsSet || !m_id->isEmpty())) { toJsonValue(QString("id"),m_id,object,QString("QString")); }
    if (m_transmitter && (m_transmitterIsSet || !m_transmitter->isEmpty())) { toJsonValue(QString("transmitter"),m_transmitter,object,QString("QString")); }
    if (m_protocol && (m_protocolIsSet || !m_protocol->isEmpty())) { toJsonValue(QString("protocol"),m_protocol,object,QString("QString")); }
    if (m_address && (m_addressIsSet || !m_address->isEmpty())) { toJsonValue(QString("address"),m_address,object,QString("QString")); }
    if (m_functionIsSet) { object->insert("function",QJsonValue(m_function)); }
    if (m_priorityIsSet) { object->insert("priority",QJsonValue(m_priority)); }
    if (m_type && (m_typeIsSet || !m_type->isEmpty())) { toJsonValue(QString("type"),m_type,object,QString("QString")); }
    if (m_text && (m_textIsSet || !m_text->isEmpty())) { toJsonValue(QString("text"),m_text,object,QString("QString")); }
    if (m_voiceFile && (m_voiceFileIsSet || !m_voiceFile->isEmpty())) { toJsonValue(QString("voiceFile"),m_voiceFile,object,QString("QString")); }
    if (m_speedIsSet) { object->insert("speed",QJsonValue(m_speed)); }
    if (m_polarity && (m_polarityIsSet || !m_polarity->isEmpty())) { toJsonValue(QString("polarity"),m_polarity,object,QString("QString")); }
    if (m_shortTypeIsSet) { object->insert("shortType",QJsonValue(m_shortType)); }
    if (m_sourceIsSet) { object->insert("source",QJsonValue(m_source)); }
    return object;
}
bool SWGMessageCenterPost::isSet()
{
    return m_idIsSet || m_transmitterIsSet || m_protocolIsSet || m_addressIsSet || m_functionIsSet || m_priorityIsSet || m_typeIsSet
        || m_textIsSet || m_voiceFileIsSet || m_speedIsSet || m_polarityIsSet || m_shortTypeIsSet || m_sourceIsSet;
}
QString* SWGMessageCenterPost::getId() { return m_id; }
void SWGMessageCenterPost::setId(QString* value) { if (m_id != value) { delete m_id; m_id = value; } m_idIsSet = true; }
QString* SWGMessageCenterPost::getTransmitter() { return m_transmitter; }
void SWGMessageCenterPost::setTransmitter(QString* value) { if (m_transmitter != value) { delete m_transmitter; m_transmitter = value; } m_transmitterIsSet = true; }
QString* SWGMessageCenterPost::getProtocol() { return m_protocol; }
void SWGMessageCenterPost::setProtocol(QString* value) { if (m_protocol != value) { delete m_protocol; m_protocol = value; } m_protocolIsSet = true; }
QString* SWGMessageCenterPost::getAddress() { return m_address; }
void SWGMessageCenterPost::setAddress(QString* value) { if (m_address != value) { delete m_address; m_address = value; } m_addressIsSet = true; }
qint32 SWGMessageCenterPost::getFunction() { return m_function; }
void SWGMessageCenterPost::setFunction(qint32 value) { m_function = value; m_functionIsSet = true; }
qint32 SWGMessageCenterPost::getPriority() { return m_priority; }
void SWGMessageCenterPost::setPriority(qint32 value) { m_priority = value; m_priorityIsSet = true; }
QString* SWGMessageCenterPost::getType() { return m_type; }
void SWGMessageCenterPost::setType(QString* value) { if (m_type != value) { delete m_type; m_type = value; } m_typeIsSet = true; }
QString* SWGMessageCenterPost::getText() { return m_text; }
void SWGMessageCenterPost::setText(QString* value) { if (m_text != value) { delete m_text; m_text = value; } m_textIsSet = true; }
QString* SWGMessageCenterPost::getVoiceFile() { return m_voiceFile; }
void SWGMessageCenterPost::setVoiceFile(QString* value) { if (m_voiceFile != value) { delete m_voiceFile; m_voiceFile = value; } m_voiceFileIsSet = true; }
qint32 SWGMessageCenterPost::getSpeed() { return m_speed; }
void SWGMessageCenterPost::setSpeed(qint32 value) { m_speed = value; m_speedIsSet = true; }
QString* SWGMessageCenterPost::getPolarity() { return m_polarity; }
void SWGMessageCenterPost::setPolarity(QString* value) { if (m_polarity != value) { delete m_polarity; m_polarity = value; } m_polarityIsSet = true; }
qint32 SWGMessageCenterPost::getShortType() { return m_shortType; }
void SWGMessageCenterPost::setShortType(qint32 value) { m_shortType = value; m_shortTypeIsSet = true; }
qint32 SWGMessageCenterPost::getSource() { return m_source; }
void SWGMessageCenterPost::setSource(qint32 value) { m_source = value; m_sourceIsSet = true; }
}
