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
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "feature/featureuiset.h"
#include "gui/rollupcontents.h"
#include "maincore.h"

#include "nmtplan.h"

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

QString timeStamp()
{
    return QDateTime::currentDateTime().toString("HH:mm:ss ");
}

} // namespace

// ---- one network ----

CellularNetworkTab::CellularNetworkTab(QWidget *parent) :
    QWidget(parent),
    m_id(0),
    m_updating(false)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    QFormLayout *form = new QFormLayout();
    layout->addLayout(form);

    m_name = new QLineEdit(this);
    form->addRow("Name", m_name);
    m_enabled = new QCheckBox("On the air while the controller runs", this);
    form->addRow("", m_enabled);
    m_system = new QComboBox(this);
    m_system->addItems({"NMT-450", "NMT-900"});
    form->addRow("System", m_system);
    m_country = new QComboBox(this);
    m_country->setToolTip("The frequency plan of the country (the channel numbers, the country digit of the traffic area)");
    form->addRow("Country", m_country);
    m_area = new QSpinBox(this);
    m_area->setRange(1, 15);
    m_area->setToolTip("Traffic area within the country (the second digit Y2): phones of a network with another area see a new network and roam");
    form->addRow("Traffic area", m_area);
    m_areaNo = new QSpinBox(this);
    m_areaNo->setRange(0, 4);
    m_areaNo->setSpecialValueText("none");
    m_areaNo->setToolTip("Area number 1..4 (the phones stay in their area), none: every phone is taken");
    form->addRow("Area number", m_areaNo);
    m_msPower = new QSpinBox(this);
    m_msPower->setRange(0, 3);
    m_msPower->setToolTip("Power level the phones send with: 3 is the highest, 0 the lowest");
    form->addRow("Phone power", m_msPower);
    m_carriers = new QLineEdit(this);
    m_carriers->setToolTip("channel:type[:supervisory], comma separated; every channel needs its type. Types: cc (calling), "
        "tc (traffic), cctc (both), actc, cca, ccb (NMT-900), test. Supervisory tone 1..4 (0 none) on every traffic channel, "
        "or on none for 1..4 in turn. Example: 1:cctc:1, 5:tc:2, 6:tc:3");
    form->addRow("Carriers", m_carriers);
    m_compandor = new QCheckBox("Compandor on speech", this);
    form->addRow("", m_compandor);
    m_callerId = new QCheckBox("Send the number of the caller", this);
    form->addRow("", m_callerId);
    m_clock = new QCheckBox("Send the time of day", this);
    form->addRow("", m_clock);
    m_smsc = new QLineEdit(this);
    m_smsc->setToolTip("The number the phones send short messages to; empty: no short messages from the phones");
    form->addRow("SMS centre", m_smsc);
    m_fallback = new QComboBox(this);
    m_fallback->addItems({"Echo", "Music"});
    m_fallback->setToolTip("What answers a call to a number that is not registered in this network");
    form->addRow("Other numbers", m_fallback);
    m_rxDeviceSet = new QSpinBox(this);
    m_rxDeviceSet->setRange(0, 99);
    m_rxDeviceSet->setToolTip("The device set (R0, R1 ...) with the Cellular Rx channel; networks may share it");
    form->addRow("Receiver device set", m_rxDeviceSet);
    m_txDeviceSet = new QSpinBox(this);
    m_txDeviceSet->setRange(0, 99);
    m_txDeviceSet->setToolTip("The device set (T1, T2 ...) with the Cellular Tx channel; networks may share it");
    form->addRow("Transmitter device set", m_txDeviceSet);

    m_summary = new QLabel("Not running", this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({"Ch", "Type", "Down MHz", "Up MHz", "State", "Rx dB", "Tx ms", "Empty"});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(110);
    layout->addWidget(m_table);
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setMinimumHeight(100);
    layout->addWidget(m_log);

    connect(m_system, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularNetworkTab::onSystemChanged);
    connect(m_name, &QLineEdit::editingFinished, this, &CellularNetworkTab::onChanged);
    connect(m_enabled, &QCheckBox::toggled, this, &CellularNetworkTab::onChanged);
    connect(m_country, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularNetworkTab::onChanged);
    connect(m_area, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularNetworkTab::onChanged);
    connect(m_areaNo, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularNetworkTab::onChanged);
    connect(m_msPower, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularNetworkTab::onChanged);
    connect(m_carriers, &QLineEdit::editingFinished, this, &CellularNetworkTab::onChanged);
    connect(m_compandor, &QCheckBox::toggled, this, &CellularNetworkTab::onChanged);
    connect(m_callerId, &QCheckBox::toggled, this, &CellularNetworkTab::onChanged);
    connect(m_clock, &QCheckBox::toggled, this, &CellularNetworkTab::onChanged);
    connect(m_smsc, &QLineEdit::editingFinished, this, &CellularNetworkTab::onChanged);
    connect(m_fallback, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularNetworkTab::onChanged);
    connect(m_rxDeviceSet, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularNetworkTab::onChanged);
    connect(m_txDeviceSet, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularNetworkTab::onChanged);
}

