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

#ifndef INCLUDE_FEATURE_CELLULARGUI_H_
#define INCLUDE_FEATURE_CELLULARGUI_H_

#include "feature/featuregui.h"
#include "settings/rollupstate.h"
#include "util/messagequeue.h"

#include "cellularsettings.h"

class PluginAPI;
class FeatureUISet;
class Feature;
class Cellular;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

// One network: its plan and carriers, the radios it runs on, the state of its carriers and the
// event log. Settings are taken when the network starts.
class CellularGUI : public FeatureGUI
{
    Q_OBJECT
public:
    static CellularGUI* create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature);
    void destroy() override;

    void resetToDefaults() override;
    QByteArray serialize() const override;
    bool deserialize(const QByteArray& data) override;
    MessageQueue *getInputMessageQueue() override { return &m_inputMessageQueue; }
    void setWorkspaceIndex(int index) override;
    int getWorkspaceIndex() const override { return m_settings.m_workspaceIndex; }
    void setGeometryBytes(const QByteArray& blob) override { m_settings.m_geometryBytes = blob; }
    QByteArray getGeometryBytes() const override { return m_settings.m_geometryBytes; }

private:
    explicit CellularGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent = nullptr);
    ~CellularGUI() override;

    void buildUI();
    void fillCountries();
    void displaySettings();
    void applySettings();
    bool handleMessage(const Message& message);

    Cellular *m_cellular;
    CellularSettings m_settings;
    RollupState m_rollupState;
    MessageQueue m_inputMessageQueue;
    bool m_doApplySettings;
    unsigned int m_tickCount;

    QComboBox *m_system;
    QComboBox *m_country;
    QSpinBox *m_area;
    QSpinBox *m_areaNo;
    QSpinBox *m_msPower;
    QLineEdit *m_carriers;
    QCheckBox *m_compandor;
    QCheckBox *m_callerId;
    QCheckBox *m_clock;
    QLineEdit *m_smsc;
    QComboBox *m_fallback;
    QSpinBox *m_rxDeviceSet;
    QSpinBox *m_txDeviceSet;
    QSpinBox *m_lead;
    QPushButton *m_startStop;
    QLabel *m_summary;
    QTableWidget *m_table;
    QPlainTextEdit *m_log;

private slots:
    void handleInputMessages();
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void onSettingChanged();
    void onSystemChanged();
    void onStartStop(bool checked);
    void tick();
};

#endif // INCLUDE_FEATURE_CELLULARGUI_H_
