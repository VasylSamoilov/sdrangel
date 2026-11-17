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

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "feature/featureuiset.h"
#include "gui/rollupcontents.h"
#include "maincore.h"

#include "nmtplan.h"

#include "cellular.h"
#include "cellulargui.h"

namespace
{

enum Column
{
    ColChannel,
    ColType,
    ColDownlink,
    ColUplink,
    ColState,
    ColRxLevel,
    ColTxFill,
    ColUnderruns,
    ColCount
};

} // namespace

CellularGUI* CellularGUI::create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature)
{
    return new CellularGUI(pluginAPI, featureUISet, feature);
}

void CellularGUI::destroy()
{
    delete this;
}

CellularGUI::CellularGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent) :
    FeatureGUI(parent),
    m_cellular(reinterpret_cast<Cellular*>(feature)),
    m_doApplySettings(true),
    m_tickCount(0)
{
    (void) pluginAPI;
    (void) featureUISet;

    m_feature = feature;
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/feature/cellular/readme.md";

    buildUI();

    RollupContents *rollupContents = getRollupContents();
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));

    m_cellular->setMessageQueueToGUI(&m_inputMessageQueue);
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick())); // 50 ms

    m_settings.setRollupState(&m_rollupState);
    displaySettings();
    applySettings();
    sizeToContents();
    m_resizer.enableChildMouseTracking();
}

CellularGUI::~CellularGUI()
{
    disconnect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));
    m_cellular->setMessageQueueToGUI(nullptr);
}

void CellularGUI::buildUI()
{
    RollupContents *rollupContents = getRollupContents();

    // the network
    QWidget *network = new QWidget(rollupContents);
    network->setObjectName("network");
    network->setWindowTitle("Network");
    QFormLayout *form = new QFormLayout(network);
    form->setContentsMargins(3, 3, 3, 3);

    m_system = new QComboBox(network);
    m_system->addItems({"NMT-450", "NMT-900"});
    form->addRow("System", m_system);
    m_country = new QComboBox(network);
    m_country->setToolTip("The frequency plan of the country (the channel numbers, the country digit of the traffic area)");
    form->addRow("Country", m_country);
    m_area = new QSpinBox(network);
    m_area->setRange(1, 15);
    m_area->setToolTip("Traffic area within the country (the second digit Y2): phones of a network with another area see a new network and roam");
    form->addRow("Traffic area", m_area);
    m_areaNo = new QSpinBox(network);
    m_areaNo->setRange(0, 4);
    m_areaNo->setSpecialValueText("none");
    m_areaNo->setToolTip("Area number 1..4 (the phones stay in their area), none: every phone is taken");
    form->addRow("Area number", m_areaNo);
    m_msPower = new QSpinBox(network);
    m_msPower->setRange(0, 3);
    m_msPower->setToolTip("Power level the phones send with: 3 is the highest, 0 the lowest");
    form->addRow("Phone power", m_msPower);
    m_carriers = new QLineEdit(network);
    m_carriers->setToolTip("channel[:type[:supervisory]], comma separated. Types: cc (calling), tc (traffic), cctc (both), "
        "actc, cca, ccb (NMT-900), test. Supervisory tone 1..4. Example: 1:cctc, 5, 6:tc:2");
    form->addRow("Carriers", m_carriers);
    m_compandor = new QCheckBox("Compandor on speech", network);
    form->addRow("", m_compandor);
    m_callerId = new QCheckBox("Send the number of the caller", network);
    form->addRow("", m_callerId);
    m_clock = new QCheckBox("Send the time of day", network);
    form->addRow("", m_clock);
    m_smsc = new QLineEdit(network);
    m_smsc->setToolTip("The number the phones send short messages to; empty: no short messages from the phones");
    form->addRow("SMS centre", m_smsc);
    m_fallback = new QComboBox(network);
    m_fallback->addItems({"Echo", "Music"});
    m_fallback->setToolTip("What answers a call to a number that is not registered");
    form->addRow("Other numbers", m_fallback);
    m_rxDeviceSet = new QSpinBox(network);
    m_rxDeviceSet->setRange(0, 99);
    m_rxDeviceSet->setToolTip("The device set (R0, R1 ...) with the Cellular Rx channel");
    form->addRow("Receiver device set", m_rxDeviceSet);
    m_txDeviceSet = new QSpinBox(network);
    m_txDeviceSet->setRange(0, 99);
    m_txDeviceSet->setToolTip("The device set (T1, T2 ...) with the Cellular Tx channel");
    form->addRow("Transmitter device set", m_txDeviceSet);
    m_lead = new QSpinBox(network);
    m_lead->setRange(20, 1000);
    m_lead->setSuffix(" ms");
    m_lead->setToolTip("How far the transmitter runs ahead of the receiver. It is part of the round trip "
        "(keep the round trip below 300 ms); too little and the transmitter runs empty");
    form->addRow("Transmit lead", m_lead);
    m_startStop = new QPushButton("Start", network);
    m_startStop->setCheckable(true);
    form->addRow("", m_startStop);

    // the carriers
    QWidget *status = new QWidget(rollupContents);
    status->setObjectName("status");
    status->setWindowTitle("Carriers");
    QVBoxLayout *statusLayout = new QVBoxLayout(status);
    statusLayout->setContentsMargins(3, 3, 3, 3);
    m_summary = new QLabel("Not running", status);
    m_summary->setWordWrap(true);
    statusLayout->addWidget(m_summary);
    m_table = new QTableWidget(0, ColCount, status);
    m_table->setHorizontalHeaderLabels({"Ch", "Type", "Down MHz", "Up MHz", "State", "Rx dB", "Tx ms", "Empty"});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(120);
    statusLayout->addWidget(m_table);

    // the events
    QWidget *events = new QWidget(rollupContents);
    events->setObjectName("events");
    events->setWindowTitle("Events");
    QVBoxLayout *eventsLayout = new QVBoxLayout(events);
    eventsLayout->setContentsMargins(3, 3, 3, 3);
    m_log = new QPlainTextEdit(events);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setMinimumHeight(120);
    eventsLayout->addWidget(m_log);

    network->setMinimumWidth(420);

    connect(m_system, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularGUI::onSystemChanged);
    connect(m_country, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularGUI::onSettingChanged);
    connect(m_area, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularGUI::onSettingChanged);
    connect(m_areaNo, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularGUI::onSettingChanged);
    connect(m_msPower, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularGUI::onSettingChanged);
    connect(m_carriers, &QLineEdit::editingFinished, this, &CellularGUI::onSettingChanged);
    connect(m_compandor, &QCheckBox::toggled, this, &CellularGUI::onSettingChanged);
    connect(m_callerId, &QCheckBox::toggled, this, &CellularGUI::onSettingChanged);
    connect(m_clock, &QCheckBox::toggled, this, &CellularGUI::onSettingChanged);
    connect(m_smsc, &QLineEdit::editingFinished, this, &CellularGUI::onSettingChanged);
    connect(m_fallback, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularGUI::onSettingChanged);
    connect(m_rxDeviceSet, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularGUI::onSettingChanged);
    connect(m_txDeviceSet, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularGUI::onSettingChanged);
    connect(m_lead, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularGUI::onSettingChanged);
    connect(m_startStop, &QPushButton::toggled, this, &CellularGUI::onStartStop);
}

