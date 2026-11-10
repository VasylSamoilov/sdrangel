// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for MessageCenter.yaml, following the Swagger client model interface.
#ifndef SWGMessageCenterReport_H_
#define SWGMessageCenterReport_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGMessageCenterReport : public SWGObject {
public:
    SWGMessageCenterReport();
    explicit SWGMessageCenterReport(QString* json);
    ~SWGMessageCenterReport() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGMessageCenterReport* fromJson(QString& json) override;
    bool isSet() override;
    qint32 getLogged();
    void setLogged(qint32 value);
    qint32 getRouted();
    void setRouted(qint32 value);
    QString* getEndpoints();
    void setEndpoints(QString* value);
    QString* getPages();
    void setPages(QString* value);
    QString* getLog();
    void setLog(QString* value);
    QString* getErrors();
    void setErrors(QString* value);
private:
    qint32 m_logged = 0;
    bool m_loggedIsSet = false;
    qint32 m_routed = 0;
    bool m_routedIsSet = false;
    QString* m_endpoints = nullptr;
    bool m_endpointsIsSet = false;
    QString* m_pages = nullptr;
    bool m_pagesIsSet = false;
    QString* m_log = nullptr;
    bool m_logIsSet = false;
    QString* m_errors = nullptr;
    bool m_errorsIsSet = false;
};
}
#endif
