// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for MessageCenter.yaml, following the Swagger client model interface.
#ifndef SWGMessageCenterSettings_H_
#define SWGMessageCenterSettings_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGMessageCenterSettings : public SWGObject {
public:
    SWGMessageCenterSettings();
    explicit SWGMessageCenterSettings(QString* json);
    ~SWGMessageCenterSettings() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGMessageCenterSettings* fromJson(QString& json) override;
    bool isSet() override;
    QString* getTitle();
    void setTitle(QString* value);
    qint64 getRgbColor();
    void setRgbColor(qint64 value);
    qint32 getLogSize();
    void setLogSize(qint32 value);
    qint32 getClaimTimeoutSec();
    void setClaimTimeoutSec(qint32 value);
    qint32 getSilentSec();
    void setSilentSec(qint32 value);
    QString* getRoutes();
    void setRoutes(QString* value);
    QString* getSinks();
    void setSinks(QString* value);
    QString* getSources();
    void setSources(QString* value);
private:
    QString* m_title = nullptr;
    bool m_titleIsSet = false;
    qint64 m_rgbColor = 0;
    bool m_rgbColorIsSet = false;
    qint32 m_logSize = 0;
    bool m_logSizeIsSet = false;
    qint32 m_claimTimeoutSec = 0;
    bool m_claimTimeoutSecIsSet = false;
    qint32 m_silentSec = 0;
    bool m_silentSecIsSet = false;
    QString* m_routes = nullptr;
    bool m_routesIsSet = false;
    QString* m_sinks = nullptr;
    bool m_sinksIsSet = false;
    QString* m_sources = nullptr;
    bool m_sourcesIsSet = false;
};
}
#endif
