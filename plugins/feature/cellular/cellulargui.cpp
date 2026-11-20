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

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "feature/featureuiset.h"
#include "gui/buttonswitch.h"
#include "gui/flowlayout.h"
#include "gui/rollupcontents.h"
#include "maincore.h"

#include "nmtplan.h"

#include "cellulargui.h"

const int CellularGUI::m_maxLogLines;

namespace
{

enum PhoneColumn
{
    PhoneNumber,
    PhoneState,
    PhoneChannel,
    PhoneNetwork,
    PhoneRegistered,
    PhoneLastHeard,
    PhoneLevel,
    PhoneQuality,
    PhonePower,
    PhoneLastFrame,
    PhonePassword,
    PhoneSmsCentre,
    PhoneDialled,
    PhoneKept,
    PhoneColCount
};

enum NetworkColumn
{
    NetOn,
    NetName,
    NetSystem,
    NetCountry,
    NetY1,
    NetArea,
    NetCarriers,
    NetColCount
};

enum CarrierColumn
{
    CarNetwork,
    CarChannel,
    CarType,
    CarDownlink,
    CarUplink,
    CarState,
    CarRxLevel,
    CarTxFill,
    CarUnderruns,
    CarPhone,
    CarColCount
};

// Tables that fit a small screen: they shrink, scroll and do not need much height.
QTableWidget *makeTable(QWidget *parent, const QStringList& headers)
{
    QTableWidget *table = new QTableWidget(0, headers.size(), parent);

    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setMinimumHeight(60);
    table->setMinimumWidth(0);
    return table;
}

QToolButton *iconButton(QWidget *parent, const QString& icon, const QString& tip)
{
    QToolButton *button = new QToolButton(parent);

    button->setIcon(QIcon(icon));
    button->setToolTip(tip);
    return button;
}

// A row of controls that wraps when the window is narrow.
QWidget *flowRow(QWidget *parent, const QList<QWidget*>& widgets)
{
    QWidget *row = new QWidget(parent);
    FlowLayout *flow = new FlowLayout(row, 0, 3, 3);

    for (QWidget *w : widgets)
    {
        w->setParent(row);
        flow->addWidget(w);
    }
    return row;
}

void setCell(QTableWidget *table, int row, int col, const QString& text)
{
    QTableWidgetItem *item = table->item(row, col);

    if (!item)
    {
        item = new QTableWidgetItem();
        table->setItem(row, col, item);
    }
    if (item->text() != text) {
        item->setText(text);
    }
}

QString powerName(int level)
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

} // namespace

// ---- the settings of one network ----

