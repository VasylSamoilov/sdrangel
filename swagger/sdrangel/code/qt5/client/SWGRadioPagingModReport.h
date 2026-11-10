// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for RadioPagingMod.yaml, following the Swagger client model interface.
#ifndef SWGRadioPagingModReport_H_
#define SWGRadioPagingModReport_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"

namespace SWGSDRangel {
class SWG_API SWGRadioPagingModReport : public SWGObject {
public:
    SWGRadioPagingModReport();
    explicit SWGRadioPagingModReport(QString* json);
    ~SWGRadioPagingModReport() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGRadioPagingModReport* fromJson(QString& json) override;
    bool isSet() override;
    float getChannelPowerDb();
    void setChannelPowerDb(float value);
    qint32 getChannelSampleRate();
    void setChannelSampleRate(qint32 value);
    qint32 getState();
    void setState(qint32 value);
    qint32 getQueued();
    void setQueued(qint32 value);
    qint32 getInFlight();
    void setInFlight(qint32 value);
    qint64 getCurrentId();
    void setCurrentId(qint64 value);
    qint64 getLastSentId();
    void setLastSentId(qint64 value);
    qint64 getSent();
    void setSent(qint64 value);
    qint64 getDropped();
    void setDropped(qint64 value);
    qint64 getSampleCounter();
    void setSampleCounter(qint64 value);
    qint32 getErs();
    void setErs(qint32 value);
    qint32 getErsMs();
    void setErsMs(qint32 value);
    qint64 getErsCount();
    void setErsCount(qint64 value);
private:
    float m_channelPowerDB = 0;
    bool m_channelPowerDBIsSet = false;
    qint32 m_channelSampleRate = 0;
    bool m_channelSampleRateIsSet = false;
    qint32 m_state = 0;
    bool m_stateIsSet = false;
    qint32 m_queued = 0;
    bool m_queuedIsSet = false;
    qint32 m_inFlight = 0;
    bool m_inFlightIsSet = false;
    qint64 m_currentId = 0;
    bool m_currentIdIsSet = false;
    qint64 m_lastSentId = 0;
    bool m_lastSentIdIsSet = false;
    qint64 m_sent = 0;
    bool m_sentIsSet = false;
    qint64 m_dropped = 0;
    bool m_droppedIsSet = false;
    qint64 m_sampleCounter = 0;
    bool m_sampleCounterIsSet = false;
    qint32 m_ers = 0;
    bool m_ersIsSet = false;
    qint32 m_ersMs = 0;
    bool m_ersMsIsSet = false;
    qint64 m_ersCount = 0;
    bool m_ersCountIsSet = false;
};
}
#endif
