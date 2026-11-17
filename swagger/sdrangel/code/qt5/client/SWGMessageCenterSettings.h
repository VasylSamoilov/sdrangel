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
    qint32 getHistoryEnabled();
    void setHistoryEnabled(qint32 value);
    qint32 getHistoryMaxMb();
    void setHistoryMaxMb(qint32 value);
    qint32 getClaimTimeoutSec();
    void setClaimTimeoutSec(qint32 value);
    qint32 getSilentSec();
    void setSilentSec(qint32 value);
    qint32 getUseReverseApi();
    void setUseReverseApi(qint32 value);
    QString* getReverseApiAddress();
    void setReverseApiAddress(QString* value);
    qint32 getReverseApiPort();
    void setReverseApiPort(qint32 value);
    qint32 getReverseApiFeatureSetIndex();
    void setReverseApiFeatureSetIndex(qint32 value);
    qint32 getReverseApiFeatureIndex();
    void setReverseApiFeatureIndex(qint32 value);
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
    qint32 m_historyEnabled = 0;
    bool m_historyEnabledIsSet = false;
    qint32 m_historyMaxMb = 0;
    bool m_historyMaxMbIsSet = false;
    qint32 m_claimTimeoutSec = 0;
    bool m_claimTimeoutSecIsSet = false;
    qint32 m_silentSec = 0;
    bool m_silentSecIsSet = false;
    qint32 m_useReverseApi = 0;
    bool m_useReverseApiIsSet = false;
    QString* m_reverseApiAddress = nullptr;
    bool m_reverseApiAddressIsSet = false;
    qint32 m_reverseApiPort = 0;
    bool m_reverseApiPortIsSet = false;
    qint32 m_reverseApiFeatureSetIndex = 0;
    bool m_reverseApiFeatureSetIndexIsSet = false;
    qint32 m_reverseApiFeatureIndex = 0;
    bool m_reverseApiFeatureIndexIsSet = false;
    QString* m_routes = nullptr;
    bool m_routesIsSet = false;
    QString* m_sinks = nullptr;
    bool m_sinksIsSet = false;
    QString* m_sources = nullptr;
    bool m_sourcesIsSet = false;
};
}
#endif