// the countries of the system, the short name as the data
void CellularGUI::fillCountries()
{
    const modemnmt::System system = m_system->currentIndex() == 1 ? modemnmt::System::Nmt900 : modemnmt::System::Nmt450;
    QStringList seen;

    m_country->blockSignals(true);
    m_country->clear();
    for (const modemnmt::NmtCountry& c : modemnmt::nmtCountries())
    {
        const QString shortName = QString::fromLatin1(c.shortName);

        if (c.system != system || seen.contains(shortName)) {
            continue;
        }
        seen.append(shortName);
        QString label = QString("%1 - %2").arg(shortName).arg(QString::fromLatin1(c.longName));
        if (!c.tested) {
            label += " (untested)";
        }
        m_country->addItem(label, shortName);
    }
    const int at = m_country->findData(m_settings.m_country);
    m_country->setCurrentIndex(at >= 0 ? at : 0);
    m_country->blockSignals(false);
}

void CellularGUI::displaySettings()
{
    m_doApplySettings = false;
    setTitleColor(QColor::fromRgb(m_settings.m_rgbColor));
    setWindowTitle(m_settings.m_title);
    setTitle(m_settings.m_title);

    m_system->setCurrentIndex(m_settings.m_system == 1 ? 1 : 0);
    fillCountries();
    m_area->setValue(m_settings.m_area);
    m_areaNo->setValue(m_settings.m_areaNo);
    m_msPower->setValue(m_settings.m_msPower);
    m_carriers->setText(m_settings.m_carriers);
    m_compandor->setChecked(m_settings.m_compandor);
    m_callerId->setChecked(m_settings.m_sendCallerId);
    m_clock->setChecked(m_settings.m_sendClock);
    m_smsc->setText(m_settings.m_smscNumber);
    m_fallback->setCurrentIndex(m_settings.m_fallback == CellularSettings::FallbackMusic ? 1 : 0);
    m_rxDeviceSet->setValue(m_settings.m_rxDeviceSetIndex);
    m_txDeviceSet->setValue(m_settings.m_txDeviceSetIndex);
    m_lead->setValue(m_settings.m_leadMs);

    getRollupContents()->restoreState(m_rollupState);
    m_doApplySettings = true;
}

