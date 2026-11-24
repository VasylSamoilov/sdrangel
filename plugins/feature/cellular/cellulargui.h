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

#include <QDialog>
#include <QDateTime>
#include <QMap>
#include <QWidget>

#include <deque>

#include "feature/featuregui.h"
#include "settings/rollupstate.h"
#include "util/messagequeue.h"

#include "cellular.h"
#include "cellularbandstrip.h"
#include "cellularsettings.h"
#include "cellularspeech.h"
#include "nmtphonebook.h"

class PluginAPI;
class FeatureUISet;
class Feature;
class ButtonSwitch;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QToolButton;

class CellularChannelGrid;

// The settings of one network, edited as a copy: they are taken once, with OK. `others`: the
// channels of the other networks on the same devices, with their names.
class CellularNetworkDialog : public QDialog
{
    Q_OBJECT
public:
    CellularNetworkDialog(const CellularNetworkSettings& settings, const QMap<int, QString>& others, QWidget *parent = nullptr);

    void read(CellularNetworkSettings& settings) const;
    // "10:cc, 18:tc:1"
    static QString carriersText(const std::vector<modemnmt::NmtCarrierConfig>& carriers);
    // span of a device set in Hz (where carriers go), false if it is not there or not of that direction
    static bool deviceSpan(int index, bool tx, double& low, double& high, double& centre, int& rate);
    // the device sets with a Cellular Rx (tx: Cellular Tx) channel, and the one chosen even if not
    static void fillDeviceSets(QComboBox *combo, bool tx, int chosen);

private:
    void fillCountries(const QString& selected);
    void onPlanChanged();
    void onGridChanged();
    void onTextChanged();
    void showBandwidth();

    CellularChannelGrid *m_grid;
    QLabel *m_bandwidth;
    QLabel *m_carriersError;

    CellularNetworkSettings m_settings;
    QLineEdit *m_name;
    QCheckBox *m_enabled;
    QComboBox *m_system;
    QComboBox *m_country;
    QSpinBox *m_y1;
    QSpinBox *m_area;
    QSpinBox *m_areaNo;
    QComboBox *m_msPower;
    QLineEdit *m_carriers;
    QCheckBox *m_compandor;
    QCheckBox *m_callerId;
    QCheckBox *m_clock;
    QLineEdit *m_smsc;
    QComboBox *m_smsCodepage;               // "vendor/model/codepage" of protocol nmt in the data
    QComboBox *m_fallback;
    QWidget *m_echoBox;
    QDoubleSpinBox *m_echoDelay;
    QSpinBox *m_echoLevel;
    QCheckBox *m_echoAnnounce;
    QWidget *m_musicBox;
    QComboBox *m_musicSource;
    QLineEdit *m_musicFile;
    QComboBox *m_musicInput;                // item data: the device name, empty for the default
    QSpinBox *m_musicLevel;
    QComboBox *m_musicLoop;
    QSpinBox *m_callLimit;
    QComboBox *m_rxDeviceSet;               // item data: the index of the device set
    QComboBox *m_txDeviceSet;
};

