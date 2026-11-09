// SPDX-License-Identifier: GPL-3.0-or-later
// Qt model for RadioPagingDemod.yaml, following the Swagger client model interface.
#ifndef SWGRadioPagingDemodSettings_H_
#define SWGRadioPagingDemodSettings_H_
#include <QJsonObject>
#include <QString>
#include "SWGObject.h"
#include "export.h"
#include "SWGChannelMarker.h"
#include "SWGRollupState.h"

namespace SWGSDRangel {
class SWG_API SWGRadioPagingDemodSettings : public SWGObject {
public:
    SWGRadioPagingDemodSettings();
    explicit SWGRadioPagingDemodSettings(QString* json);
    ~SWGRadioPagingDemodSettings() override;
    void init();
    void cleanup();
    QString asJson() override;
    QJsonObject* asJsonObject() override;
    void fromJsonObject(QJsonObject& json) override;
    SWGRadioPagingDemodSettings* fromJson(QString& json) override;
    bool isSet() override;
    qint64 getInputFrequencyOffset();
    void setInputFrequencyOffset(qint64 value);
    float getRfBandwidth();
    void setRfBandwidth(float value);
    qint32 getPocsag512Enabled();
    void setPocsag512Enabled(qint32 value);
    qint32 getPocsag1200Enabled();
    void setPocsag1200Enabled(qint32 value);
    qint32 getPocsag2400Enabled();
    void setPocsag2400Enabled(qint32 value);
    float getPocsagDeviation();
    void setPocsagDeviation(float value);
    qint32 getFlex1600x2Enabled();
    void setFlex1600x2Enabled(qint32 value);
    qint32 getFlex3200x2Enabled();
    void setFlex3200x2Enabled(qint32 value);
    qint32 getFlex3200x4Enabled();
    void setFlex3200x4Enabled(qint32 value);
    qint32 getFlex6400x4Enabled();
    void setFlex6400x4Enabled(qint32 value);
    float getFlexDeviation();
    void setFlexDeviation(float value);
    qint32 getGscEnabled();
    void setGscEnabled(qint32 value);
    float getGscDeviation();
    void setGscDeviation(float value);
    qint32 getGscNonBatterySaver();
    void setGscNonBatterySaver(qint32 value);
    qint32 getGscVoiceTimeout();
    void setGscVoiceTimeout(qint32 value);
    qint32 getNormalPolarity();
    void setNormalPolarity(qint32 value);
    qint32 getInvertedPolarity();
    void setInvertedPolarity(qint32 value);
    qint32 getRgbColor();
    void setRgbColor(qint32 value);
    QString* getTitle();
    void setTitle(QString* value);
    qint32 getStreamIndex();
    void setStreamIndex(qint32 value);
    qint32 getUseReverseApi();
    void setUseReverseApi(qint32 value);
    QString* getReverseApiAddress();
    void setReverseApiAddress(QString* value);
    qint32 getReverseApiPort();
    void setReverseApiPort(qint32 value);
    qint32 getReverseApiDeviceIndex();
    void setReverseApiDeviceIndex(qint32 value);
    qint32 getReverseApiChannelIndex();
    void setReverseApiChannelIndex(qint32 value);
    SWGChannelMarker* getChannelMarker();
    void setChannelMarker(SWGChannelMarker* value);
    SWGRollupState* getRollupState();
    void setRollupState(SWGRollupState* value);
private:
    qint64 m_inputFrequencyOffset = 0;
    bool m_inputFrequencyOffsetIsSet = false;
    float m_rfBandwidth = 0;
    bool m_rfBandwidthIsSet = false;
    qint32 m_pocsag512Enabled = 0;
    bool m_pocsag512EnabledIsSet = false;
    qint32 m_pocsag1200Enabled = 0;
    bool m_pocsag1200EnabledIsSet = false;
    qint32 m_pocsag2400Enabled = 0;
    bool m_pocsag2400EnabledIsSet = false;
    float m_pocsagDeviation = 0;
    bool m_pocsagDeviationIsSet = false;
    qint32 m_flex1600x2Enabled = 0;
    bool m_flex1600x2EnabledIsSet = false;
    qint32 m_flex3200x2Enabled = 0;
    bool m_flex3200x2EnabledIsSet = false;
    qint32 m_flex3200x4Enabled = 0;
    bool m_flex3200x4EnabledIsSet = false;
    qint32 m_flex6400x4Enabled = 0;
    bool m_flex6400x4EnabledIsSet = false;
    float m_flexDeviation = 0;
    bool m_flexDeviationIsSet = false;
    qint32 m_gscEnabled = 0;
    bool m_gscEnabledIsSet = false;
    float m_gscDeviation = 0;
    bool m_gscDeviationIsSet = false;
    qint32 m_gscNonBatterySaver = 0;
    bool m_gscNonBatterySaverIsSet = false;
    qint32 m_gscVoiceTimeout = 0;
    bool m_gscVoiceTimeoutIsSet = false;
    qint32 m_normalPolarity = 0;
    bool m_normalPolarityIsSet = false;
    qint32 m_invertedPolarity = 0;
    bool m_invertedPolarityIsSet = false;
    qint32 m_rgbColor = 0;
    bool m_rgbColorIsSet = false;
    QString* m_title = nullptr;
    bool m_titleIsSet = false;
    qint32 m_streamIndex = 0;
    bool m_streamIndexIsSet = false;
    qint32 m_useReverseAPI = 0;
    bool m_useReverseAPIIsSet = false;
    QString* m_reverseAPIAddress = nullptr;
    bool m_reverseAPIAddressIsSet = false;
    qint32 m_reverseAPIPort = 0;
    bool m_reverseAPIPortIsSet = false;
    qint32 m_reverseAPIDeviceIndex = 0;
    bool m_reverseAPIDeviceIndexIsSet = false;
    qint32 m_reverseAPIChannelIndex = 0;
    bool m_reverseAPIChannelIndexIsSet = false;
    SWGChannelMarker* m_channelMarker = nullptr;
    bool m_channelMarkerIsSet = false;
    SWGRollupState* m_rollupState = nullptr;
    bool m_rollupStateIsSet = false;
};
}
#endif
