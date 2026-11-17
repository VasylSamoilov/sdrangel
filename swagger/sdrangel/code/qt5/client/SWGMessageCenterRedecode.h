// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for MessageCenter.yaml, following the Swagger client model interface.
#ifndef SWGMessageCenterRedecode_H_
#define SWGMessageCenterRedecode_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "SWGRadioPagingCharset.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGMessageCenterRedecode : public SWGObject {
public:
    SWGMessageCenterRedecode();
    explicit SWGMessageCenterRedecode(QString* json);
    ~SWGMessageCenterRedecode() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGMessageCenterRedecode* fromJson(QString& json) override;
    bool isSet() override;
    QString* getId();
    void setId(QString* value);
    SWGRadioPagingCharset* getCharset();
    void setCharset(SWGRadioPagingCharset* value);
    QString* getType();
    void setType(QString* value);
private:
    QString* m_id = nullptr;
    bool m_idIsSet = false;
    SWGRadioPagingCharset* m_charset = nullptr;
    bool m_charsetIsSet = false;
    QString* m_type = nullptr;
    bool m_typeIsSet = false;
};
}
#endif
