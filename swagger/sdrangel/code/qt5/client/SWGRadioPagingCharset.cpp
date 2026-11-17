// SPDX-License-Identifier: GPL-3.0-or-later
#include "SWGRadioPagingCharset.h"
#include "SWGHelpers.h"
#include <QJsonDocument>
namespace SWGSDRangel {
SWGRadioPagingCharset::SWGRadioPagingCharset() { init(); }
SWGRadioPagingCharset::SWGRadioPagingCharset(QString* json) { init(); if (json) { fromJson(*json); } }
SWGRadioPagingCharset::~SWGRadioPagingCharset() { cleanup(); }
void SWGRadioPagingCharset::cleanup()
{
    delete m_vendor; m_vendor = nullptr;
    delete m_model; m_model = nullptr;
    delete m_codepage; m_codepage = nullptr;
}
void SWGRadioPagingCharset::init()
{
    cleanup();
    m_vendor = new QString(); m_vendorIsSet = false;
    m_model = new QString(); m_modelIsSet = false;
    m_codepage = new QString(); m_codepageIsSet = false;
}
SWGRadioPagingCharset* SWGRadioPagingCharset::fromJson(QString& json)
{
    auto object = QJsonDocument::fromJson(json.toUtf8()).object(); fromJsonObject(object); return this;
}
void SWGRadioPagingCharset::fromJsonObject(QJsonObject& json)
{
    if (json.contains("vendor")) { ::SWGSDRangel::setValue(&m_vendor,json["vendor"],"QString","QString"); m_vendorIsSet = true; }
    if (json.contains("model")) { ::SWGSDRangel::setValue(&m_model,json["model"],"QString","QString"); m_modelIsSet = true; }
    if (json.contains("codepage")) { ::SWGSDRangel::setValue(&m_codepage,json["codepage"],"QString","QString"); m_codepageIsSet = true; }
}
QString SWGRadioPagingCharset::asJson()
{
    auto* object = asJsonObject(); QString result = QJsonDocument(*object).toJson(); delete object; return result;
}
QJsonObject* SWGRadioPagingCharset::asJsonObject()
{
    auto* object = new QJsonObject();
    if (m_vendor && (m_vendorIsSet || !m_vendor->isEmpty())) { toJsonValue(QString("vendor"),m_vendor,object,QString("QString")); }
    if (m_model && (m_modelIsSet || !m_model->isEmpty())) { toJsonValue(QString("model"),m_model,object,QString("QString")); }
    if (m_codepage && (m_codepageIsSet || !m_codepage->isEmpty())) { toJsonValue(QString("codepage"),m_codepage,object,QString("QString")); }
    return object;
}
bool SWGRadioPagingCharset::isSet()
{
    return m_vendorIsSet || m_modelIsSet || m_codepageIsSet;
}
QString* SWGRadioPagingCharset::getVendor() { return m_vendor; }
void SWGRadioPagingCharset::setVendor(QString* value) { if (m_vendor != value) { delete m_vendor; m_vendor = value; } m_vendorIsSet = true; }
QString* SWGRadioPagingCharset::getModel() { return m_model; }
void SWGRadioPagingCharset::setModel(QString* value) { if (m_model != value) { delete m_model; m_model = value; } m_modelIsSet = true; }
QString* SWGRadioPagingCharset::getCodepage() { return m_codepage; }
void SWGRadioPagingCharset::setCodepage(QString* value) { if (m_codepage != value) { delete m_codepage; m_codepage = value; } m_codepageIsSet = true; }
}