CellularNetworkDialog::CellularNetworkDialog(const CellularNetworkSettings& settings, QWidget *parent) :
    QDialog(parent),
    m_settings(settings)
{
    setWindowTitle(QString("Network %1").arg(settings.m_name));
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    // the form scrolls on a small screen
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QWidget *page = new QWidget(scroll);
    QFormLayout *form = new QFormLayout(page);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(page);
    layout->addWidget(scroll);

    m_name = new QLineEdit(page);
    m_name->setToolTip("Name of the network, in the lists of the tester");
    form->addRow("Name", m_name);
    m_enabled = new QCheckBox("On the air while the tester runs", page);
    form->addRow(m_enabled);
    m_system = new QComboBox(page);
    m_system->addItems({"NMT-450", "NMT-900"});
    form->addRow("System", m_system);
    m_country = new QComboBox(page);
    m_country->setToolTip("The frequency plan of the country (the channel numbers, the country digit of the traffic area)");
    form->addRow("Country", m_country);
    m_y1 = new QSpinBox(page);
    m_y1->setRange(-1, 15);
    m_y1->setSpecialValueText("country");
    m_y1->setToolTip("Country digit Y1 of the traffic area. country: the one of the country (DK 5, SE 6, NO 7, FI 8 ...). "
        "0..15 (NMT-900 0..7) makes a test network: a phone only takes calling channels with the Y1 of its country selector, "
        "so networks with different Y1 show which one the phone is set to");
    form->addRow("Country digit Y1", m_y1);
    m_area = new QSpinBox(page);
    m_area->setRange(0, 15);
    m_area->setToolTip("Traffic area within the country (the second digit Y2): phones of a network with another area see a new network and roam");
    form->addRow("Traffic area", m_area);
    m_areaNo = new QSpinBox(page);
    m_areaNo->setRange(0, 4);
    m_areaNo->setSpecialValueText("none");
    m_areaNo->setToolTip("Area number 1..4 (the phones stay in their area), none: every phone is taken");
    form->addRow("Area number", m_areaNo);
    m_msPower = new QSpinBox(page);
    m_msPower->setRange(0, 3);
    m_msPower->setToolTip("Power level the phones send with (NMT Doc 450-3 5.7.1): 3 high (15 W or 7 W), "
        "2 medium (10 dB less), 1 and 0 low (20 dB less). A phone that receives the network strongly in a call "
        "goes to low power by itself");
    form->addRow("Phone power", m_msPower);
    m_carriers = new QLineEdit(page);
    m_carriers->setToolTip("channel:type[:supervisory], comma separated; every channel needs its type. Types: cc (calling), "
        "tc (traffic), cctc (both), actc, cca, ccb (NMT-900), test. Supervisory tone 1..4 (0 none) on every traffic channel, "
        "or on none for 1..4 in turn. Example: 1:cctc:1, 5:tc:2, 6:tc:3");
    form->addRow("Carriers", m_carriers);
    m_compandor = new QCheckBox("Compandor on speech", page);
    form->addRow(m_compandor);
    m_callerId = new QCheckBox("Send the number of the caller", page);
    form->addRow(m_callerId);
    m_clock = new QCheckBox("Send the time of day", page);
    form->addRow(m_clock);
    m_smsc = new QLineEdit(page);
    m_smsc->setToolTip("The number the phones send short messages to; empty: no short messages from the phones");
    form->addRow("SMS centre", m_smsc);
    m_fallback = new QComboBox(page);
    m_fallback->addItems({"Echo", "Music"});
    m_fallback->setToolTip("What answers a call to a number that is not registered in this network");
    form->addRow("Other numbers", m_fallback);
    m_rxDeviceSet = new QSpinBox(page);
    m_rxDeviceSet->setRange(0, 99);
    m_rxDeviceSet->setToolTip("The device set (R0, R1 ...) with the Cellular Rx channel; networks may share it");
    form->addRow("Receiver device set", m_rxDeviceSet);
    m_txDeviceSet = new QSpinBox(page);
    m_txDeviceSet->setRange(0, 99);
    m_txDeviceSet->setToolTip("The device set (T1, T2 ...) with the Cellular Tx channel; networks may share it");
    form->addRow("Transmitter device set", m_txDeviceSet);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_name->setText(settings.m_name);
    m_enabled->setChecked(settings.m_enabled);
    m_system->setCurrentIndex(settings.m_system == 1 ? 1 : 0);
    fillCountries(settings.m_country);
    m_y1->setValue(settings.m_y1);
    m_area->setValue(settings.m_area);
    m_areaNo->setValue(settings.m_areaNo);
    m_msPower->setValue(settings.m_msPower);
    m_carriers->setText(settings.m_carriers);
    m_compandor->setChecked(settings.m_compandor);
    m_callerId->setChecked(settings.m_sendCallerId);
    m_clock->setChecked(settings.m_sendClock);
    m_smsc->setText(settings.m_smscNumber);
    m_fallback->setCurrentIndex(settings.m_fallback == CellularNetworkSettings::FallbackMusic ? 1 : 0);
    m_rxDeviceSet->setValue(settings.m_rxDeviceSetIndex);
    m_txDeviceSet->setValue(settings.m_txDeviceSetIndex);

    connect(m_system, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        fillCountries(m_country->currentData().toString());
    });
}

// the countries of the system, the short name as the data
void CellularNetworkDialog::fillCountries(const QString& selected)
{
    const modemnmt::System system = m_system->currentIndex() == 1 ? modemnmt::System::Nmt900 : modemnmt::System::Nmt450;
    QStringList seen;

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
}

void CellularNetworkDialog::read(CellularNetworkSettings& s) const
{
    s.m_name = m_name->text().trimmed().isEmpty() ? QString("Network %1").arg(s.m_id) : m_name->text().trimmed();
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

// ---- the tester ----

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
    QWidget *main = new QWidget(rollupContents);
    main->setObjectName("cellular");
    main->setWindowTitle("Cellular");
    QVBoxLayout *layout = new QVBoxLayout(main);
    layout->setContentsMargins(3, 3, 3, 3);

    // the tester on or off, what is on the air, the options
    QHBoxLayout *top = new QHBoxLayout();
    m_startStop = new ButtonSwitch(main);
    QIcon startStop;
    startStop.addFile(":/play.png", QSize(16, 16), QIcon::Normal, QIcon::Off);
    startStop.addFile(":/stop.png", QSize(16, 16), QIcon::Normal, QIcon::On);
    m_startStop->setIcon(startStop);
    m_startStop->setToolTip("Start or stop the tester: the networks that are on go on the air");
    top->addWidget(m_startStop);
    m_summary = new QLabel("Stopped", main);
    m_summary->setWordWrap(true);
    m_summary->setMinimumWidth(0);
    m_summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addWidget(m_summary, 1);
    QToolButton *options = iconButton(main, ":/listing.png", "Options of the tester");
    top->addWidget(options);
    layout->addLayout(top);

    m_tabs = new QTabWidget(main);
    m_tabs->setTabPosition(QTabWidget::West);
    m_tabs->addTab(buildPhones(), "Phones");
    m_tabs->addTab(buildTest(), "Test");
    m_tabs->addTab(buildLog(), "Log");
    m_tabs->addTab(buildNetworks(), "Networks");
    m_tabs->addTab(buildCarriers(), "Carriers");
    layout->addWidget(m_tabs, 1);
    main->setMinimumWidth(0);
    // the tabs take the height of the window
    main->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(m_startStop, &ButtonSwitch::toggled, this, &CellularGUI::onStartStop);
    connect(options, &QToolButton::clicked, this, &CellularGUI::openSettings);
}

