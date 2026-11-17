// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for RadioPagingCharset.yaml, following the Swagger client model interface.
#ifndef SWGRadioPagingCharset_H_
#define SWGRadioPagingCharset_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGRadioPagingCharset : public SWGObject {
public:
    SWGRadioPagingCharset();
    explicit SWGRadioPagingCharset(QString* json);
    ~SWGRadioPagingCharset() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGRadioPagingCharset* fromJson(QString& json) override;
    bool isSet() override;
    QString* getVendor();
    void setVendor(QString* value);
    QString* getModel();
    void setModel(QString* value);
    QString* getCodepage();
    void setCodepage(QString* value);
private:
    QString* m_vendor = nullptr;
    bool m_vendorIsSet = false;
    QString* m_model = nullptr;
    bool m_modelIsSet = false;
    QString* m_codepage = nullptr;
    bool m_codepageIsSet = false;
};
}
#endif
