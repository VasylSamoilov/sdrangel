// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for RadioPagingDemod.yaml, following the Swagger client model interface.
#ifndef SWGRadioPagingDemodReport_H_
#define SWGRadioPagingDemodReport_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGRadioPagingDemodReport : public SWGObject {
public:
    SWGRadioPagingDemodReport();
    explicit SWGRadioPagingDemodReport(QString* json);
    ~SWGRadioPagingDemodReport() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGRadioPagingDemodReport* fromJson(QString& json) override;
    bool isSet() override;
    float getChannelPowerDb();
    void setChannelPowerDb(float value);
    qint32 getChannelSampleRate();
    void setChannelSampleRate(qint32 value);
    qint32 getPocsag512Sync();
    void setPocsag512Sync(qint32 value);
    qint32 getPocsag1200Sync();
    void setPocsag1200Sync(qint32 value);
    qint32 getPocsag2400Sync();
    void setPocsag2400Sync(qint32 value);
    qint32 getFlexSync();
    void setFlexSync(qint32 value);
    QString* getFlexMode();
    void setFlexMode(QString* value);
    float getFlexQuality();
    void setFlexQuality(float value);
    qint32 getGscSync();
    void setGscSync(qint32 value);
    qint32 getGscVoice();
    void setGscVoice(qint32 value);
    qint64 getPocsagMessages();
    void setPocsagMessages(qint64 value);
    qint64 getFlexMessages();
    void setFlexMessages(qint64 value);
    qint64 getGscMessages();
    void setGscMessages(qint64 value);
private:
    float m_channelPowerDB = 0;
    bool m_channelPowerDBIsSet = false;
    qint32 m_channelSampleRate = 0;
    bool m_channelSampleRateIsSet = false;
    qint32 m_pocsag512Sync = 0;
    bool m_pocsag512SyncIsSet = false;
    qint32 m_pocsag1200Sync = 0;
    bool m_pocsag1200SyncIsSet = false;
    qint32 m_pocsag2400Sync = 0;
    bool m_pocsag2400SyncIsSet = false;
    qint32 m_flexSync = 0;
    bool m_flexSyncIsSet = false;
    QString* m_flexMode = nullptr;
    bool m_flexModeIsSet = false;
    float m_flexQuality = 0;
    bool m_flexQualityIsSet = false;
    qint32 m_gscSync = 0;
    bool m_gscSyncIsSet = false;
    qint32 m_gscVoice = 0;
    bool m_gscVoiceIsSet = false;
    qint64 m_pocsagMessages = 0;
    bool m_pocsagMessagesIsSet = false;
    qint64 m_flexMessages = 0;
    bool m_flexMessagesIsSet = false;
    qint64 m_gscMessages = 0;
    bool m_gscMessagesIsSet = false;
};
}
#endif
