// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for MessageCenter.yaml, following the Swagger client model interface.
#ifndef SWGMessageCenterActions_H_
#define SWGMessageCenterActions_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "SWGMessageCenterPost.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGMessageCenterActions : public SWGObject {
public:
    SWGMessageCenterActions();
    explicit SWGMessageCenterActions(QString* json);
    ~SWGMessageCenterActions() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGMessageCenterActions* fromJson(QString& json) override;
    bool isSet() override;
    SWGMessageCenterPost* getPost();
    void setPost(SWGMessageCenterPost* value);
    QString* getRequeue();
    void setRequeue(QString* value);
    QString* getCancel();
    void setCancel(QString* value);
    qint32 getClearSent();
    void setClearSent(qint32 value);
private:
    SWGMessageCenterPost* m_post = nullptr;
    bool m_postIsSet = false;
    QString* m_requeue = nullptr;
    bool m_requeueIsSet = false;
    QString* m_cancel = nullptr;
    bool m_cancelIsSet = false;
    qint32 m_clearSent = 0;
    bool m_clearSentIsSet = false;
};
}
#endif
