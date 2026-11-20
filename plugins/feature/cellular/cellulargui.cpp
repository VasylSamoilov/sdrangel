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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
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
    ColPhone,
    ColCount
};

enum PhoneColumn
{
    PhoneNumber,
    PhoneRegistered,
    PhoneChannel,
    PhoneState,
    PhoneLastHeard,
    PhoneLevel,
    PhoneQuality,
    PhonePower,
    PhoneLastFrame,
    PhoneColCount
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
    // the settings fold away while the network is tested, so the phones and the log have the room
    QToolButton *fold = new QToolButton(this);
    fold->setText("Settings");
    fold->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    fold->setArrowType(Qt::DownArrow);
    fold->setCheckable(true);
    fold->setChecked(true);
    fold->setAutoRaise(true);
    fold->setToolTip("Show or hide the settings of the network");
    layout->addWidget(fold);
    QWidget *settings = new QWidget(this);
    QFormLayout *form = new QFormLayout(settings);
    form->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(settings);
    connect(fold, &QToolButton::toggled, this, [fold, settings](bool open) {
        settings->setVisible(open);
        fold->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });

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
    m_y1 = new QSpinBox(this);
    m_y1->setRange(-1, 15);
    m_y1->setSpecialValueText("country");
    m_y1->setToolTip("Country digit Y1 of the traffic area. country: the one of the country (DK 5, SE 6, NO 7, FI 8 ...). "
        "0..15 (NMT-900 0..7) makes a test network: a phone only takes calling channels with the Y1 of its country selector, "
        "so networks with different Y1 show which one the phone is set to");
    form->addRow("Country digit Y1", m_y1);
    m_area = new QSpinBox(this);
    m_area->setRange(0, 15);
    m_area->setToolTip("Traffic area within the country (the second digit Y2): phones of a network with another area see a new network and roam");
    form->addRow("Traffic area", m_area);
    m_areaNo = new QSpinBox(this);
    m_areaNo->setRange(0, 4);
    m_areaNo->setSpecialValueText("none");
    m_areaNo->setToolTip("Area number 1..4 (the phones stay in their area), none: every phone is taken");
    form->addRow("Area number", m_areaNo);
    m_msPower = new QSpinBox(this);
    m_msPower->setRange(0, 3);
    m_msPower->setToolTip("Power level the phones send with (NMT Doc 450-3 5.7.1): 3 high (15 W or 7 W), "
        "2 medium (10 dB less), 1 and 0 low (20 dB less). A phone that receives the network strongly in a call "
        "goes to low power by itself");
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