QWidget *CellularGUI::buildPhones()
{
    QWidget *page = new QWidget(m_tabs);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_phones = makeTable(page, {"Number", "State", "Ch", "Network", "Reg", "Heard", "Rx dB", "Q", "Power", "Last frame",
        "K", "SMS centre", "Dialled", "Kept"});
    m_phones->horizontalHeaderItem(PhonePassword)->setToolTip("Password K of the phone as its seizure frames show it");
    m_phones->horizontalHeaderItem(PhoneSmsCentre)->setToolTip("Numbers the phone sent short messages to or asked "
        "for its waiting messages on, with the traffic area of that network");
    m_phones->horizontalHeaderItem(PhoneDialled)->setToolTip("Numbers the phone dialled, the newest first");
    m_phones->horizontalHeaderItem(PhoneKept)->setToolTip("Short messages the centre keeps for the phone, and "
        "indicators postponed until it is free");
    m_phones->horizontalHeaderItem(PhoneRegistered)->setToolTip("Yes: the phone made a roaming update to this network; "
        "no: it was only heard (a call, an answer)");
    m_phones->horizontalHeaderItem(PhoneLevel)->setToolTip("Received level of its channel when it was last heard, dB of full scale");
    m_phones->horizontalHeaderItem(PhoneQuality)->setToolTip("Quality of its last frame, 0..1");
    m_phones->horizontalHeaderItem(PhonePower)->setToolTip("Power level in its frames: high, medium or low");
    m_phones->setToolTip("Every phone a network heard (its identity in a frame for the channel it came on) or that "
        "registered. Right click: actions on the phone; double click: the phone in the Test tab");
    m_phones->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_phones);

    connect(m_phones, &QTableWidget::customContextMenuRequested, this, &CellularGUI::phoneMenu);
    connect(m_phones, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem *item = m_phones->item(row, PhoneNumber);
        if (!item) {
            return;
        }
        const int at = m_testNetwork->findData(item->data(Qt::UserRole).toInt());
        if (at >= 0) {
            m_testNetwork->setCurrentIndex(at);
        }
        m_testPhone->setCurrentText(item->text());
        m_tabs->setCurrentIndex(1);
    });
    return page;
}

