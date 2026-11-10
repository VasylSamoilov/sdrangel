// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for RadioPagingMod.yaml, following the Swagger client model interface.
#ifndef SWGRadioPagingModActions_H_
#define SWGRadioPagingModActions_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"
#include "SWGRadioPagingModTx.h"

namespace SWGSDRangel {
class SWG_API SWGRadioPagingModActions : public SWGObject {
public:
    SWGRadioPagingModActions();
    explicit SWGRadioPagingModActions(QString* json);
    ~SWGRadioPagingModActions() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGRadioPagingModActions* fromJson(QString& json) override;
    bool isSet() override;
    SWGRadioPagingModTx* getTx();
    void setTx(SWGRadioPagingModTx* value);
    qint32 getClear();
    void setClear(qint32 value);
    qint32 getAbort();
    void setAbort(qint32 value);
private:
    SWGRadioPagingModTx* m_tx = nullptr;
    bool m_txIsSet = false;
    qint32 m_clear = 0;
    bool m_clearIsSet = false;
    qint32 m_abort = 0;
    bool m_abortIsSet = false;
};
}
#endif
