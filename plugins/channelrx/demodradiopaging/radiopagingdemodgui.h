///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#ifndef INCLUDE_RADIOPAGINGDEMODGUI_H
#define INCLUDE_RADIOPAGINGDEMODGUI_H

#include <QElapsedTimer>
#include <QRegularExpressionMatch>
#include <QSet>
#ifdef QT_TEXTTOSPEECH_FOUND
#include <QTextToSpeech>
#endif

#include "channel/channelgui.h"
#include "dsp/channelmarker.h"
#include "util/messagequeue.h"
#include "util/messagerecord.h"
#include "settings/rollupstate.h"

#include "radiopagingdemodsettings.h"
#include "radiopagingdemodsink.h"

class PluginAPI;
class DeviceUISet;
class BasebandSampleSink;
class RadioPagingDemod;
class QLabel;
class QCheckBox;
class ScopeVis;

namespace Ui {
    class RadioPagingDemodGUI;
}

// Lean GUI: protocol and speed checkboxes, decoder status and the latest messages. The full
// history, filters, rules and re-interpretation are in the message center
class RadioPagingDemodGUI : public ChannelGUI {
    Q_OBJECT

public:
    static RadioPagingDemodGUI* create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel);
    virtual void destroy();

    void resetToDefaults();
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    virtual MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; }
    virtual void setWorkspaceIndex(int index) { m_settings.m_workspaceIndex = index; }
    virtual int getWorkspaceIndex() const { return m_settings.m_workspaceIndex; }
    virtual void setGeometryBytes(const QByteArray& blob) { m_settings.m_geometryBytes = blob; }
    virtual QByteArray getGeometryBytes() const { return m_settings.m_geometryBytes; }
    virtual QString getTitle() const { return m_settings.m_title; }
    virtual QColor getTitleColor() const { return m_settings.m_rgbColor; }
    virtual void zetHidden(bool hidden) { m_settings.m_hidden = hidden; }
    virtual bool getHidden() const { return m_settings.m_hidden; }
    virtual ChannelMarker& getChannelMarker() { return m_channelMarker; }
    virtual int getStreamIndex() const { return m_settings.m_streamIndex; }
    virtual void setStreamIndex(int streamIndex) { m_settings.m_streamIndex = streamIndex; }

public slots:
    void channelMarkerChangedByCursor();
    void channelMarkerHighlightedByCursor();

private:
    static const int m_maxRows = 500;

    Ui::RadioPagingDemodGUI* ui;
    PluginAPI* m_pluginAPI;
    DeviceUISet* m_deviceUISet;
    ChannelMarker m_channelMarker;
    RollupState m_rollupState;
    RadioPagingDemodSettings m_settings;
    qint64 m_deviceCenterFrequency;
    int m_basebandSampleRate;
    bool m_doApplySettings;
    RadioPagingDemod* m_radioPagingDemod;
    uint32_t m_tickCount;
    MessageQueue m_inputMessageQueue;
    ScopeVis *m_scopeVis;
    bool m_loadingData = false;         //!< A log is being read: its messages are shown, not notified again
    QSet<QString> m_mapItems;           //!< Names of the items sent to the Map
#ifdef QT_TEXTTOSPEECH_FOUND
    QTextToSpeech *m_speech = nullptr;
