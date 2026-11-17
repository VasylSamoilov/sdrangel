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

#include <QWidget>

#include "feature/featuregui.h"
#include "settings/rollupstate.h"
#include "util/messagequeue.h"

#include "cellular.h"
#include "cellularsettings.h"

class PluginAPI;
class FeatureUISet;
class Feature;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

// One network in its tab: its plan, carriers and radios, the state of its carriers and its log.
class CellularNetworkTab : public QWidget
{
    Q_OBJECT
public:
    explicit CellularNetworkTab(QWidget *parent = nullptr);

    void display(const CellularNetworkSettings& settings);
    void read(CellularNetworkSettings& settings) const;
    int id() const { return m_id; }
    // the network runs with other settings than the shown ones
    void setStale(bool stale);
    void showStatus(const Cellular::MsgReportStatus& report);
    void appendLog(const QString& line);

signals:
    void changed();

private:
    void fillCountries(const QString& selected);

    int m_id;
    bool m_updating;
    QLineEdit *m_name;
    QCheckBox *m_enabled;
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
    QLabel *m_stale;
    QLabel *m_summary;
    QTableWidget *m_table;
    QPlainTextEdit *m_log;

private slots:
    void onSystemChanged();
    void onChanged();
};

// The networks of the controller: one tab each, the transmit lead and the start of the controller.
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
    void displaySettings();
    void applySettings();
    bool handleMessage(const Message& message);
    CellularNetworkTab *tabFor(int id) const;
    void addTab(const CellularNetworkSettings& network);

    Cellular *m_cellular;
    CellularSettings m_settings;
    RollupState m_rollupState;
    MessageQueue m_inputMessageQueue;
    bool m_doApplySettings;
    unsigned int m_tickCount;

    QSpinBox *m_lead;
    QPushButton *m_startStop;
    QPushButton *m_add;
    QPushButton *m_remove;
    QTabWidget *m_tabs;
    QPlainTextEdit *m_log;                  // the controller

private slots:
    void handleInputMessages();
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void onNetworkChanged();
    void onLeadChanged();
    void onStartStop(bool checked);
    void onAdd();
    void onRemove();
    void tick();
};

#endif // INCLUDE_FEATURE_CELLULARGUI_H_
