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
    qint32 getPocsagEnabled();
    void setPocsagEnabled(qint32 value);
    float getPocsagDeviation();
    void setPocsagDeviation(float value);
    qint32 getFlexEnabled();
    void setFlexEnabled(qint32 value);
    float getFlexDeviation();
    void setFlexDeviation(float value);
    qint32 getGscEnabled();
    void setGscEnabled(qint32 value);
    float getGscDeviation();
    void setGscDeviation(float value);
    qint32 getGscVoiceTimeout();
    void setGscVoiceTimeout(qint32 value);
    qint32 getGscAudio();
    void setGscAudio(qint32 value);
    float getVolume();
    void setVolume(float value);
    qint32 getGscVoiceRecord();
    void setGscVoiceRecord(qint32 value);
    qint32 getGscVoiceKeepDays();
    void setGscVoiceKeepDays(qint32 value);
    qint32 getGscVoiceKeepMin();
    void setGscVoiceKeepMin(qint32 value);
    QString* getChannelAlias();
    void setChannelAlias(QString* value);
    QString* getGscVoiceDirectory();
    void setGscVoiceDirectory(QString* value);
    QString* getAudioDeviceName();
    void setAudioDeviceName(QString* value);
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
    qint32 m_pocsagEnabled = 0;
    bool m_pocsagEnabledIsSet = false;
    float m_pocsagDeviation = 0;
    bool m_pocsagDeviationIsSet = false;
    qint32 m_flexEnabled = 0;
    bool m_flexEnabledIsSet = false;
    float m_flexDeviation = 0;
    bool m_flexDeviationIsSet = false;
    qint32 m_gscEnabled = 0;
    bool m_gscEnabledIsSet = false;
    float m_gscDeviation = 0;
    bool m_gscDeviationIsSet = false;
    qint32 m_gscVoiceTimeout = 0;
    bool m_gscVoiceTimeoutIsSet = false;
    qint32 m_gscAudio = 0;
    bool m_gscAudioIsSet = false;
    float m_volume = 0;
    bool m_volumeIsSet = false;
    qint32 m_gscVoiceRecord = 0;
    bool m_gscVoiceRecordIsSet = false;
    qint32 m_gscVoiceKeepDays = 0;
    bool m_gscVoiceKeepDaysIsSet = false;
    qint32 m_gscVoiceKeepMin = 0;
    bool m_gscVoiceKeepMinIsSet = false;
    QString* m_channelAlias = nullptr;
    bool m_channelAliasIsSet = false;
    QString* m_gscVoiceDirectory = nullptr;
    bool m_gscVoiceDirectoryIsSet = false;
    QString* m_audioDeviceName = nullptr;
    bool m_audioDeviceNameIsSet = false;
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