#endif

    explicit RadioPagingDemodGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget* parent = nullptr);
    virtual ~RadioPagingDemodGUI();

    void blockApplySettings(bool block);
    void applySettings(const QStringList& settingsKeys, bool force = false);
    void displaySettings();
    bool handleMessage(const Message& message);
    void makeUIConnections();
    void updateAbsoluteCenterFrequency();
    //!< The message is shown and given to the notifications (not when loading a log)
    void showRecord(const MessageRecord& record);
    void checkNotifications(const MessageRecord& record);
    //!< The field of the record that a notification matches
    //!< The texts of the field of the record a notification is tried on: the address and the members of a group message
    static QStringList matchTexts(const MessageRecord& record, int field);
    //!< ${address}, ${message}, ${protocol}, ${type}, ${channel} and ${0}, ${1}... of the match, replaced in the string
    static QString subStrings(const MessageRecord& record, const QRegularExpressionMatch& match, const QString& string);
    void enableSpeechIfNeeded();
    void speechNotification(const QString& speech);
    void commandNotification(const QString& command);
    void sendToMap(const MessageRecord& record, float latitude, float longitude);
    void clearFromMap();
    //!< Hides the rows whose address does not match the filter; a row shown is hidden or not at once
    void applyAddressFilter();
    bool addressMatchesFilter(const QStringList& addresses) const;
    void leaveEvent(QEvent*);
    void enterEvent(EnterEventType*);
    void updateStatus();
    //!< The rows of each protocol: the live line, the options and the network lines of each polarity
    void layoutProtocols();
    //!< The polarity of the network being received by a protocol (in sync, else received last)
    int currentPolarity(const RadioPagingDemodSink::Status& status, int protocol);
    //!< The live line of a protocol: state, what is received, polarity, quality, carrier offset, messages
    static QString statusLine(const RadioPagingDemodSink::Status& status, int protocol, int pol, bool sync,
        const QString& state, const QString& detail);
    //!< FLEX: the lines of the network of the last frame of a polarity
    static QString flexNetworkLines(const RadioPagingDemodSink::Status& status, int pol);
    //!< What the receivers of a polarity remember and when they were reset
    static QString memoryLine(const RadioPagingDemodSink::Status& status, int protocol, int pol);
    //!< The polarities shown: those received since the reset, the normal one when none was
    static void shownPolarities(const RadioPagingDemodSink::Status& status, int protocol, bool shown[2]);
    //!< The group slot bars of FLEX, one for each polarity shown, with the state of each slot
    void updateGroupBars(const RadioPagingDemodSink::Status& status, const bool shown[2]);
    static QString groupSlotToolTip(const RadioPagingFlexDecoder::GroupSlot& slot, int pol, const RadioPagingDemodSink::Status& status);
    static QString polarityHtml(bool inverted);
    static QStringList flexDetails(const RadioPagingDemodSink::Status& status);
    static QString flexToolTip(const RadioPagingDemodSink::Status& status);
    static QString qualityHtml(const RadioPagingBerWindow& ber, bool live);
    static QString offsetHtml(float offset, bool live);

    // FLEX: between the frames of a transmission the decoder hunts for the next sync 1. The
    // display keeps SYNC until that sync is due: a frame (1.875 s, STD-43A 3.2) after the start
    // of the last one, plus one status period (the display sees the start that much late)
    static const qint64 m_flexFrameMs = 1875;
    static const qint64 m_statusPeriodMs = 200;
    QString m_pocsagToolTip;            //!< Descriptions of the status fields (from the form)
    QString m_gscToolTip;
    QElapsedTimer m_statusClock;
    qint64 m_flexFrameStart = -1;       //!< m_statusClock when the last frame was first seen, -1 none
    bool m_flexWasSync = false;
    int m_currentPol[3] = {0, 0, 0};    //!< The polarity of the live line of each protocol
    QLabel *m_networkLabel[3][2];       //!< The network lines of each protocol and polarity
    QLabel *m_codepageLabel[3];
    //!< The editor of the custom codepage of a protocol (0 POCSAG, 1 FLEX, 2 GSC): a character for each code of the
    //!< default table, the direction, export and import
    void editCustomCodepage(int protocol);
    //!< The codepage lists with the Custom entry of each protocol
    void fillCodepageLists();
    QWidget *m_flexGroupBar[2] = {nullptr, nullptr};    //!< The 16 group slots of each polarity
    QLabel *m_flexGroupCell[2][16];

private slots:
    void on_deltaFrequency_changed(qint64 value);
    void on_rfBW_valueChanged(int value);
    void on_pocsagDeviation_valueChanged(int value);
    void on_flexDeviation_valueChanged(int value);
    void on_flexMaxMessageNumber_valueChanged(int value);
    void on_pocsagEnabled_clicked(bool checked);
    void on_flexEnabled_clicked(bool checked);
    void on_gscEnabled_clicked(bool checked);
    void on_gscVoiceTimeout_valueChanged(int value);
    void on_gscDeviation_valueChanged(int value);
    void on_gscAudio_toggled(bool checked);
    void on_gscVoiceRecord_toggled(bool checked);
    void on_gscVoiceFiles_clicked();
    void on_channelAlias_editingFinished();
    void voiceDirectorySelect(const QPoint& p);
    void displayVoiceRecordToolTip();
    void on_volume_valueChanged(int value);
    void audioSelect(const QPoint& p);
    void on_clearTable_clicked();
    void on_udpEnabled_clicked(bool checked);
    void on_udpAddress_editingFinished();
    void on_udpPort_valueChanged(int value);
    void on_udpFormat_currentIndexChanged(int index);
    void on_notifications_clicked();
    void importFromPager();
    void on_logEnable_clicked(bool checked);
    void on_logFilename_clicked();
    void on_logOpen_clicked();
    void on_filterAddress_editingFinished();
    void on_channel1_currentIndexChanged(int index);
    void on_channel2_currentIndexChanged(int index);
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void onMenuDialogCalled(const QPoint& p);
    void handleInputMessages();
    void tick();
};

#endif // INCLUDE_RADIOPAGINGDEMODGUI_H