QWidget *CellularGUI::buildTest()
{
    QWidget *page = new QWidget(m_tabs);
    QFormLayout *form = new QFormLayout(page);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setContentsMargins(3, 3, 3, 3);

    m_testNetwork = new QComboBox(page);
    m_testNetwork->setToolTip("The network that does the action");
    form->addRow("Network", m_testNetwork);
    m_testPhone = new QComboBox(page);
    m_testPhone->setEditable(true);
    m_testPhone->setMinimumContentsLength(7);
    m_testPhone->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_testPhone->lineEdit()->setPlaceholderText("3726136");
    m_testPhone->setToolTip("Number of the phone: the country digit and the six digits (Z X1..X6), "
        "or one the network heard");
    form->addRow("Phone", m_testPhone);
    m_testState = new QLabel(page);
    m_testState->setWordWrap(true);
    form->addRow(m_testState);

    QPushButton *call = new QPushButton("Call");
    call->setToolTip("The network calls the phone: it is paged, registered or not, and rings. "
        "When it answers it hears the echo of itself or music, as set in Other numbers of the network");
    QPushButton *hangUp = new QPushButton("Hang up");
    hangUp->setToolTip("Ends the call the network made to the phone");
    QPushButton *release = new QPushButton("Release");
    release->setToolTip("Releases any call of the phone from the network side");
    form->addRow("Call", flowRow(page, {call, hangUp, release}));

    m_smsText = new QLineEdit(page);
    m_smsText->setPlaceholderText("Short message to the phone");
    m_smsText->setToolTip("Text of a short message to the phone (NMT Doc 450-3 Annex 24)");
    m_smsText->setMinimumWidth(80);
    QPushButton *sms = new QPushButton("Send", page);
    sms->setToolTip("The phone is paged and called without ringing, the message goes over the data link of the call; "
        "the log tells whether the phone took it");
    QPushButton *keep = new QPushButton("Keep", page);
    keep->setToolTip("The SMS centre keeps the message until the phone calls the centre for its waiting messages, "
        "e.g. after an SMS indicator");
    QHBoxLayout *smsRow = new QHBoxLayout();
    smsRow->setSpacing(3);
    smsRow->addWidget(m_smsText, 1);
    smsRow->addWidget(sms);
    smsRow->addWidget(keep);
    form->addRow("SMS", smsRow);

    m_mwiKind = new QComboBox();
    m_mwiKind->addItem("SMS waiting", 0x01);
    m_mwiKind->addItem("Voice mail waiting", 0x02);
    m_mwiKind->addItem("Fax waiting", 0x04);
    m_mwiKind->addItem("E-mail waiting", 0x08);
    m_mwiKind->addItem("Data waiting", 0x10);
    m_mwiKind->setToolTip("Message waiting indicator (frame 5c, NMT Doc 450-1 4.4.1.16). An idle phone is called "
        "without ringing and gets it in the clearing of that call; a phone in a conversation gets it at once and in "
        "the clearing; a busy phone gets it when it is free. The phone switches an indicator off, not the network");
    QPushButton *mwi = new QPushButton("Send");
    mwi->setToolTip("Sends the message waiting indicator to the phone");
    form->addRow("Indicator", flowRow(page, {m_mwiKind, mwi}));

    m_powerLevel = new QComboBox();
    m_powerLevel->addItem("High", 3);
    m_powerLevel->addItem("Medium", 2);
    m_powerLevel->addItem("Low", 1);
    m_powerLevel->setToolTip("Power level ordered to the phone in its call (NMT Doc 450-1 4.4.1.8): high 15 W or 7 W, "
        "medium 10 dB less, low 20 dB less");
    QPushButton *power = new QPushButton("Order");
    power->setToolTip("Orders the power level to the phone in its call; the log tells whether it took it");
    form->addRow("Power", flowRow(page, {m_powerLevel, power}));

    m_moveChannel = new QSpinBox();
    m_moveChannel->setRange(0, 2023);
    m_moveChannel->setSpecialValueText("any");
    m_moveChannel->setToolTip("Traffic channel to move the call to; any: the first free one");
    QPushButton *move = new QPushButton("Move");
    move->setToolTip("Moves the call of the phone to another traffic channel of the network (switching a call in progress)");
    form->addRow("Move", flowRow(page, {m_moveChannel, move}));

    auto act = [this](int action, const QString& text, int value) {
        requestAction(m_testNetwork->currentData().toInt(), m_testPhone->currentText().trimmed(), action, text, value);
    };
    connect(call, &QPushButton::clicked, this, [act]() { act(Cellular::MsgAction::Call, QString(), 0); });
    connect(hangUp, &QPushButton::clicked, this, [act]() { act(Cellular::MsgAction::HangUp, QString(), 0); });
    connect(release, &QPushButton::clicked, this, [act]() { act(Cellular::MsgAction::Release, QString(), 0); });
    connect(sms, &QPushButton::clicked, this, [this, act]() { act(Cellular::MsgAction::Sms, m_smsText->text(), 0); });
    connect(keep, &QPushButton::clicked, this, [this, act]() { act(Cellular::MsgAction::Hold, m_smsText->text(), 0); });
    connect(mwi, &QPushButton::clicked, this, [this, act]() {
        act(Cellular::MsgAction::Mwi, QString(), m_mwiKind->currentData().toInt());
    });
    connect(power, &QPushButton::clicked, this, [this, act]() {
        act(Cellular::MsgAction::Power, QString(), m_powerLevel->currentData().toInt());
    });
    connect(move, &QPushButton::clicked, this, [this, act]() { act(Cellular::MsgAction::Move, QString(), m_moveChannel->value()); });
    connect(m_testNetwork, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { showTestPhone(); });
    connect(m_testPhone, &QComboBox::currentTextChanged, this, [this]() { showTestPhone(); });
    return page;
}

QWidget *CellularGUI::buildLog()
{
    QWidget *page = new QWidget(m_tabs);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_logNetwork = new QComboBox();
    m_logNetwork->setToolTip("The events of all networks, of the tester itself or of one network");
    m_logFrames = new QCheckBox("Frames");
    m_logFrames->setChecked(true);
    m_logFrames->setToolTip("Show the frames from and to the phones (ch 18 < 10b ...), not only the events");
    QToolButton *clear = iconButton(nullptr, ":/bin.png", "Clear the log");
    layout->addWidget(flowRow(page, {m_logNetwork, m_logFrames, clear}));
    m_log = new QPlainTextEdit(page);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);    // a narrow window shows whole lines
    m_log->setMinimumHeight(60);
    layout->addWidget(m_log);

    connect(m_logNetwork, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { renderLog(); });
    connect(m_logFrames, &QCheckBox::toggled, this, [this]() { renderLog(); });
    connect(clear, &QToolButton::clicked, this, [this]() {
        m_logLines.clear();
        m_log->clear();
    });
    return page;
}

