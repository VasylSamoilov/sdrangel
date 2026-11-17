///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov                                             //
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

#ifndef INCLUDE_CELLULARRXGUI_H
#define INCLUDE_CELLULARRXGUI_H

#include <vector>

#include "channel/channelgui.h"
#include "dsp/channelmarker.h"
#include "settings/rollupstate.h"
#include "util/messagequeue.h"

#include "cellularrxsettings.h"

class PluginAPI;
class DeviceUISet;
class BasebandSampleSink;
class CellularRx;
class QGridLayout;
class QLabel;

// The span of the stream and one small box per carrier: which network and channel, where it is
// in the stream, the level it receives.
class CellularRxGUI : public ChannelGUI
{
    Q_OBJECT

public:
    static CellularRxGUI* create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel);
    void destroy();

    void resetToDefaults() override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;
    MessageQueue *getInputMessageQueue() override { return &m_inputMessageQueue; }
    void setWorkspaceIndex(int index) override { m_settings.m_workspaceIndex = index; }
    int getWorkspaceIndex() const override { return m_settings.m_workspaceIndex; }
    void setGeometryBytes(const QByteArray& blob) override { m_settings.m_geometryBytes = blob; }
    QByteArray getGeometryBytes() const override { return m_settings.m_geometryBytes; }
    QString getTitle() const override { return m_settings.m_title; }
    QColor getTitleColor() const override { return m_settings.m_rgbColor; }
    void zetHidden(bool hidden) override { m_settings.m_hidden = hidden; }
    bool getHidden() const override { return m_settings.m_hidden; }
    ChannelMarker& getChannelMarker() override { return m_channelMarker; }
    int getStreamIndex() const override { return m_settings.m_streamIndex; }
    void setStreamIndex(int streamIndex) override { m_settings.m_streamIndex = streamIndex; }

private:
    explicit CellularRxGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget* parent = nullptr);
    ~CellularRxGUI() override;

    void displaySettings();
    void rebuildBoxes(const std::vector<int>& ids);

    PluginAPI* m_pluginAPI;
    DeviceUISet* m_deviceUISet;
    ChannelMarker m_channelMarker;
    RollupState m_rollupState;
    CellularRxSettings m_settings;
    CellularRx* m_cellularRx;
    MessageQueue m_inputMessageQueue;
    QLabel *m_span;
    QGridLayout *m_boxLayout;
    std::vector<QLabel*> m_boxes;
    std::vector<int> m_boxIds;
    unsigned int m_tickCount;

private slots:
    void handleInputMessages();
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void onMenuDialogCalled(const QPoint& p);
    void tick();
};

#endif // INCLUDE_CELLULARRXGUI_H
