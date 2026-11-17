// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGRadioPagingModActions.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGRadioPagingModActions::SWGRadioPagingModActions() = default;
SWGRadioPagingModActions::SWGRadioPagingModActions(QString* json) { init(); if (json) { fromJson(*json); } }
SWGRadioPagingModActions::~SWGRadioPagingModActions() { cleanup(); }
void SWGRadioPagingModActions::cleanup()
{
    delete m_tx; m_tx = nullptr;
}
void SWGRadioPagingModActions::init()
{
    cleanup();
    m_tx = new SWGRadioPagingModTx(); m_txIsSet = false;
    m_clear = 0; m_clearIsSet = false;
    m_abort = 0; m_abortIsSet = false;
    m_systemEvent = 0; m_systemEventIsSet = false;
    m_systemEventFrames = 0; m_systemEventFramesIsSet = false;
}
SWGRadioPagingModActions* SWGRadioPagingModActions::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGRadioPagingModActions::fromJsonObject(QJsonObject& json)
{
    if (json.contains("tx")) { ::SWGSDRangel::setValue(&m_tx,json["tx"],"SWGRadioPagingModTx","SWGRadioPagingModTx"); m_txIsSet = true; }
    if (json.contains("clear")) { ::SWGSDRangel::setValue(&m_clear,json["clear"],"qint32",""); m_clearIsSet = true; }
    if (json.contains("abort")) { ::SWGSDRangel::setValue(&m_abort,json["abort"],"qint32",""); m_abortIsSet = true; }
    if (json.contains("systemEvent")) { ::SWGSDRangel::setValue(&m_systemEvent,json["systemEvent"],"qint32",""); m_systemEventIsSet = true; }
    if (json.contains("systemEventFrames")) { ::SWGSDRangel::setValue(&m_systemEventFrames,json["systemEventFrames"],"qint32",""); m_systemEventFramesIsSet = true; }
}
QString SWGRadioPagingModActions::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGRadioPagingModActions::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_tx && (m_txIsSet || m_tx->isSet())) { toJsonValue(QString("tx"),m_tx,object,QString("SWGRadioPagingModTx")); }
    if (m_clearIsSet) { object->insert("clear",QJsonValue(m_clear)); }
    if (m_abortIsSet) { object->insert("abort",QJsonValue(m_abort)); }
    if (m_systemEventIsSet) { object->insert("systemEvent",QJsonValue(m_systemEvent)); }
    if (m_systemEventFramesIsSet) { object->insert("systemEventFrames",QJsonValue(m_systemEventFrames)); }
    return object;
}
bool SWGRadioPagingModActions::isSet()
{
    return m_txIsSet || m_clearIsSet || m_abortIsSet || m_systemEventIsSet || m_systemEventFramesIsSet;
}
SWGRadioPagingModTx* SWGRadioPagingModActions::getTx() { return m_tx; }
void SWGRadioPagingModActions::setTx(SWGRadioPagingModTx* value) { if (m_tx != value) { delete m_tx; m_tx = value; } m_txIsSet = true; }
qint32 SWGRadioPagingModActions::getClear() { return m_clear; }
void SWGRadioPagingModActions::setClear(qint32 value) { m_clear = value; m_clearIsSet = true; }
qint32 SWGRadioPagingModActions::getAbort() { return m_abort; }
void SWGRadioPagingModActions::setAbort(qint32 value) { m_abort = value; m_abortIsSet = true; }
qint32 SWGRadioPagingModActions::getSystemEvent() { return m_systemEvent; }
void SWGRadioPagingModActions::setSystemEvent(qint32 value) { m_systemEvent = value; m_systemEventIsSet = true; }
qint32 SWGRadioPagingModActions::getSystemEventFrames() { return m_systemEventFrames; }
void SWGRadioPagingModActions::setSystemEventFrames(qint32 value) { m_systemEventFrames = value; m_systemEventFramesIsSet = true; }
}