    // what the tester does with a phone of this network
    QGridLayout *actions = new QGridLayout();
    layout->addLayout(actions);
    m_phone = new QLineEdit(this);
    m_phone->setPlaceholderText("3726136");
    m_phone->setToolTip("Number of the phone: the country digit and the six digits (Z X1..X6). "
        "Click a row of the Phones tab to take its number");
    actions->addWidget(new QLabel("Phone", this), 0, 0);
    actions->addWidget(m_phone, 0, 1);
    QPushButton *call = new QPushButton("Call", this);
    call->setToolTip("The network calls the phone: it is paged, registered or not, and rings. "
        "When it answers it hears the echo of itself or music, as set in Other numbers");
    QPushButton *hangUp = new QPushButton("Hang up", this);
    hangUp->setToolTip("Ends the call the network made to the phone");
    QPushButton *release = new QPushButton("Release", this);
    release->setToolTip("Releases any call of the phone from the network side");
    actions->addWidget(call, 0, 2);
    actions->addWidget(hangUp, 0, 3);
    actions->addWidget(release, 0, 4);
    m_smsText = new QLineEdit(this);
    m_smsText->setPlaceholderText("Short message to the phone");
    m_smsText->setToolTip("Text of a short message to the phone (NMT Doc 450-3 Annex 24): the phone is paged "
        "and called without ringing, the message goes over the data link of the call");
    QPushButton *sms = new QPushButton("Send", this);
    sms->setToolTip("Sends the short message to the phone; the log tells whether the phone took it");
    actions->addWidget(new QLabel("SMS", this), 1, 0);
    actions->addWidget(m_smsText, 1, 1, 1, 3);
    actions->addWidget(sms, 1, 4);
    m_mwiKind = new QComboBox(this);
    m_mwiKind->addItem("SMS waiting", 0x01);
    m_mwiKind->addItem("Voice mail waiting", 0x02);
    m_mwiKind->addItem("Fax waiting", 0x04);
    m_mwiKind->addItem("E-mail waiting", 0x08);
    m_mwiKind->addItem("Data waiting", 0x10);
    m_mwiKind->setToolTip("Message waiting indicator to show on the phone (frame 5c, NMT Doc 450-1 4.4.1.16). "
        "A phone in a call gets it when the call is cleared, an idle phone is called without ringing. "
        "The network can only set an indicator: the user clears it on the phone");
    QPushButton *mwi = new QPushButton("Send", this);
    mwi->setToolTip("Sends the message waiting indicator to the phone");
    actions->addWidget(new QLabel("Indicator", this), 2, 0);
    actions->addWidget(m_mwiKind, 2, 1, 1, 3);
    actions->addWidget(mwi, 2, 4);
    m_powerLevel = new QComboBox(this);
    m_powerLevel->addItem("High", 3);
    m_powerLevel->addItem("Medium", 2);
    m_powerLevel->addItem("Low", 1);
    m_powerLevel->setToolTip("Power level ordered to the phone in its call (NMT Doc 450-1 4.4.1.8): high 15 W or 7 W, "
        "medium 10 dB less, low 20 dB less");
    QPushButton *power = new QPushButton("Power", this);
    power->setToolTip("Orders the power level to the phone in its call; the log tells whether it took it");
    m_moveChannel = new QSpinBox(this);
    m_moveChannel->setRange(0, 2023);
    m_moveChannel->setSpecialValueText("any");
    m_moveChannel->setToolTip("Traffic channel to move the call to; any: the first free one");
    QPushButton *move = new QPushButton("Move", this);
    move->setToolTip("Moves the call of the phone to another traffic channel of the network (switching a call in progress, SCIP)");
    actions->addWidget(new QLabel("In call", this), 3, 0);
    actions->addWidget(m_powerLevel, 3, 1);
    actions->addWidget(power, 3, 2);
    actions->addWidget(m_moveChannel, 3, 3);
    actions->addWidget(move, 3, 4);
    actions->setColumnStretch(1, 1);

    m_views = new QTabWidget(this);
    layout->addWidget(m_views);
    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({"Ch", "Type", "Down MHz", "Up MHz", "State", "Rx dB", "Tx ms", "Empty", "Phone"});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(110);
    m_views->addTab(m_table, "Carriers");
    m_phones = new QTableWidget(0, PhoneColCount, this);
    m_phones->setHorizontalHeaderLabels({"Number", "Registered", "Ch", "State", "Last heard", "Rx dB", "Quality",
        "Power", "Last frame"});
    m_phones->horizontalHeaderItem(PhoneRegistered)->setToolTip("Yes: the phone made a roaming update to this network; "
        "no: it was only heard (a call, an answer)");
    m_phones->horizontalHeaderItem(PhoneLevel)->setToolTip("Received level of its channel when it was last heard, dB of full scale");
    m_phones->horizontalHeaderItem(PhoneQuality)->setToolTip("Quality of its last frame, 0..1");
    m_phones->horizontalHeaderItem(PhonePower)->setToolTip("Power level in its frames: high, medium or low");
    m_phones->verticalHeader()->setVisible(false);
    m_phones->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_phones->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_phones->horizontalHeader()->setStretchLastSection(true);
    m_phones->setMinimumHeight(110);
    m_views->addTab(m_phones, "Phones");
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setMinimumHeight(100);
    m_views->addTab(m_log, "Log");