void CellularGUI::applySettings()
{
    if (m_doApplySettings) {
        m_cellular->getInputMessageQueue()->push(Cellular::MsgConfigureCellular::create(m_settings));
    }
}

void CellularGUI::onSystemChanged()
{
    m_settings.m_system = m_system->currentIndex();
    fillCountries();
    onSettingChanged();
}

void CellularGUI::onSettingChanged()
{
    if (!m_doApplySettings) {
        return;
    }
    m_settings.m_system = m_system->currentIndex();
    m_settings.m_country = m_country->currentData().toString();
    m_settings.m_area = m_area->value();
    m_settings.m_areaNo = m_areaNo->value();
    m_settings.m_msPower = m_msPower->value();
    m_settings.m_carriers = m_carriers->text();
    m_settings.m_compandor = m_compandor->isChecked();
    m_settings.m_sendCallerId = m_callerId->isChecked();
    m_settings.m_sendClock = m_clock->isChecked();
    m_settings.m_smscNumber = m_smsc->text();
    m_settings.m_fallback = m_fallback->currentIndex() == 1 ? CellularSettings::FallbackMusic : CellularSettings::FallbackEcho;
    m_settings.m_rxDeviceSetIndex = m_rxDeviceSet->value();
    m_settings.m_txDeviceSetIndex = m_txDeviceSet->value();
    m_settings.m_leadMs = m_lead->value();
    applySettings();
}

void CellularGUI::onStartStop(bool checked)
{
    if (!m_doApplySettings) {
        return;
    }
    m_startStop->setText(checked ? "Stop" : "Start");
    m_cellular->getInputMessageQueue()->push(Cellular::MsgStartStop::create(checked));
}

void CellularGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings();
}

QByteArray CellularGUI::serialize() const
{
    return m_settings.serialize();
}

bool CellularGUI::deserialize(const QByteArray& data)
{
    const bool ok = m_settings.deserialize(data);

    m_feature->setWorkspaceIndex(m_settings.m_workspaceIndex);
    displaySettings();
    applySettings();
    return ok;
}

void CellularGUI::setWorkspaceIndex(int index)
{
    m_settings.m_workspaceIndex = index;
    m_feature->setWorkspaceIndex(index);
}

void CellularGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;
    getRollupContents()->saveState(m_rollupState);
}

void CellularGUI::handleInputMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop()))
    {
        handleMessage(*message);
        delete message;
    }
}

bool CellularGUI::handleMessage(const Message& message)
{
    if (Cellular::MsgReportLog::match(message))
    {
        const QString& text = static_cast<const Cellular::MsgReportLog&>(message).getText();
        m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss ") + text);
        return true;
    }
    else if (Cellular::MsgReportStatus::match(message))
    {
        const Cellular::MsgReportStatus& report = static_cast<const Cellular::MsgReportStatus&>(message);

        m_summary->setText(report.m_summary);
        m_table->setRowCount(report.m_carriers.size());
        for (int row = 0; row < report.m_carriers.size(); row++)
        {
            const Cellular::MsgReportStatus::Carrier& c = report.m_carriers[row];
            const bool on = !c.state.startsWith("off");
            const QStringList cells = {
                QString::number(c.channel),
                c.type,
                QString::number(c.downlink / 1e6, 'f', 4),
                QString::number(c.uplink / 1e6, 'f', 4),
                c.state,
                on ? QString::number(c.rxLevel, 'f', 1) : QString(),
                on ? QString::number(c.txFillMs, 'f', 0) : QString(),
                on ? QString::number(c.underruns) : QString()
            };
            for (int col = 0; col < ColCount; col++)
            {
                QTableWidgetItem *item = m_table->item(row, col);
                if (!item)
                {
                    item = new QTableWidgetItem();
                    m_table->setItem(row, col, item);
                }
                item->setText(cells[col]);
                item->setForeground(on ? QBrush() : QBrush(Qt::red));
            }
        }
        return true;
    }
    else if (Cellular::MsgConfigureCellular::match(message))
    {
        m_settings = static_cast<const Cellular::MsgConfigureCellular&>(message).getSettings();
        m_settings.setRollupState(&m_rollupState);
        displaySettings();
        return true;
    }
    return false;
}

void CellularGUI::tick()
{
    if (++m_tickCount % 10 != 0) {
        return;
    }
    // the feature may stop by itself (an error, a radio channel removed)
    const bool running = m_cellular->getState() == Feature::StRunning;
    if (m_startStop->isChecked() != running)
    {
        m_doApplySettings = false;
        m_startStop->setChecked(running);
        m_startStop->setText(running ? "Stop" : "Start");
        m_doApplySettings = true;
    }
}
