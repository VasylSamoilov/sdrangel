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

#ifndef INCLUDE_FEATURE_MESSAGECENTERGUI_H_
#define INCLUDE_FEATURE_MESSAGECENTERGUI_H_

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QWidget>

#include "feature/featuregui.h"
#include "util/messagequeue.h"
#include "settings/rollupstate.h"

#include "messagecenter.h"
#include "messagecentersettings.h"

class PluginAPI;
class FeatureUISet;

// The fields of a filter of the message center: the view of the log and the routes use it
class MessageCenterFilterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MessageCenterFilterWidget(QWidget *parent = nullptr);
    MessageCenterFilter filter() const;
    void setFilter(const MessageCenterFilter& filter);

signals:
    void changed();

private:
    QComboBox *m_protocol;
    QComboBox *m_direction;
    QComboBox *m_polarity;
    QComboBox *m_type;
    QLineEdit *m_alias;
    QLineEdit *m_address;
    QLineEdit *m_text;
    QCheckBox *m_hideDuplicates;
    QCheckBox *m_voiceOnly;
};

class MessageCenterGUI : public FeatureGUI
{
    Q_OBJECT
public:
    static MessageCenterGUI* create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature);
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
    MessageCenter *m_center;
    MessageCenterSettings m_settings;
    RollupState m_rollupState;
    MessageQueue m_inputMessageQueue;
    bool m_doApplySettings;
    int m_shownLogged;                  //!< Records of the log when the table was made
    static const int m_maxLogRows = 1000;

    // The log
    MessageCenterFilterWidget *m_logFilter;
    QTableWidget *m_logTable;
    QPushButton *m_playButton;
    // The endpoints and the queue
    QTableWidget *m_endpointsTable;
    QTableWidget *m_queueTable;
    // The page to compose
    QComboBox *m_transmitter;
    QComboBox *m_protocol;
    QLineEdit *m_address;
    QComboBox *m_function;
    QComboBox *m_type;
    QLineEdit *m_text;
    QLineEdit *m_voiceFile;
    QComboBox *m_speed;
    QComboBox *m_polarity;
    // FLEX binary: B and the flags M, D, H, I of header word 2; FLEX secure: the type t1 t0
    QSpinBox *m_blocking;
    QCheckBox *m_mailDrop;
    QCheckBox *m_rightToLeft;
    QCheckBox *m_headerMessage;
    QCheckBox *m_statusInfo;
    QComboBox *m_secureType;
    // The routes and the files
    QTableWidget *m_routesTable;
    QTableWidget *m_sinksTable;
    QTableWidget *m_sourcesTable;
    QPlainTextEdit *m_errors;

    explicit MessageCenterGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent = nullptr);
    ~MessageCenterGUI() override;

    QWidget *makeRollup(const QString& title);
    void buildLog(QWidget *container);
    void buildEndpoints(QWidget *container);
    void buildQueue(QWidget *container);
    void buildCompose(QWidget *container);
    void buildRoutes(QWidget *container);
    void buildFiles(QWidget *container);

    void applySettings(const QStringList& settingsKeys, bool force = false);
    void displaySettings();
    bool handleMessage(const Message& message);
    void refresh();
    void refreshLog();
    void refreshEndpoints();
    void refreshQueue();
    void refreshComposeTransmitters();
    void refreshRoutes();
    void refreshFiles();
    QString selectedId(QTableWidget *table) const;
    MessageRecord composedPage() const;
    void editRoute(int index);
    void editFile(bool sink);

private slots:
    void handleInputMessages();
    void onMenuDialogCalled(const QPoint &p);
    void onWidgetRolled(QWidget* widget, bool rollDown);
    void playSelected();
    void post();
    void pageAction(int action);
    void addRoute();
    void removeRoute();
    void editSelectedRoute();
    void addSink();
    void removeSink();
    void addSource();
    void removeSource();
    void transmitterChanged();
    void typeChanged();
    void browseVoice();
};

#endif // INCLUDE_FEATURE_MESSAGECENTERGUI_H_