// The networks of the tester and what it sees: phones, a test bench for one phone, the log, the
// networks and their carriers, one tab each.
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

    struct LogLine
    {
        QString time;
        int network;            // 0: the controller
        QString text;
        int kind;               // Cellular::MsgReportLog::Kind
    };

    void buildUI();
    void fitHeight();
    void resizeEvent(QResizeEvent *event) override;
    QWidget *buildPhones();
    QWidget *buildTest();
    QWidget *buildLog();
    QWidget *buildNetworks();
    QWidget *buildCarriers();
    void displaySettings();
    void applySettings();
    bool handleMessage(const Message& message);
    void showStatus(const Cellular::MsgReportStatus& report);
    // the Mobiles table: the book and the phones the networks hear, one row per phone
    void showMobiles();
    int mobileRow(modemnmt::System system, const QString& number) const;
    void editMobile(modemnmt::System system, const QString& number);
    void addMobile();
    void forgetMobile(modemnmt::System system, const QString& number);
    // what the phone does now on the networks, and the network that heard it last
    QString liveText(const QString& number, int& network, QColor& colour, QString& line) const;
    modemnmt::System systemOf(int network) const;
    void showCarriers();
    void showSummary();
    void showTestPhone();
    void showSpeech();
    const CellularSpeechSeen *speechOf(const QString& number) const;
    void fillNetworkCombos();
    void addLog(int network, const QString& text, int kind);
    QString logHtml(const LogLine& line, bool withNetwork) const;
    bool logShown(const LogLine& line) const;
    void renderLog();
    QString networkName(int id) const;
    int selectedNetwork() const;
    void editNetwork(int id);
    void requestAction(int network, const QString& number, int action, const QString& text, int value);
    void phoneMenu(const QPoint& pos);
    void openSettings();

    Cellular *m_cellular;
    CellularSettings m_settings;
    RollupState m_rollupState;
    MessageQueue m_inputMessageQueue;
    bool m_doApplySettings;
    unsigned int m_tickCount;

    ButtonSwitch *m_startStop;
    QLabel *m_summary;
    QTabWidget *m_tabs;
    // Mobiles
    QTableWidget *m_phones;
    modemnmt::NmtPhoneBook m_book;              // a copy of the feature's, for presets and the table
    QMap<QString, CellularSpeechSeen> m_speech; // the call meters, by number: the last report of its call
    // the Call section: the followed call drawn large, its phone's radio line and log
    CellularCallPanel *m_callPanel;
    QPlainTextEdit *m_callLog;
    ButtonSwitch *m_callFollow;             // off: the section stops where it is
    QString m_callNumber;                   // the phone of the followed call
    int m_callChannel = 0;
    int m_callNetwork = 0;
    bool callLine(int network, const QString& text) const;
    QString radioOf(const QString& number) const;     // its Mobiles state, two lines
    // Console
    QWidget *m_consolePage;
    QLabel *m_consoleRadio;
    void showConsoleRadio();
    QWidget *buildConsole();
    void showConsole();
    void consoleKey(QChar key);
    QString consoleTarget() const;
    int networkOfPhone(const QString& number) const;
    struct ConsoleView                      // the console's last report
    {
        int m_state = 0;                    // Cellular::MsgReportConsole::State
        int m_network = 0;
        QString m_peer;
        QDateTime m_since;
        QString m_keys;
    };
    ConsoleView m_consoleReport;
    QLabel *m_consoleBell;
    QLabel *m_consoleState;
    QLabel *m_consoleKeys;
    QLineEdit *m_consoleNumber;
    QToolButton *m_consoleCall;             // call, or answer the call that rings
    QToolButton *m_consoleHangUp;
    QToolButton *m_consoleFlash;
    QToolButton *m_consoleSpeakerMute;      // right click: the output device
    ButtonSwitch *m_consoleMicOn;           // right click: the input device
    void consoleDevice(const QPoint& p, bool input);
    QLineEdit *m_consoleSms;
    QWidget *m_consoleInCall;
    QComboBox *m_consoleChannel;
    QComboBox *m_consolePower;
    QLineEdit *m_consoleOwn;
    QSpinBox *m_consoleVolume;
    QSpinBox *m_consoleGain;
    ButtonSwitch *m_consoleAuto;
    CellularSpeechLine *m_speechLine;           // under the tabs: the followed call
    // Test
    QComboBox *m_testNetwork;
    QComboBox *m_testPhone;
    QLabel *m_testState;
    QLineEdit *m_smsText;
    QComboBox *m_powerLevel;
    QComboBox *m_moveChannel;               // "any free" (0) and the network's voice channels
    QPushButton *m_testRelease;
    QPushButton *m_testPower;
    QPushButton *m_testMove;
    // the voice channels of a network in a list: "any free" (0), then each by its number
    void voiceChannels(QComboBox *box, int network) const;
    // Log
    QComboBox *m_logNetwork;
    QCheckBox *m_logFrames;
    QPlainTextEdit *m_log;
    ButtonSwitch *m_logEnable;
    QToolButton *m_logFilename;
    std::deque<LogLine> m_logLines;
    // Networks
    QTableWidget *m_networks;
    // Carriers
    CellularBandStrip *m_strip;
    QTableWidget *m_carriers;

    // the last report of each network
    struct NetworkStatus
    {
        bool onAir = false;
        int registered = 0;
        int calls = 0;
        QString summary;
        QList<Cellular::MsgReportStatus::Carrier> carriers;
        QList<Cellular::MsgReportStatus::Phone> phones;
    };
    QMap<int, NetworkStatus> m_status;

    static const int m_maxLogLines = 5000;

private slots:
    void handleInputMessages();
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void onStartStop(bool checked);
    void tick();
};

#endif // INCLUDE_FEATURE_CELLULARGUI_H_