// the countries of the system, the short name as the data
void CellularNetworkTab::fillCountries(const QString& selected)
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
    const int at = m_country->findData(selected);
    m_country->setCurrentIndex(at >= 0 ? at : 0);
    m_country->blockSignals(false);
}

void CellularNetworkTab::display(const CellularNetworkSettings& s)
{
    m_updating = true;
    m_id = s.m_id;
    m_name->setText(s.m_name);
    m_enabled->setChecked(s.m_enabled);
    m_system->setCurrentIndex(s.m_system == 1 ? 1 : 0);
    fillCountries(s.m_country);
    m_area->setValue(s.m_area);
    m_areaNo->setValue(s.m_areaNo);
    m_msPower->setValue(s.m_msPower);
    m_carriers->setText(s.m_carriers);
    m_compandor->setChecked(s.m_compandor);
    m_callerId->setChecked(s.m_sendCallerId);
    m_clock->setChecked(s.m_sendClock);
    m_smsc->setText(s.m_smscNumber);
    m_fallback->setCurrentIndex(s.m_fallback == CellularNetworkSettings::FallbackMusic ? 1 : 0);
    m_rxDeviceSet->setValue(s.m_rxDeviceSetIndex);
    m_txDeviceSet->setValue(s.m_txDeviceSetIndex);
    m_updating = false;
}

void CellularNetworkTab::read(CellularNetworkSettings& s) const
{
    s.m_id = m_id;
    s.m_name = m_name->text().trimmed().isEmpty() ? QString("Network %1").arg(m_id) : m_name->text().trimmed();
    s.m_enabled = m_enabled->isChecked();
    s.m_system = m_system->currentIndex();
    s.m_country = m_country->currentData().toString();
    s.m_area = m_area->value();
    s.m_areaNo = m_areaNo->value();
    s.m_msPower = m_msPower->value();
    s.m_carriers = m_carriers->text();
    s.m_compandor = m_compandor->isChecked();
    s.m_sendCallerId = m_callerId->isChecked();
    s.m_sendClock = m_clock->isChecked();
    s.m_smscNumber = m_smsc->text();
    s.m_fallback = m_fallback->currentIndex() == 1 ? CellularNetworkSettings::FallbackMusic : CellularNetworkSettings::FallbackEcho;
    s.m_rxDeviceSetIndex = m_rxDeviceSet->value();
    s.m_txDeviceSetIndex = m_txDeviceSet->value();
}

void CellularNetworkTab::onSystemChanged()
{
    fillCountries(m_country->currentData().toString());
    onChanged();
}

void CellularNetworkTab::onChanged()
{
    if (!m_updating) {
        emit changed();
    }
}

void CellularNetworkTab::appendLog(const QString& line)
{
    m_log->appendPlainText(timeStamp() + line);
}

void CellularNetworkTab::showStatus(const Cellular::MsgReportStatus& report)
{
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
}

// ---- the controller ----

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
    QWidget *networks = new QWidget(rollupContents);
    networks->setObjectName("networks");
    networks->setWindowTitle("Networks");
    QVBoxLayout *layout = new QVBoxLayout(networks);
    layout->setContentsMargins(3, 3, 3, 3);

    QHBoxLayout *controls = new QHBoxLayout();
    m_startStop = new QPushButton("Start", networks);
    m_startStop->setCheckable(true);
    m_startStop->setToolTip("Start the controller: the networks that are on go on the air");
    controls->addWidget(m_startStop);
    m_add = new QPushButton("Add network", networks);
    controls->addWidget(m_add);
    m_remove = new QPushButton("Remove network", networks);
    m_remove->setToolTip("Remove the network of the open tab (it stops if it runs)");
    controls->addWidget(m_remove);
    controls->addStretch();
    controls->addWidget(new QLabel("Transmit lead", networks));
    m_lead = new QSpinBox(networks);
    m_lead->setRange(20, 1000);
    m_lead->setSuffix(" ms");
    m_lead->setToolTip("How far the transmitters run ahead of the receivers. It is part of the round trip "
        "(keep the round trip below 300 ms); too little and the transmitters run empty. Taken when the controller starts");
    controls->addWidget(m_lead);
    layout->addLayout(controls);

    m_tabs = new QTabWidget(networks);
    layout->addWidget(m_tabs);
    m_log = new QPlainTextEdit(networks);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setMaximumHeight(70);
    layout->addWidget(m_log);
    networks->setMinimumWidth(520);

    connect(m_startStop, &QPushButton::toggled, this, &CellularGUI::onStartStop);
    connect(m_add, &QPushButton::clicked, this, &CellularGUI::onAdd);
    connect(m_remove, &QPushButton::clicked, this, &CellularGUI::onRemove);
    connect(m_lead, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularGUI::onLeadChanged);
}