QWidget *CellularGUI::buildNetworks()
{
    QWidget *page = new QWidget(m_tabs);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_networks = makeTable(page, {"On", "Name", "System", "Country", "Y1", "Area", "Carriers"});
    m_networks->horizontalHeaderItem(NetOn)->setToolTip("On the air while the tester runs: switch a network on or off "
        "without touching the others");
    m_networks->setToolTip("The networks of the tester; double click to edit one");
    layout->addWidget(m_networks);
    QToolButton *add = iconButton(nullptr, ":/plus.png", "Add a network (off until switched on)");
    QToolButton *edit = iconButton(nullptr, ":/edit.png", "Edit the settings of the selected network");
    QToolButton *remove = iconButton(nullptr, ":/bin.png", "Remove the selected network (it stops if it runs)");
    layout->addWidget(flowRow(page, {add, edit, remove}));

    connect(m_networks, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        const QTableWidgetItem *item = m_networks->item(row, NetName);
        if (item && col != NetOn) {
            editNetwork(item->data(Qt::UserRole).toInt());
        }
    });
    connect(m_networks, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!m_doApplySettings || item->column() != NetOn) {
            return;
        }
        const QTableWidgetItem *name = m_networks->item(item->row(), NetName);
        const int id = name ? name->data(Qt::UserRole).toInt() : 0;
        for (CellularNetworkSettings& n : m_settings.m_networks)
        {
            if (n.m_id == id && n.m_enabled != (item->checkState() == Qt::Checked))
            {
                n.m_enabled = item->checkState() == Qt::Checked;
                applySettings();
            }
        }
    });
    connect(add, &QToolButton::clicked, this, [this]() {
        const int id = m_settings.addNetwork().m_id;
        displaySettings();
        applySettings();
        editNetwork(id);
    });
    connect(edit, &QToolButton::clicked, this, [this]() {
        if (const int id = selectedNetwork()) {
            editNetwork(id);
        }
    });
    connect(remove, &QToolButton::clicked, this, [this]() {
        const int id = selectedNetwork();
        if (!id || QMessageBox::question(this, "Remove network", QString("Remove the network %1?").arg(networkName(id)))
                != QMessageBox::Yes) {
            return;
        }
        for (int i = 0; i < m_settings.m_networks.size(); i++)
        {
            if (m_settings.m_networks[i].m_id == id)
            {
                m_settings.m_networks.removeAt(i);
                break;
            }
        }
        m_status.remove(id);
        displaySettings();
        applySettings();
        showCarriers();
        showSummary();
    });
    return page;
}

QWidget *CellularGUI::buildCarriers()
{
    QWidget *page = new QWidget(m_tabs);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_carriers = makeTable(page, {"Network", "Ch", "Type", "Down MHz", "Up MHz", "State", "Rx dB", "Tx ms", "Empty", "Phone"});
    m_carriers->horizontalHeaderItem(CarTxFill)->setToolTip("Milliseconds of audio in the transmit queue");
    m_carriers->horizontalHeaderItem(CarUnderruns)->setToolTip("Times the transmit queue ran empty: the transmit lead is too small");
    m_carriers->setToolTip("The carriers of the networks on the air. Red: it cannot go on the air, with the reason");
    layout->addWidget(m_carriers);
    return page;
}

void CellularGUI::openSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle("Cellular options");
    QFormLayout *form = new QFormLayout(&dialog);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    QSpinBox *lead = new QSpinBox(&dialog);
    lead->setRange(20, 1000);
    lead->setSuffix(" ms");
    lead->setValue(m_settings.m_leadMs);
    lead->setToolTip("How far the transmitters run ahead of the receivers. It is part of the round trip "
        "(keep the round trip below 300 ms); too little and the transmitters run empty. Taken when the tester starts");
    form->addRow("Transmit lead", lead);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted && lead->value() != m_settings.m_leadMs)
    {
        m_settings.m_leadMs = lead->value();
        applySettings();
    }
}