    connect(m_phones, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (QTableWidgetItem *item = m_phones->item(row, PhoneNumber)) {
            m_phone->setText(item->text());
        }
    });
    connect(call, &QPushButton::clicked, this, [this]() { requestAction(Cellular::MsgAction::Call, QString(), 0); });
    connect(hangUp, &QPushButton::clicked, this, [this]() { requestAction(Cellular::MsgAction::HangUp, QString(), 0); });
    connect(release, &QPushButton::clicked, this, [this]() { requestAction(Cellular::MsgAction::Release, QString(), 0); });
    connect(sms, &QPushButton::clicked, this, [this]() { requestAction(Cellular::MsgAction::Sms, m_smsText->text(), 0); });
    connect(mwi, &QPushButton::clicked, this, [this]() {
        requestAction(Cellular::MsgAction::Mwi, QString(), m_mwiKind->currentData().toInt());
    });
    connect(power, &QPushButton::clicked, this, [this]() {
        requestAction(Cellular::MsgAction::Power, QString(), m_powerLevel->currentData().toInt());
    });
    connect(move, &QPushButton::clicked, this, [this]() {
        requestAction(Cellular::MsgAction::Move, QString(), m_moveChannel->value());
    });

    connect(m_system, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularNetworkTab::onSystemChanged);
    connect(m_name, &QLineEdit::editingFinished, this, &CellularNetworkTab::onChanged);
    connect(m_enabled, &QCheckBox::toggled, this, &CellularNetworkTab::onChanged);
    connect(m_country, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularNetworkTab::onChanged);
    connect(m_y1, QOverload<int>::of(&QSpinBox::valueChanged), this, &CellularNetworkTab::onChanged);
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
    m_y1->setValue(s.m_y1);
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
    s.m_y1 = m_y1->value();
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

void CellularNetworkTab::requestAction(int action, const QString& text, int value)
{
    emit actionRequested(m_id, action, m_phone->text().trimmed(), text, value);
}

static QString powerName(int level)
{
    switch (level)
    {
    case 3: return "high";
    case 2: return "medium";
    case 1:
    case 0: return "low";
    default: return QString();
    }
}

// A phone keeps its row: rows are updated in place and new phones come at the end, so the view
// does not move under the user.
void CellularNetworkTab::showPhones(const QList<Cellular::MsgReportStatus::Phone>& phones)
{
    for (const Cellular::MsgReportStatus::Phone& p : phones)
    {
        int row = 0;
        while (row < m_phones->rowCount() && (!m_phones->item(row, PhoneNumber) || m_phones->item(row, PhoneNumber)->text() != p.number)) {
            row++;
        }
        if (row == m_phones->rowCount()) {
            m_phones->insertRow(row);
        }
        const QStringList cells = {
            p.number,
            p.registered ? "yes" : "no",
            p.channel ? QString::number(p.channel) : QString(),
            p.state,
            p.lastHeard.isValid() ? p.lastHeard.toString("HH:mm:ss") : QString(),
            p.lastHeard.isValid() ? QString::number(p.level, 'f', 1) : QString(),
            p.lastHeard.isValid() ? QString::number(p.quality, 'f', 2) : QString(),
            powerName(p.power),
            p.lastFrame
        };
        for (int col = 0; col < PhoneColCount; col++)
        {
            QTableWidgetItem *item = m_phones->item(row, col);
            if (!item)
            {
                item = new QTableWidgetItem();
                m_phones->setItem(row, col, item);
            }
            item->setText(cells[col]);
        }
    }
}

void CellularNetworkTab::showStatus(const Cellular::MsgReportStatus& report)
{
    m_summary->setText(report.m_summary);
    showPhones(report.m_phones);
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
            on ? QString::number(c.underruns) : QString(),
            c.phone
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
    connect(tab, &CellularNetworkTab::actionRequested, this,
        [this](int network, int action, const QString& number, const QString& text, int value) {
            m_cellular->getInputMessageQueue()->push(Cellular::MsgAction::create(network,
                static_cast<Cellular::MsgAction::Action>(action), number, text, value));
        });
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
