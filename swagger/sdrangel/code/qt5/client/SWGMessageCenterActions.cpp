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
    delete m_redecode; m_redecode = nullptr;
    delete m_ingest; m_ingest = nullptr;
    delete m_importFile; m_importFile = nullptr;
}
void SWGMessageCenterActions::init()
{
    cleanup();
    m_post = new SWGMessageCenterPost(); m_postIsSet = false;
    m_requeue = new QString(); m_requeueIsSet = false;
    m_cancel = new QString(); m_cancelIsSet = false;
    m_clearSent = 0; m_clearSentIsSet = false;
    m_redecode = new SWGMessageCenterRedecode(); m_redecodeIsSet = false;
    m_ingest = new QString(); m_ingestIsSet = false;
    m_importFile = new QString(); m_importFileIsSet = false;
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
    if (json.contains("redecode")) { ::SWGSDRangel::setValue(&m_redecode,json["redecode"],"SWGMessageCenterRedecode","SWGMessageCenterRedecode"); m_redecodeIsSet = true; }
    if (json.contains("ingest")) { ::SWGSDRangel::setValue(&m_ingest,json["ingest"],"QString","QString"); m_ingestIsSet = true; }
    if (json.contains("importFile")) { ::SWGSDRangel::setValue(&m_importFile,json["importFile"],"QString","QString"); m_importFileIsSet = true; }
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
    if (m_redecode && (m_redecodeIsSet || m_redecode->isSet())) { toJsonValue(QString("redecode"),m_redecode,object,QString("SWGMessageCenterRedecode")); }
    if (m_ingest && (m_ingestIsSet || !m_ingest->isEmpty())) { toJsonValue(QString("ingest"),m_ingest,object,QString("QString")); }
    if (m_importFile && (m_importFileIsSet || !m_importFile->isEmpty())) { toJsonValue(QString("importFile"),m_importFile,object,QString("QString")); }
    return object;
}
bool SWGMessageCenterActions::isSet()
{
    return (m_post && m_post->isSet()) || m_requeueIsSet || m_cancelIsSet || m_clearSentIsSet || m_redecodeIsSet || m_ingestIsSet || m_importFileIsSet;
}
SWGMessageCenterPost* SWGMessageCenterActions::getPost() { return m_post; }
void SWGMessageCenterActions::setPost(SWGMessageCenterPost* value) { if (m_post != value) { delete m_post; m_post = value; } m_postIsSet = true; }
QString* SWGMessageCenterActions::getRequeue() { return m_requeue; }
void SWGMessageCenterActions::setRequeue(QString* value) { if (m_requeue != value) { delete m_requeue; m_requeue = value; } m_requeueIsSet = true; }
QString* SWGMessageCenterActions::getCancel() { return m_cancel; }
void SWGMessageCenterActions::setCancel(QString* value) { if (m_cancel != value) { delete m_cancel; m_cancel = value; } m_cancelIsSet = true; }
qint32 SWGMessageCenterActions::getClearSent() { return m_clearSent; }
void SWGMessageCenterActions::setClearSent(qint32 value) { m_clearSent = value; m_clearSentIsSet = true; }
SWGMessageCenterRedecode* SWGMessageCenterActions::getRedecode() { return m_redecode; }
void SWGMessageCenterActions::setRedecode(SWGMessageCenterRedecode* value) { if (m_redecode != value) { delete m_redecode; m_redecode = value; } m_redecodeIsSet = true; }
QString* SWGMessageCenterActions::getIngest() { return m_ingest; }
void SWGMessageCenterActions::setIngest(QString* value) { if (m_ingest != value) { delete m_ingest; m_ingest = value; } m_ingestIsSet = true; }
QString* SWGMessageCenterActions::getImportFile() { return m_importFile; }
void SWGMessageCenterActions::setImportFile(QString* value) { if (m_importFile != value) { delete m_importFile; m_importFile = value; } m_importFileIsSet = true; }
}