// The dialog edits a copy; settings that came meanwhile are kept, the network is found again by its id.
void CellularGUI::editNetwork(int id)
{
    const CellularNetworkSettings *current = m_settings.network(id);

    if (!current) {
        return;
    }
    CellularNetworkDialog dialog(*current, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto it = std::find_if(m_settings.m_networks.begin(), m_settings.m_networks.end(),
        [id](const CellularNetworkSettings& n) { return n.m_id == id; });
    if (it != m_settings.m_networks.end())
    {
        dialog.read(*it);
        displaySettings();
        applySettings();
    }
}

int CellularGUI::selectedNetwork() const
{
    // the highlighted row (the current index is not always set: rows are selected by the program too)
    const QModelIndexList rows = m_networks->selectionModel()->selectedRows();
    const int row = rows.isEmpty() ? m_networks->currentRow() : rows.first().row();
    const QTableWidgetItem *item = row >= 0 ? m_networks->item(row, NetName) : nullptr;

    return item ? item->data(Qt::UserRole).toInt() : 0;
}

QString CellularGUI::networkName(int id) const
{
    const CellularNetworkSettings *n = m_settings.network(id);

    return n ? n->m_name : QString("network %1").arg(id);
}

void CellularGUI::requestAction(int network, const QString& number, int action, const QString& text, int value)
{
    m_cellular->getInputMessageQueue()->push(Cellular::MsgAction::create(network,
        static_cast<Cellular::MsgAction::Action>(action), number, text, value));
}

void CellularGUI::phoneMenu(const QPoint& pos)
{
    const QTableWidgetItem *item = m_phones->itemAt(pos);
    const QTableWidgetItem *numberItem = item ? m_phones->item(item->row(), PhoneNumber) : nullptr;

    if (!numberItem) {
        return;
    }
    const QString number = numberItem->text();
    const int network = numberItem->data(Qt::UserRole).toInt();
    QMenu menu(this);

    menu.addAction("Call", this, [=]() { requestAction(network, number, Cellular::MsgAction::Call, QString(), 0); });
    menu.addAction("Hang up", this, [=]() { requestAction(network, number, Cellular::MsgAction::HangUp, QString(), 0); });
    menu.addAction("Release", this, [=]() { requestAction(network, number, Cellular::MsgAction::Release, QString(), 0); });
    menu.addSeparator();
    menu.addAction("SMS...", this, [=]() {
        bool ok = false;
        const QString text = QInputDialog::getText(this, "SMS", QString("Short message to %1").arg(number), QLineEdit::Normal, QString(), &ok);
        if (ok) {
            requestAction(network, number, Cellular::MsgAction::Sms, text, 0);
        }
    });
    menu.addAction("Keep SMS...", this, [=]() {
        bool ok = false;
        const QString text = QInputDialog::getText(this, "Keep SMS", QString("Message the centre keeps for %1").arg(number),
            QLineEdit::Normal, QString(), &ok);
        if (ok) {
            requestAction(network, number, Cellular::MsgAction::Hold, text, 0);
        }
    });
    QMenu *indicator = menu.addMenu("Indicator");
    for (int i = 0; i < m_mwiKind->count(); i++)
    {
        const int flags = m_mwiKind->itemData(i).toInt();
        indicator->addAction(m_mwiKind->itemText(i), this, [=]() {
            requestAction(network, number, Cellular::MsgAction::Mwi, QString(), flags);
        });
    }
    QMenu *power = menu.addMenu("Power in call");
    for (int i = 0; i < m_powerLevel->count(); i++)
    {
        const int level = m_powerLevel->itemData(i).toInt();
        power->addAction(m_powerLevel->itemText(i), this, [=]() {
            requestAction(network, number, Cellular::MsgAction::Power, QString(), level);
        });
    }
    menu.addAction("Move call to a free channel", this, [=]() {
        requestAction(network, number, Cellular::MsgAction::Move, QString(), 0);
    });
    menu.addSeparator();
    menu.addAction("Test this phone", this, [this, network, number]() {
        const int at = m_testNetwork->findData(network);
        if (at >= 0) {
            m_testNetwork->setCurrentIndex(at);
        }
        m_testPhone->setCurrentText(number);
        m_tabs->setCurrentIndex(1);
    });
    menu.exec(m_phones->viewport()->mapToGlobal(pos));
}

// The lists of networks of the Test and Log tabs follow the settings; the choices stay.
void CellularGUI::fillNetworkCombos()
{
    const int test = m_testNetwork->currentData().toInt();
    const QVariant log = m_logNetwork->currentData();

    m_testNetwork->blockSignals(true);
    m_logNetwork->blockSignals(true);
    m_testNetwork->clear();
    m_logNetwork->clear();
    m_logNetwork->addItem("All networks", -1);
    m_logNetwork->addItem("Tester", 0);
    for (const CellularNetworkSettings& n : m_settings.m_networks)
    {
        m_testNetwork->addItem(n.m_name, n.m_id);
        m_logNetwork->addItem(n.m_name, n.m_id);
    }
    const int testAt = m_testNetwork->findData(test);
    m_testNetwork->setCurrentIndex(testAt >= 0 ? testAt : 0);
    const int logAt = m_logNetwork->findData(log.isValid() ? log.toInt() : -1);
    m_logNetwork->setCurrentIndex(logAt >= 0 ? logAt : 0);
    m_testNetwork->blockSignals(false);
    m_logNetwork->blockSignals(false);
    showTestPhone();
}

// The networks table is updated in place: rows stay, the selection stays.
void CellularGUI::displaySettings()
{
    m_doApplySettings = false;
    setTitleColor(QColor::fromRgb(m_settings.m_rgbColor));
    setWindowTitle(m_settings.m_title);
    setTitle(m_settings.m_title);

    const int selected = selectedNetwork();
    m_networks->setRowCount(m_settings.m_networks.size());
    for (int row = 0; row < m_settings.m_networks.size(); row++)
    {
        const CellularNetworkSettings& n = m_settings.m_networks[row];
        QTableWidgetItem *on = m_networks->item(row, NetOn);
        if (!on)
        {
            on = new QTableWidgetItem();
            on->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_networks->setItem(row, NetOn, on);
        }
        on->setCheckState(n.m_enabled ? Qt::Checked : Qt::Unchecked);
        setCell(m_networks, row, NetName, n.m_name);
        m_networks->item(row, NetName)->setData(Qt::UserRole, n.m_id);
        setCell(m_networks, row, NetSystem, n.m_system == 1 ? "NMT-900" : "NMT-450");
        setCell(m_networks, row, NetCountry, n.m_country);
        setCell(m_networks, row, NetY1, n.m_y1 < 0 ? "country" : QString::number(n.m_y1));
        setCell(m_networks, row, NetArea, QString::number(n.m_area));
        setCell(m_networks, row, NetCarriers, n.m_carriers);
        if (n.m_id == selected) {
            m_networks->selectionModel()->select(m_networks->model()->index(row, 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
    fillNetworkCombos();

    getRollupContents()->restoreState(m_rollupState);
    m_doApplySettings = true;
}

void CellularGUI::applySettings()
{
    if (m_doApplySettings) {
        m_cellular->getInputMessageQueue()->push(Cellular::MsgConfigureCellular::create(m_settings));
    }
}

void CellularGUI::onStartStop(bool checked)
{
    if (!m_doApplySettings) {
        return;
    }
    m_cellular->getInputMessageQueue()->push(Cellular::MsgStartStop::create(checked));
}

// ---- what the tester reports ----

void CellularGUI::showStatus(const Cellular::MsgReportStatus& report)
{
    NetworkStatus& s = m_status[report.m_network];

    s.onAir = report.m_onAir;
    s.registered = report.m_registered;
    s.calls = report.m_calls;
    s.summary = report.m_summary;
    s.carriers = report.m_carriers;
    s.phones = report.m_phones;
    showPhones(report.m_network, report.m_phones);
    showCarriers();
    showSummary();
    showTestPhone();
}

// A phone keeps its row (by network and number): rows are updated in place and new phones come at
// the end, so the view does not move under the user.
void CellularGUI::showPhones(int network, const QList<Cellular::MsgReportStatus::Phone>& phones)
{
    for (const Cellular::MsgReportStatus::Phone& p : phones)
    {
        int row = 0;
        for (; row < m_phones->rowCount(); row++)
        {
            const QTableWidgetItem *item = m_phones->item(row, PhoneNumber);
            if (item && item->text() == p.number && item->data(Qt::UserRole).toInt() == network) {
                break;
            }
        }
        if (row == m_phones->rowCount()) {
            m_phones->insertRow(row);
        }
        setCell(m_phones, row, PhoneNumber, p.number);
        m_phones->item(row, PhoneNumber)->setData(Qt::UserRole, network);
        setCell(m_phones, row, PhoneState, p.state.isEmpty() ? "idle" : p.state);
        setCell(m_phones, row, PhoneChannel, p.channel ? QString::number(p.channel) : QString());
        setCell(m_phones, row, PhoneNetwork, networkName(network));
        setCell(m_phones, row, PhoneRegistered, p.registered ? "yes" : "no");
        setCell(m_phones, row, PhoneLastHeard, p.lastHeard.isValid() ? p.lastHeard.toString("HH:mm:ss") : QString());
        setCell(m_phones, row, PhoneLevel, p.lastHeard.isValid() ? QString::number(p.level, 'f', 1) : QString());
        setCell(m_phones, row, PhoneQuality, p.lastHeard.isValid() ? QString::number(p.quality, 'f', 2) : QString());
        setCell(m_phones, row, PhonePower, powerName(p.power));
        setCell(m_phones, row, PhoneLastFrame, p.lastFrame);
        // what was learned stays when a report does not know it (the tester started again)
        if (!p.password.isEmpty()) {
            setCell(m_phones, row, PhonePassword, p.password);
        }
        if (!p.smsCentre.isEmpty()) {
            setCell(m_phones, row, PhoneSmsCentre, p.smsCentre);
        }
        if (!p.dialled.isEmpty()) {
            setCell(m_phones, row, PhoneDialled, p.dialled);
        }
        QStringList kept;
        if (p.held > 0) {
            kept.append(QString("%1 SMS").arg(p.held));
        }
        if (p.pendingMwi) {
            kept.append("indicator");
        }
        setCell(m_phones, row, PhoneKept, kept.join(", "));
    }
}

// The carriers of all networks, in the order of the networks.
void CellularGUI::showCarriers()
{
    int row = 0;

    for (const CellularNetworkSettings& n : m_settings.m_networks)
    {
        const auto it = m_status.constFind(n.m_id);
        if (it == m_status.constEnd()) {
            continue;
        }
        for (const Cellular::MsgReportStatus::Carrier& c : it->carriers)
        {
            if (row >= m_carriers->rowCount()) {
                m_carriers->insertRow(row);
            }
            const bool on = !c.state.startsWith("off");
            setCell(m_carriers, row, CarNetwork, n.m_name);
            setCell(m_carriers, row, CarChannel, QString::number(c.channel));
            setCell(m_carriers, row, CarType, c.type);
            setCell(m_carriers, row, CarDownlink, QString::number(c.downlink / 1e6, 'f', 4));
            setCell(m_carriers, row, CarUplink, QString::number(c.uplink / 1e6, 'f', 4));
            setCell(m_carriers, row, CarState, c.state);
            setCell(m_carriers, row, CarRxLevel, on ? QString::number(c.rxLevel, 'f', 1) : QString());
            setCell(m_carriers, row, CarTxFill, on ? QString::number(c.txFillMs, 'f', 0) : QString());
            setCell(m_carriers, row, CarUnderruns, on ? QString::number(c.underruns) : QString());
            setCell(m_carriers, row, CarPhone, c.phone);
            for (int col = 0; col < CarColCount; col++) {
                m_carriers->item(row, col)->setForeground(on ? QBrush() : QBrush(Qt::red));
            }
            row++;
        }
    }
    m_carriers->setRowCount(row);
}

void CellularGUI::showSummary()
{
    if (m_cellular->getState() != Feature::StRunning)
    {
        m_summary->setText("Stopped");
        return;
    }
    int onAir = 0;
    int registered = 0;
    int calls = 0;
    for (const CellularNetworkSettings& n : m_settings.m_networks)
    {
        const auto it = m_status.constFind(n.m_id);
        if (n.m_enabled && it != m_status.constEnd() && it->onAir)
        {
            onAir++;
            registered += it->registered;
            calls += it->calls;
        }
    }
    m_summary->setText(QString("%1 on the air, %2 registered, %3 calls").arg(onAir).arg(registered).arg(calls));
}

// What the phone of the Test tab does now, and the phones its network heard to choose from.
void CellularGUI::showTestPhone()
{
    const int network = m_testNetwork->currentData().toInt();
    const QString number = m_testPhone->currentText().trimmed();
    const auto it = m_status.constFind(network);
    QStringList heard;
    QString state = "Not heard by this network";

    if (it != m_status.constEnd())
    {
        for (const Cellular::MsgReportStatus::Phone& p : it->phones)
        {
            heard.append(p.number);
            if (p.number == number)
            {
                state = QString("%1, %2%3").arg(p.state.isEmpty() ? "idle" : p.state)
                    .arg(p.registered ? "registered" : "not registered")
                    .arg(p.lastHeard.isValid() ? QString(", heard %1 on channel %2").arg(p.lastHeard.toString("HH:mm:ss")).arg(p.channel) : QString());
            }
        }
    }
    m_testState->setText(state);
    m_testState->setVisible(!number.isEmpty());

    // the list changes only when other phones were heard; the text being typed stays
    QStringList items;
    for (int i = 0; i < m_testPhone->count(); i++) {
        items.append(m_testPhone->itemText(i));
    }
    if (items != heard)
    {
        m_testPhone->blockSignals(true);
        const QString text = m_testPhone->currentText();
        m_testPhone->clear();
        m_testPhone->addItems(heard);
        m_testPhone->setCurrentText(text);
        m_testPhone->blockSignals(false);
    }
}

bool CellularGUI::logShown(const LogLine& line) const
{
    const int network = m_logNetwork->currentData().toInt();

    if (network >= 0 && line.network != network) {
        return false;
    }
    return m_logFrames->isChecked() || !line.text.startsWith("ch ");
}

static QString logText(const QString& time, const QString& network, const QString& text)
{
    return network.isEmpty() ? time + " " + text : time + " " + network + ": " + text;
}

void CellularGUI::addLog(int network, const QString& text)
{
    LogLine line{QDateTime::currentDateTime().toString("HH:mm:ss"), network, text};

    m_logLines.push_back(line);
    if (m_logLines.size() > static_cast<size_t>(m_maxLogLines)) {
        m_logLines.pop_front();
    }
    if (!logShown(line)) {
        return;
    }
    // the view stays where the user scrolled to; at the end it follows
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum();
    const int value = bar->value();
    const bool all = m_logNetwork->currentData().toInt() < 0;

    m_log->appendPlainText(logText(line.time, all && network ? networkName(network) : QString(), line.text));
    bar->setValue(atEnd ? bar->maximum() : value);
}

void CellularGUI::renderLog()
{
    const bool all = m_logNetwork->currentData().toInt() < 0;
    QStringList lines;

    for (const LogLine& line : m_logLines)
    {
        if (logShown(line)) {
            lines.append(logText(line.time, all && line.network ? networkName(line.network) : QString(), line.text));
        }
    }
    m_log->setMaximumBlockCount(0);
    m_log->setPlainText(lines.join('\n'));
    m_log->setMaximumBlockCount(m_maxLogLines);
    m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
}

// ---- the plumbing of a feature GUI ----

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
        addLog(report.getNetwork(), report.getText());
        return true;
    }
    else if (Cellular::MsgReportStatus::match(message))
    {
        showStatus(static_cast<const Cellular::MsgReportStatus&>(message));
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
    // the tester may stop by itself
    const bool running = m_cellular->getState() == Feature::StRunning;
    if (m_startStop->isChecked() != running)
    {
        m_doApplySettings = false;
        m_startStop->setChecked(running);
        m_doApplySettings = true;
        showSummary();
    }
}