CellularNetworkTab *CellularGUI::tabFor(int id) const
{
    for (int i = 0; i < m_tabs->count(); i++)
    {
        CellularNetworkTab *tab = qobject_cast<CellularNetworkTab*>(m_tabs->widget(i));

        if (tab && tab->id() == id) {
            return tab;
        }
    }
    return nullptr;
}

void CellularGUI::addTab(const CellularNetworkSettings& network)
{
    CellularNetworkTab *tab = new CellularNetworkTab(m_tabs);

    tab->display(network);
    m_tabs->addTab(tab, network.m_name + (network.m_enabled ? "" : " (off)"));
    connect(tab, &CellularNetworkTab::changed, this, &CellularGUI::onNetworkChanged);
}

void CellularGUI::displaySettings()
{
    m_doApplySettings = false;
    setTitleColor(QColor::fromRgb(m_settings.m_rgbColor));
    setWindowTitle(m_settings.m_title);
    setTitle(m_settings.m_title);
    m_lead->setValue(m_settings.m_leadMs);

    // the tabs of the networks that stay keep their state and logs
    for (int i = m_tabs->count() - 1; i >= 0; i--)
    {
        CellularNetworkTab *tab = qobject_cast<CellularNetworkTab*>(m_tabs->widget(i));

        if (!tab || !m_settings.network(tab->id()))
        {
            m_tabs->removeTab(i);
            delete tab;
        }
    }
    for (const CellularNetworkSettings& n : m_settings.m_networks)
    {
        CellularNetworkTab *tab = tabFor(n.m_id);

        if (!tab)
        {
            addTab(n);
            continue;
        }
        tab->display(n);
        m_tabs->setTabText(m_tabs->indexOf(tab), n.m_name + (n.m_enabled ? "" : " (off)"));
    }

    getRollupContents()->restoreState(m_rollupState);
    m_doApplySettings = true;
}

void CellularGUI::applySettings()
{
    if (m_doApplySettings) {
        m_cellular->getInputMessageQueue()->push(Cellular::MsgConfigureCellular::create(m_settings));
    }
}

void CellularGUI::onNetworkChanged()
{
    CellularNetworkTab *tab = qobject_cast<CellularNetworkTab*>(sender());

    if (!tab) {
        return;
    }
    for (CellularNetworkSettings& n : m_settings.m_networks)
    {
        if (n.m_id == tab->id())
        {
            tab->read(n);
            m_tabs->setTabText(m_tabs->indexOf(tab), n.m_name + (n.m_enabled ? "" : " (off)"));
        }
    }
    applySettings();
}

void CellularGUI::onLeadChanged()
{
    m_settings.m_leadMs = m_lead->value();
    applySettings();
}

void CellularGUI::onAdd()
{
    const CellularNetworkSettings& n = m_settings.addNetwork();

    addTab(n);
    m_tabs->setCurrentIndex(m_tabs->count() - 1);
    applySettings();
}

void CellularGUI::onRemove()
{
    CellularNetworkTab *tab = qobject_cast<CellularNetworkTab*>(m_tabs->currentWidget());

    if (!tab) {
        return;
    }
    for (int i = 0; i < m_settings.m_networks.size(); i++)
    {
        if (m_settings.m_networks[i].m_id == tab->id())
        {
            m_settings.m_networks.removeAt(i);
            break;
        }
    }
    m_tabs->removeTab(m_tabs->indexOf(tab));
    delete tab;
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
        const Cellular::MsgReportLog& report = static_cast<const Cellular::MsgReportLog&>(message);
        CellularNetworkTab *tab = report.getNetwork() ? tabFor(report.getNetwork()) : nullptr;

        if (tab) {
            tab->appendLog(report.getText());
        } else if (report.getNetwork()) {
            m_log->appendPlainText(timeStamp() + QString("network %1: ").arg(report.getNetwork()) + report.getText());
        } else {
            m_log->appendPlainText(timeStamp() + report.getText());
        }
        return true;
    }
    else if (Cellular::MsgReportStatus::match(message))
    {
        const Cellular::MsgReportStatus& report = static_cast<const Cellular::MsgReportStatus&>(message);
        CellularNetworkTab *tab = tabFor(report.m_network);

        if (tab) {
            tab->showStatus(report);
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
    // the controller may stop by itself
    const bool running = m_cellular->getState() == Feature::StRunning;
    if (m_startStop->isChecked() != running)
    {
        m_doApplySettings = false;
        m_startStop->setChecked(running);
        m_startStop->setText(running ? "Stop" : "Start");
        m_doApplySettings = true;
    }
}
