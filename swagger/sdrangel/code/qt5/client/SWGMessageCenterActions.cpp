// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGMessageCenterActions.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGMessageCenterActions::SWGMessageCenterActions() { init(); }
SWGMessageCenterActions::SWGMessageCenterActions(QString* json) { init(); if (json) { fromJson(*json); } }
SWGMessageCenterActions::~SWGMessageCenterActions() { cleanup(); }
void SWGMessageCenterActions::cleanup()
{
    delete m_post; m_post = nullptr;
    delete m_requeue; m_requeue = nullptr;
    delete m_cancel; m_cancel = nullptr;
}
void SWGMessageCenterActions::init()
{
    cleanup();
    m_post = new SWGMessageCenterPost(); m_postIsSet = false;
    m_requeue = new QString(); m_requeueIsSet = false;
    m_cancel = new QString(); m_cancelIsSet = false;
    m_clearSent = 0; m_clearSentIsSet = false;
}
SWGMessageCenterActions* SWGMessageCenterActions::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGMessageCenterActions::fromJsonObject(QJsonObject& json)
{
    if (json.contains("post")) { ::SWGSDRangel::setValue(&m_post,json["post"],"SWGMessageCenterPost","SWGMessageCenterPost"); m_postIsSet = true; }
    if (json.contains("requeue")) { ::SWGSDRangel::setValue(&m_requeue,json["requeue"],"QString","QString"); m_requeueIsSet = true; }
    if (json.contains("cancel")) { ::SWGSDRangel::setValue(&m_cancel,json["cancel"],"QString","QString"); m_cancelIsSet = true; }
    if (json.contains("clearSent")) { ::SWGSDRangel::setValue(&m_clearSent,json["clearSent"],"qint32",""); m_clearSentIsSet = true; }
}
QString SWGMessageCenterActions::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGMessageCenterActions::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_post && m_post->isSet()) { toJsonValue(QString("post"),m_post,object,QString("SWGMessageCenterPost")); }
    if (m_requeue && (m_requeueIsSet || !m_requeue->isEmpty())) { toJsonValue(QString("requeue"),m_requeue,object,QString("QString")); }
    if (m_cancel && (m_cancelIsSet || !m_cancel->isEmpty())) { toJsonValue(QString("cancel"),m_cancel,object,QString("QString")); }
    if (m_clearSentIsSet) { object->insert("clearSent",QJsonValue(m_clearSent)); }
    return object;
}
bool SWGMessageCenterActions::isSet()
{
    return (m_post && m_post->isSet()) || m_requeueIsSet || m_cancelIsSet || m_clearSentIsSet;
}
SWGMessageCenterPost* SWGMessageCenterActions::getPost() { return m_post; }
void SWGMessageCenterActions::setPost(SWGMessageCenterPost* value) { if (m_post != value) { delete m_post; m_post = value; } m_postIsSet = true; }
QString* SWGMessageCenterActions::getRequeue() { return m_requeue; }
void SWGMessageCenterActions::setRequeue(QString* value) { if (m_requeue != value) { delete m_requeue; m_requeue = value; } m_requeueIsSet = true; }
QString* SWGMessageCenterActions::getCancel() { return m_cancel; }
void SWGMessageCenterActions::setCancel(QString* value) { if (m_cancel != value) { delete m_cancel; m_cancel = value; } m_cancelIsSet = true; }
qint32 SWGMessageCenterActions::getClearSent() { return m_clearSent; }
void SWGMessageCenterActions::setClearSent(qint32 value) { m_clearSent = value; m_clearSentIsSet = true; }
}
