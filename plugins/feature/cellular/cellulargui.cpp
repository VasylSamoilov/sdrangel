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
#include <cmath>

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "audio/audiodeviceinfo.h"
#include "device/deviceapi.h"
#include "device/deviceset.h"
#include "dsp/devicesamplesink.h"
#include "dsp/devicesamplesource.h"
#include "feature/featureuiset.h"
#include "audio/audiodevicemanager.h"
#include "dsp/dspengine.h"
#include "gui/audioselectdialog.h"
#include "gui/buttonswitch.h"
#include "gui/crightclickenabler.h"
#include "gui/dialogpositioner.h"
#include "gui/flowlayout.h"
#include "gui/rollupcontents.h"
#include "maincore.h"

#include "nmtplan.h"

#include "cellularbook.h"
#include "cellularchannelgrid.h"
#include "cellularcolumns.h"
#include "cellulargui.h"
#include "cellularmobiles.h"

const int CellularGUI::m_maxLogLines;

namespace
{

enum MobileColumn
{
    MobileName,         // two lines: the state dot, name and number; the chips
    MobileState,        // two lines: what it does now; where and how it was heard
    MobileSpeech,       // two lines: the call meters, from the phone and to the phone
    MobileColCount
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

// right click on a cell: Copy its text, as SDRangel's tables do
void copyMenu(QTableWidget *table)
{
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(table, &QTableWidget::customContextMenuRequested, table, [table](const QPoint& pos) {
        const QTableWidgetItem *item = table->itemAt(pos);
        if (!item || item->text().isEmpty()) {
            return;
        }
        QMenu *menu = new QMenu(table);
        QObject::connect(menu, &QMenu::aboutToHide, menu, &QMenu::deleteLater);
        const QString text = item->text();
        menu->addAction("Copy", menu, [text]() { QGuiApplication::clipboard()->setText(text); });
        menu->popup(table->viewport()->mapToGlobal(pos));
    });
}

QString logFileTip(const QString& filename)
{
    return QString("Set the .csv file to log into (appended to): %1").arg(filename);
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

} // namespace

// ---- the settings of one network ----

CellularNetworkDialog::CellularNetworkDialog(const CellularNetworkSettings& settings, const QMap<int, QString>& others, QWidget *parent) :
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
    m_msPower = new QComboBox(page);
    m_msPower->addItem("3 high (15 W, handheld 7 W)", 3);
    m_msPower->addItem("2 medium (10 dB less)", 2);
    m_msPower->addItem("1 low (20 dB less)", 1);
    m_msPower->addItem("0 low (20 dB less)", 0);
    m_msPower->setToolTip("The highest power level the phones may send with, in the power bits of every frame "
        "(NMT Doc 450-3 5.7.1; osmocom-analog's default is 1). A phone that receives the network strongly in a "
        "call goes lower by itself; the Test tab can order another level in a call");
    form->addRow("Phone power (max)", m_msPower);
    // the channels: the plan of the country as a grid, the same as text below it
    m_grid = new CellularChannelGrid(page);
    m_grid->setToolTip("The channels of the country plan by frequency (each row starts at the downlink frequency on its left). "
        "Click: free, CC, TC, CC/TC; right click: any type and the supervisory tone of a traffic channel. "
        "Grey: used by another network on the same devices; dark: outside the span of the devices");
    m_grid->setMinimumHeight(170);
    m_grid->setOthers(others);
    form->addRow(new QLabel("Carriers", page));
    form->addRow(m_grid);
    QLabel *legend = new QLabel(page);
    legend->setTextFormat(Qt::RichText);
    legend->setWordWrap(true);
    legend->setText("<span style=\"background-color:#c07a1a;\">&nbsp;CC&nbsp;</span> "
        "<span style=\"background-color:#2f6db3;\">&nbsp;TC&nbsp;</span> "
        "<span style=\"background-color:#7a4fb3;\">&nbsp;CC/TC&nbsp;</span> "
        "<span style=\"background-color:#4a4a4a;\">&nbsp;other network&nbsp;</span> "
        "<span style=\"background-color:#1c1c1c; color:#777777;\">&nbsp;outside the devices&nbsp;</span>");
    form->addRow(legend);
    m_bandwidth = new QLabel(page);
    m_bandwidth->setWordWrap(true);
    m_bandwidth->setTextFormat(Qt::RichText);
    form->addRow(m_bandwidth);
    m_carriers = new QLineEdit(page);
    m_carriers->setToolTip("The same channels as text: channel:type[:supervisory], comma separated. Types: cc (calling), "
        "tc (traffic), cctc (both), actc, cca, ccb (NMT-900), test. Supervisory tone 1..4 (0 none) on every traffic channel, "
        "or on none for 1..4 in turn. Example: 1:cctc:1, 5:tc:2, 6:tc:3");
    form->addRow(m_carriers);
    m_carriersError = new QLabel(page);
    m_carriersError->setWordWrap(true);
    m_carriersError->setStyleSheet("color: #ff6060;");
    m_carriersError->hide();
    form->addRow(m_carriersError);
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
    m_fallback->setToolTip("What answers a call to a number that is no phone (a phone number is paged)");
    form->addRow("Other numbers", m_fallback);
    // the echo
    m_echoBox = new QWidget(page);
    QFormLayout *echo = new QFormLayout(m_echoBox);
    echo->setContentsMargins(12, 0, 0, 0);
    m_echoDelay = new QDoubleSpinBox(m_echoBox);
    m_echoDelay->setRange(0.2, 3.0);
    m_echoDelay->setSingleStep(0.1);
    m_echoDelay->setDecimals(1);
    m_echoDelay->setSuffix(" s");
    m_echoDelay->setToolTip("How late the caller hears itself: short to hear the radio path, long to hear oneself clearly");
    echo->addRow("Delay", m_echoDelay);
    m_echoLevel = new QSpinBox(m_echoBox);
    m_echoLevel->setRange(-30, 0);
    m_echoLevel->setSuffix(" dB");
    m_echoLevel->setToolTip("The level of the echo, never louder than the caller spoke");
    echo->addRow("Level", m_echoLevel);
    m_echoAnnounce = new QCheckBox("A tone when it answers", m_echoBox);
    m_echoAnnounce->setToolTip("1 kHz for 200 ms when the echo answers, so the caller knows");
    echo->addRow(m_echoAnnounce);
    form->addRow(m_echoBox);
    // the music
    m_musicBox = new QWidget(page);
    QFormLayout *music = new QFormLayout(m_musicBox);
    music->setContentsMargins(12, 0, 0, 0);
    m_musicSource = new QComboBox(m_musicBox);
    m_musicSource->addItems({"Built-in melody", "Sound file (WAV)", "Audio input"});
    music->addRow("Source", m_musicSource);
    QWidget *fileRow = new QWidget(m_musicBox);
    QHBoxLayout *fileLayout = new QHBoxLayout(fileRow);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    m_musicFile = new QLineEdit(fileRow);
    m_musicFile->setToolTip("A 16 bit PCM WAV file, any rate, mono or stereo");
    QToolButton *browse = new QToolButton(fileRow);
    browse->setText("...");
    browse->setToolTip("Pick the sound file");
    fileLayout->addWidget(m_musicFile, 1);
    fileLayout->addWidget(browse);
    music->addRow("File", fileRow);
    m_musicInput = new QComboBox(m_musicBox);
    m_musicInput->addItem("Default", QString());
    for (const AudioDeviceInfo& d : AudioDeviceInfo::availableInputDevices()) {
        m_musicInput->addItem(d.deviceName(), d.deviceName());
    }
    m_musicInput->setToolTip("The SDRangel audio input that plays to the caller");
    music->addRow("Input", m_musicInput);
    m_musicLevel = new QSpinBox(m_musicBox);
    m_musicLevel->setRange(-30, 0);
    m_musicLevel->setSuffix(" dB");
    m_musicLevel->setToolTip("The level of the music, never louder than the source (full scale at the speech reference)");
    music->addRow("Level", m_musicLevel);
    m_musicLoop = new QComboBox(m_musicBox);
    m_musicLoop->addItems({"Looped", "Once, then hang up"});
    music->addRow("Play", m_musicLoop);
    form->addRow(m_musicBox);
    m_callLimit = new QSpinBox(page);
    m_callLimit->setRange(0, 3600);
    m_callLimit->setSuffix(" s");
    m_callLimit->setSpecialValueText("no limit");
    m_callLimit->setToolTip("The longest call of this network (between phones, to the echo or the music) after the "
        "answer; it is ended then on both sides, so no call stays on. Off by default");
    form->addRow("Longest call", m_callLimit);
    auto farEndShown = [this, fileRow]() {
        const bool isMusic = m_fallback->currentIndex() == 1;
        m_echoBox->setVisible(!isMusic);
        m_musicBox->setVisible(isMusic);
        const int source = m_musicSource->currentIndex();
        fileRow->setEnabled(source == CellularFarEnd::MusicFile);
        m_musicInput->setEnabled(source == CellularFarEnd::MusicInput);
        m_musicLoop->setEnabled(source != CellularFarEnd::MusicInput);     // a live input plays on
    };
    connect(m_fallback, QOverload<int>::of(&QComboBox::currentIndexChanged), this, farEndShown);
    connect(m_musicSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, farEndShown);
    connect(browse, &QToolButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, "Music for the calls", m_musicFile->text(), "WAV files (*.wav)");
        if (!path.isEmpty()) {
            m_musicFile->setText(path);
        }
    });
    m_rxDeviceSet = new QComboBox(page);
    m_rxDeviceSet->setToolTip("The receiving device set with the Cellular Rx channel that receives the phones; networks may share it");
    form->addRow("Receiver device set", m_rxDeviceSet);
    m_txDeviceSet = new QComboBox(page);
    m_txDeviceSet->setToolTip("The transmitting device set with the Cellular Tx channel that transmits the base station; networks may share it");
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
    m_msPower->setCurrentIndex(std::max(0, m_msPower->findData(settings.m_msPower)));
    m_carriers->setText(settings.m_carriers);
    m_compandor->setChecked(settings.m_compandor);
    m_callerId->setChecked(settings.m_sendCallerId);
    m_clock->setChecked(settings.m_sendClock);
    m_smsc->setText(settings.m_smscNumber);
    const CellularFarEnd& fe = settings.m_farEnd;
    m_echoDelay->setValue(fe.m_echoDelayMs / 1000.0);
    m_echoLevel->setValue(fe.m_echoLevelDb);
    m_echoAnnounce->setChecked(fe.m_echoAnnounce);
    m_musicSource->setCurrentIndex(std::max(0, std::min(2, fe.m_musicSource)));
    m_musicFile->setText(fe.m_musicFile);
    m_musicInput->setCurrentIndex(std::max(0, m_musicInput->findData(fe.m_musicInput)));
    m_musicLevel->setValue(fe.m_musicLevelDb);
    m_musicLoop->setCurrentIndex(fe.m_musicLoop ? 0 : 1);
    m_callLimit->setValue(fe.m_callLimitS);
    // setting the index shows the rows of the far end (echo or music)
    m_fallback->setCurrentIndex(-1);
    m_fallback->setCurrentIndex(settings.m_fallback == CellularNetworkSettings::FallbackMusic ? 1 : 0);
    fillDeviceSets(m_rxDeviceSet, false, settings.m_rxDeviceSetIndex);
    fillDeviceSets(m_txDeviceSet, true, settings.m_txDeviceSetIndex);
    onPlanChanged();
    onTextChanged();

    // room for the grid, but not more than the screen
    QSize size(400, 700);
    if (const QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen()) {
        size = size.boundedTo(screen->availableGeometry().size());
    }
    resize(size);

    connect(m_system, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        fillCountries(m_country->currentData().toString());
        onPlanChanged();
    });
    connect(m_country, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularNetworkDialog::onPlanChanged);
    connect(m_rxDeviceSet, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularNetworkDialog::onPlanChanged);
    connect(m_txDeviceSet, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CellularNetworkDialog::onPlanChanged);
    connect(m_grid, &CellularChannelGrid::changed, this, &CellularNetworkDialog::onGridChanged);
    connect(m_carriers, &QLineEdit::editingFinished, this, &CellularNetworkDialog::onTextChanged);
}

QString CellularNetworkDialog::carriersText(const std::vector<modemnmt::NmtCarrierConfig>& carriers)
{
    using T = modemnmt::ChannelType;
    QStringList items;

    for (const modemnmt::NmtCarrierConfig& c : carriers)
    {
        QString type;
        switch (c.type)
        {
        case T::Cc: type = "cc"; break;
        case T::Cca: type = "cca"; break;
        case T::Ccb: type = "ccb"; break;
        case T::Tc: type = "tc"; break;
        case T::AcTc: type = "actc"; break;
        case T::CcTc: type = "cctc"; break;
        case T::Test: type = "test"; break;
        }
        // tone: 1..4 given, -1 none (written 0), 0 auto (not written)
        items.append(c.supervisory != 0 ? QString("%1:%2:%3").arg(c.channel).arg(type).arg(std::max(c.supervisory, 0))
            : QString("%1:%2").arg(c.channel).arg(type));
    }
    return items.join(", ");
}

// the plan of the country, the spans of the devices
void CellularNetworkDialog::onPlanChanged()
{
    double downLow = 0.0, downHigh = 0.0, upLow = 0.0, upHigh = 0.0, centre = 0.0;
    int rate = 0;

    m_grid->setPlan(m_system->currentIndex() == 1 ? modemnmt::System::Nmt900 : modemnmt::System::Nmt450,
        m_country->currentData().toString());
    if (!deviceSpan(m_txDeviceSet->currentData().toInt(), true, downLow, downHigh, centre, rate)) {
        downLow = downHigh = 0.0;
    }
    if (!deviceSpan(m_rxDeviceSet->currentData().toInt(), false, upLow, upHigh, centre, rate)) {
        upLow = upHigh = 0.0;
    }
    m_grid->setSpans(downLow, downHigh, upLow, upHigh);
    showBandwidth();
}

void CellularNetworkDialog::onGridChanged()
{
    m_carriers->setText(carriersText(m_grid->carriers()));
    onTextChanged();
}

// the text is checked as the network will take it; when it can be read the grid follows it
void CellularNetworkDialog::onTextChanged()
{
    std::vector<modemnmt::NmtCarrierConfig> carriers;
    QString error;
    // the tones written in the text; the parser gives the others 1..4 in turn, which the grid
    // does not take as given (they stay "auto" when the grid writes the text again)
    QMap<int, int> givenTones;
    for (const QString& item : m_carriers->text().split(',', Qt::SkipEmptyParts))
    {
        const QStringList parts = item.trimmed().split(':');
        if (parts.size() == 3)
        {
            const int tone = parts[2].trimmed().toInt();
            givenTones[parts[0].trimmed().toInt()] = tone > 0 ? tone : -1;     // 0 in the text: none
        }
    }
    const auto keepGiven = [&givenTones](std::vector<modemnmt::NmtCarrierConfig>& list) {
        for (modemnmt::NmtCarrierConfig& c : list) {
            c.supervisory = givenTones.value(c.channel, 0);
        }
    };

    if (CellularNetworkSettings::parseCarriers(m_carriers->text(), carriers, error))
    {
        m_carriersError->hide();
        keepGiven(carriers);
        m_grid->setCarriers(carriers);
    }
    else
    {
        m_carriersError->setText(error);
        m_carriersError->show();
        // the items that can be read are still shown
        std::vector<modemnmt::NmtCarrierConfig> partial;
        for (const QString& item : m_carriers->text().split(',', Qt::SkipEmptyParts))
        {
            std::vector<modemnmt::NmtCarrierConfig> one;
            QString itemError;
            if (CellularNetworkSettings::parseCarriers(item, one, itemError)) {
                partial.insert(partial.end(), one.begin(), one.end());
            }
        }
        keepGiven(partial);
        m_grid->setCarriers(partial);
    }
    showBandwidth();
}

void CellularNetworkDialog::fillDeviceSets(QComboBox *combo, bool tx, int chosen)
{
    const std::vector<DeviceSet*>& sets = MainCore::instance()->getDeviceSets();
    bool found = false;

    combo->clear();
    for (int i = 0; i < static_cast<int>(sets.size()); i++)
    {
        QString error;
        if (!sets[i] || !sets[i]->m_deviceAPI || !Cellular::findRadio(i, tx, error)) {
            continue;
        }
        DeviceAPI *api = sets[i]->m_deviceAPI;
        const double centre = tx ? (api->getSampleSink() ? api->getSampleSink()->getCenterFrequency() : 0.0)
            : (api->getSampleSource() ? api->getSampleSource()->getCenterFrequency() : 0.0);
        combo->addItem(QString("%1%2  %3  %4 MHz").arg(tx ? "T" : "R").arg(i).arg(api->getHardwareId())
            .arg(centre / 1e6, 0, 'f', 3), i);
        found = found || i == chosen;
    }
    // a device set that is gone or has no channel stays as it is: the network says why it cannot start
    if (!found)
    {
        QString error;
        Cellular::findRadio(chosen, tx, error);
        combo->addItem(QString("%1: %2").arg(chosen).arg(error), chosen);
    }
    combo->setCurrentIndex(combo->findData(chosen));
}

bool CellularNetworkDialog::deviceSpan(int index, bool tx, double& low, double& high, double& centre, int& rate)
{
    const std::vector<DeviceSet*>& sets = MainCore::instance()->getDeviceSets();

    if (index < 0 || index >= static_cast<int>(sets.size()) || !sets[index] || !sets[index]->m_deviceAPI) {
        return false;
    }
    DeviceAPI *api = sets[index]->m_deviceAPI;
    if (tx)
    {
        if (!api->getSampleSink()) {
            return false;
        }
        centre = api->getSampleSink()->getCenterFrequency();
        rate = api->getSampleSink()->getSampleRate();
    }
    else
    {
        if (!api->getSampleSource()) {
            return false;
        }
        centre = api->getSampleSource()->getCenterFrequency();
        rate = api->getSampleSource()->getSampleRate();
    }
    // the carriers go in 75 % of the band of the device
    low = centre - 0.375 * rate;
    high = centre + 0.375 * rate;
    return rate > 0;
}

// The span of the chosen channels, the sample rate it needs, and whether the devices of the network
// carry it.
void CellularNetworkDialog::showBandwidth()
{
    const std::vector<modemnmt::NmtCarrierConfig> carriers = m_grid->carriers();
    const modemnmt::NmtCountry *country = m_grid->country();

    if (carriers.empty() || !country)
    {
        m_bandwidth->setText("No carriers.");
        return;
    }
    double downMin = 1e12, downMax = 0.0, upMin = 1e12, upMax = 0.0;
    for (const modemnmt::NmtCarrierConfig& c : carriers)
    {
        modemnmt::NmtChannelFrequency f;
        if (modemnmt::nmtChannelFrequency(*country, c.channel, f))
        {
            downMin = std::min(downMin, f.downlink);
            downMax = std::max(downMax, f.downlink);
            upMin = std::min(upMin, f.uplink);
            upMax = std::max(upMax, f.uplink);
        }
    }
    // a channel is 25 kHz wide; the rate is an even multiple of 48 kHz
    const double span = std::max(downMax - downMin, upMax - upMin) + 25e3;
    const int needed = static_cast<int>(std::ceil(span / 0.75 / 96e3)) * 96000;
    QString text = QString("Down %1 - %2 MHz, up %3 - %4 MHz: span %5 MHz, needs at least %6 kS/s.")
        .arg(downMin / 1e6, 0, 'f', 4).arg(downMax / 1e6, 0, 'f', 4)
        .arg(upMin / 1e6, 0, 'f', 4).arg(upMax / 1e6, 0, 'f', 4)
        .arg(span / 1e6, 0, 'f', 3).arg(needed / 1000);

    double low, high, centre;
    int rate;
    const auto describe = [&](const char *name, int index, bool tx, double minHz, double maxHz) {
        if (!deviceSpan(index, tx, low, high, centre, rate)) {
            return QString(" %1 %2: not there.").arg(name).arg(index);
        }
        const bool fits = minHz - 12.5e3 >= low && maxHz + 12.5e3 <= high;
        QString s = QString(" %1 %2 at %3 MHz, %4 kS/s: %5").arg(name).arg(index).arg(centre / 1e6, 0, 'f', 4).arg(rate / 1000)
            .arg(fits ? "<span style=\"color:#5fd07f;\">fits</span>" : "<span style=\"color:#ff6060;\">does not fit</span>");
        if (!fits) {
            s += QString(" (centre %1 MHz would take it)").arg((minHz + maxHz) / 2e6, 0, 'f', 4);
        }
        return s + ".";
    };
    text += describe("Transmitter", m_txDeviceSet->currentData().toInt(), true, downMin, downMax);
    text += describe("receiver", m_rxDeviceSet->currentData().toInt(), false, upMin, upMax);
    m_bandwidth->setText(text);
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
    s.m_msPower = m_msPower->currentData().toInt();
    s.m_carriers = m_carriers->text();
    s.m_compandor = m_compandor->isChecked();
    s.m_sendCallerId = m_callerId->isChecked();
    s.m_sendClock = m_clock->isChecked();
    s.m_smscNumber = m_smsc->text();
    s.m_fallback = m_fallback->currentIndex() == 1 ? CellularNetworkSettings::FallbackMusic : CellularNetworkSettings::FallbackEcho;
    CellularFarEnd& fe = s.m_farEnd;
    fe.m_echoDelayMs = static_cast<int>(std::lround(m_echoDelay->value() * 1000.0));
    fe.m_echoLevelDb = m_echoLevel->value();
    fe.m_echoAnnounce = m_echoAnnounce->isChecked();
    fe.m_musicSource = m_musicSource->currentIndex();
    fe.m_musicFile = m_musicFile->text();
    fe.m_musicInput = m_musicInput->currentData().toString();
    fe.m_musicLevelDb = m_musicLevel->value();
    fe.m_musicLoop = m_musicLoop->currentIndex() == 0;
    fe.m_callLimitS = m_callLimit->value();
    s.m_rxDeviceSetIndex = m_rxDeviceSet->currentData().toInt();
    s.m_txDeviceSetIndex = m_txDeviceSet->currentData().toInt();
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
    // the feature sends the records of its book
    m_cellular->getInputMessageQueue()->push(Cellular::MsgBookReplace::create(QByteArray()));
    sizeToContents();
    m_resizer.enableChildMouseTracking();
}

CellularGUI::~CellularGUI()
{
    disconnect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));
    m_cellular->setMessageQueueToGUI(nullptr);
}

// The contents need more height when the window is narrower (the summary wraps, so do the forms
// of the tabs) and on some tabs than on others, but SDRangel takes the minimum size of a feature
// window from its contents only when a section rolls up or down: the bottom of the window, with
// its size grip, was cut off.
void CellularGUI::fitHeight()
{
    m_tabs->updateGeometry();       // the height for the width of the tab shown is cached
    getRollupContents()->arrangeRollups();
    sizeToContents();
    if (!isMaximized() && (height() < minimumHeight())) {
        resize(width(), minimumHeight());
    }
}

void CellularGUI::resizeEvent(QResizeEvent *event)
{
    FeatureGUI::resizeEvent(event);
    fitHeight();
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
    m_tabs->addTab(buildPhones(), "Mobiles");
    m_tabs->addTab(buildConsole(), "Console");
    m_tabs->addTab(buildTest(), "Test");
    m_tabs->addTab(buildLog(), "Log");
    m_tabs->addTab(buildNetworks(), "Networks");
    m_tabs->addTab(buildCarriers(), "Carriers");
    layout->addWidget(m_tabs, 1);
    m_speechLine = new CellularSpeechLine(main);
    layout->addWidget(m_speechLine);
    connect(m_speechLine, &CellularSpeechLine::listen, this, [this](const QString& number, int mode) {
        for (int row = 0; row < m_phones->rowCount(); row++)
        {
            const QTableWidgetItem *item = m_phones->item(row, MobileName);
            if (item && item->data(CellularMobileRole::Number).toString() == number) {
                requestAction(item->data(CellularMobileRole::Network).toInt(), number, Cellular::MsgAction::Listen, QString(), mode);
            }
        }
    });
    main->setMinimumWidth(0);
    // the tabs take the height of the window
    main->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(m_startStop, &ButtonSwitch::toggled, this, &CellularGUI::onStartStop);
    connect(options, &QToolButton::clicked, this, &CellularGUI::openSettings);
    connect(m_tabs, &QTabWidget::currentChanged, this, &CellularGUI::fitHeight);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this]() { showSpeech(); });

    // the followed call in a section of its own: meters drawn large, the phone's radio line and
    // the log lines of that phone and its channel
    QWidget *call = new QWidget(rollupContents);
    call->setObjectName("call");
    call->setWindowTitle("Call");
    QVBoxLayout *callLayout = new QVBoxLayout(call);
    callLayout->setContentsMargins(3, 3, 3, 3);
    callLayout->setSpacing(2);
    QHBoxLayout *callTop = new QHBoxLayout();
    m_callPanel = new CellularCallPanel(call);
    callTop->addWidget(m_callPanel, 1);
    m_callFollow = new ButtonSwitch(call);
    QIcon follow;
    follow.addFile(":/play.png", QSize(16, 16), QIcon::Normal, QIcon::Off);
    follow.addFile(":/pause.png", QSize(16, 16), QIcon::Normal, QIcon::On);
    m_callFollow->setIcon(follow);
    m_callFollow->setChecked(true);
    m_callFollow->setToolTip("Following the call (on): its meters and log lines come in. Off: they stop where they are");
    callTop->addWidget(m_callFollow, 0, Qt::AlignTop);
    callLayout->addLayout(callTop);
    connect(m_callFollow, &QToolButton::toggled, this, [this](bool on) {
        if (on) {
            showSpeech();
        }
    });
    m_callLog = new QPlainTextEdit(call);
    m_callLog->setReadOnly(true);
    m_callLog->setMaximumBlockCount(500);
    m_callLog->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_callLog->setFixedHeight(m_callLog->fontMetrics().lineSpacing() * 8 + 10);
    m_callLog->setToolTip("The log lines of the phone of the followed call and of its channel (frames, levels, orders)");
    callLayout->addWidget(m_callLog);
    call->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

// A log line of the followed call: it names the phone (whole, or its six digits after X=) or its channel.
bool CellularGUI::callLine(int network, const QString& text) const
{
    if (m_callNumber.isEmpty() || (m_callNetwork && network != m_callNetwork)) {
        return false;
    }
    return text.contains(m_callNumber) || text.contains("X=" + m_callNumber.mid(1))
        || (m_callChannel > 0 && text.startsWith(QString("ch %1 ").arg(m_callChannel)));
}

QWidget *CellularGUI::buildPhones()
{
    QWidget *page = new QWidget(m_tabs);
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    // one row per phone: the book and the phones the networks hear; two lines each (the delegate)
    m_phones = makeTable(page, {"Mobile", "State", "Speech"});
    // the state takes the room that is left (its text grows with the call: destination and
    // signalling state); the phone keeps the width of its name and chips
    m_phones->horizontalHeader()->setSectionResizeMode(MobileName, QHeaderView::ResizeToContents);
    m_phones->horizontalHeader()->setSectionResizeMode(MobileState, QHeaderView::Stretch);
    m_phones->horizontalHeader()->setSectionResizeMode(MobileSpeech, QHeaderView::ResizeToContents);
    m_phones->horizontalHeader()->setStretchLastSection(false);
    m_phones->setItemDelegateForColumn(MobileName, new CellularMobileDelegate(m_phones));
    CellularSpeechDelegate *speech = new CellularSpeechDelegate(m_phones, CellularMobileRole::Number,
        [this](const QString& number) { return speechOf(number); });
    // the speaker button of a call: listen in on the network the phone is heard on
    speech->setSpeaker([this](const QModelIndex& index, const QPoint& globalPos, int listen) {
        const QTableWidgetItem *item = m_phones->item(index.row(), MobileName);
        if (!item) {
            return;
        }
        const int mode = CellularSpeechPaint::speakerMenu(m_phones, globalPos, listen);
        if (mode >= 0) {
            requestAction(item->data(CellularMobileRole::Network).toInt(), item->data(CellularMobileRole::Number).toString(),
                Cellular::MsgAction::Listen, QString(), mode);
        }
    });
    m_phones->setItemDelegateForColumn(MobileSpeech, speech);
    m_phones->horizontalHeaderItem(MobileSpeech)->setToolTip(QString("The speech of the phone's call: %1 from the phone "
        "(demodulated), %2 to the phone. Each: the pauses (grey), where the speech landed from -40 to +4 dB of the "
        "reference (red line: 4.7 kHz, the largest deviation), six bands from 300 Hz to 3.4 kHz, the share at the "
        "limit. The last 2 s during the call; after it, the whole call dimmed").arg(CellularSpeechPaint::up())
        .arg(CellularSpeechPaint::down()));
    m_phones->setWordWrap(true);
    m_phones->setToolTip("The phones the tester knows (its book) and the ones the networks hear. "
        "Double click: the record of the phone; right click: actions on it");
    m_phones->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_phones);
    QToolButton *add = iconButton(nullptr, ":/plus.png", "Add a phone to the book by its number");
    QToolButton *edit = iconButton(nullptr, ":/edit.png", "The record of the selected phone");
    QToolButton *forget = iconButton(nullptr, ":/bin.png", "Forget the selected phone (its record goes; a phone the "
        "networks hear comes back)");
    layout->addWidget(flowRow(page, {add, edit, forget}));

    auto selected = [this](modemnmt::System& system, QString& number) {
        const QModelIndexList rows = m_phones->selectionModel()->selectedRows();
        const QTableWidgetItem *item = rows.isEmpty() ? nullptr : m_phones->item(rows.first().row(), MobileName);
        if (!item) {
            return false;
        }
        system = static_cast<modemnmt::System>(item->data(CellularMobileRole::System).toInt());
        number = item->data(CellularMobileRole::Number).toString();
        return true;
    };
    connect(m_phones, &QTableWidget::customContextMenuRequested, this, &CellularGUI::phoneMenu);
    connect(m_phones, &QTableWidget::itemSelectionChanged, this, &CellularGUI::showSpeech);
    connect(m_phones, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (const QTableWidgetItem *item = m_phones->item(row, MobileName)) {
            editMobile(static_cast<modemnmt::System>(item->data(CellularMobileRole::System).toInt()),
                item->data(CellularMobileRole::Number).toString());
        }
    });
    connect(add, &QToolButton::clicked, this, &CellularGUI::addMobile);
    connect(edit, &QToolButton::clicked, this, [this, selected]() {
        modemnmt::System system;
        QString number;
        if (selected(system, number)) {
            editMobile(system, number);
        }
    });
    connect(forget, &QToolButton::clicked, this, [this, selected]() {
        modemnmt::System system;
        QString number;
        if (selected(system, number)) {
            forgetMobile(system, number);
        }
    });
    // the chips wrap to the width of the column
    connect(m_phones->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int section) {
        if (section == MobileName) {
            m_phones->resizeRowsToContents();
        }
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
    QPushButton *release = new QPushButton("Release");
    m_testRelease = release;
    release->setToolTip("Ends the call of the phone from the network side: the test call, or any other call it has");
    form->addRow("Call", flowRow(page, {call, release}));

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
    m_testPower = power;
    power->setToolTip("Orders the power level to the phone in its call; the log tells whether it took it");
    form->addRow("Power", flowRow(page, {m_powerLevel, power}));

    m_moveChannel = new QComboBox();
    m_moveChannel->setToolTip("The voice (traffic) channel to move the call to: one of the network's, or any free one");
    QPushButton *move = new QPushButton("Move");
    m_testMove = move;
    move->setToolTip("Moves the call of the phone to that voice channel (switching a call in progress, NMT Doc 450-1 4.4.1.4)");
    form->addRow("Voice channel", flowRow(page, {m_moveChannel, move}));

    auto act = [this](int action, const QString& text, int value) {
        requestAction(m_testNetwork->currentData().toInt(), m_testPhone->currentText().trimmed(), action, text, value);
    };
    connect(call, &QPushButton::clicked, this, [act]() { act(Cellular::MsgAction::Call, QString(), 0); });
    connect(release, &QPushButton::clicked, this, [act]() { act(Cellular::MsgAction::Release, QString(), 0); });
    connect(sms, &QPushButton::clicked, this, [this, act]() { act(Cellular::MsgAction::Sms, m_smsText->text(), 0); });
    connect(keep, &QPushButton::clicked, this, [this, act]() { act(Cellular::MsgAction::Hold, m_smsText->text(), 0); });
    connect(mwi, &QPushButton::clicked, this, [this, act]() {
        act(Cellular::MsgAction::Mwi, QString(), m_mwiKind->currentData().toInt());
    });
    connect(power, &QPushButton::clicked, this, [this, act]() {
        act(Cellular::MsgAction::Power, QString(), m_powerLevel->currentData().toInt());
    });
    connect(move, &QPushButton::clicked, this, [this, act]() {
        act(Cellular::MsgAction::Move, QString(), m_moveChannel->currentData().toInt());
    });
    connect(m_testNetwork, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        voiceChannels(m_moveChannel, m_testNetwork->currentData().toInt());
        showTestPhone();
    });
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
    // into a .csv file, as SDRangel's decoders log
    m_logEnable = new ButtonSwitch();
    m_logEnable->setIcon(QIcon(":/record_off.png"));
    m_logEnable->setToolTip("Start/stop logging into the .csv file: every line of all networks, also the frames");
    m_logFilename = iconButton(nullptr, ":/save.png", QString());
    layout->addWidget(flowRow(page, {m_logNetwork, m_logFrames, clear, m_logEnable, m_logFilename}));
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
    connect(m_logEnable, &ButtonSwitch::clicked, this, [this](bool checked) {
        m_settings.m_logEnabled = checked;
        applySettings();
    });
    connect(m_logFilename, &QToolButton::clicked, this, [this]() {
        QFileDialog dialog(this, "Select file to log to", m_settings.m_logFilename, "*.csv");
        dialog.setDefaultSuffix("csv");
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        // appended to: an existing file is not overwritten
        dialog.setOption(QFileDialog::DontConfirmOverwrite);
        if (dialog.exec() && !dialog.selectedFiles().isEmpty())
        {
            m_settings.m_logFilename = dialog.selectedFiles().first();
            m_logFilename->setToolTip(logFileTip(m_settings.m_logFilename));
            applySettings();
        }
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
    m_networks->setToolTip("The networks of the tester; double click to edit one, right click to copy a cell. "
        "Right click on the header: show or hide columns; drag a header to move a column");
    CellularColumns::setup(m_networks, &m_settings.m_networkColumnIndexes, &m_settings.m_networkColumnSizes,
        {"On", "Y1=8 test", "NMT-450", "FI - Finland", "8", "12", "10:cctc, 18:tc:1"});
    copyMenu(m_networks);
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

    m_strip = new CellularBandStrip(page);
    layout->addWidget(m_strip);
    m_carriers = makeTable(page, {"Network", "Ch", "Type", "Down MHz", "Up MHz", "State", "Rx dB", "Tx ms", "Empty", "Phone"});
    m_carriers->horizontalHeaderItem(CarTxFill)->setToolTip("Milliseconds of audio in the transmit queue");
    m_carriers->horizontalHeaderItem(CarUnderruns)->setToolTip("Times the transmit queue ran empty: the transmit lead is too small");
    m_carriers->setToolTip("The carriers of the networks on the air. Red: it cannot go on the air, with the reason. "
        "Right click to copy a cell. Right click on the header: show or hide columns; drag a header to move a column");
    CellularColumns::setup(m_carriers, &m_settings.m_carrierColumnIndexes, &m_settings.m_carrierColumnSizes,
        {"Y1=8 test", "123", "CC/TC", "463.4250", "453.4250", "roaming: confirm", "-100.0", "1000", "0", "1234567"});
    copyMenu(m_carriers);
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
    // the channels other networks take on the same receiver or transmitter
    QMap<int, QString> others;
    for (const CellularNetworkSettings& n : m_settings.m_networks)
    {
        if (n.m_id == id || (n.m_rxDeviceSetIndex != current->m_rxDeviceSetIndex && n.m_txDeviceSetIndex != current->m_txDeviceSetIndex)) {
            continue;
        }
        std::vector<modemnmt::NmtCarrierConfig> carriers;
        QString error;
        if (CellularNetworkSettings::parseCarriers(n.m_carriers, carriers, error) || !carriers.empty())
        {
            for (const modemnmt::NmtCarrierConfig& c : carriers) {
                others[c.channel] = n.m_name;
            }
        }
    }
    CellularNetworkDialog dialog(*current, others, this);
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

// ---- the console: the operator's own phone ----

QWidget *CellularGUI::buildConsole()
{
    QWidget *page = new QWidget(m_tabs);
    m_consolePage = page;
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // what it does now
    QHBoxLayout *stateRow = new QHBoxLayout();
    m_consoleBell = new QLabel(page);
    m_consoleBell->setPixmap(QIcon(":/bell_line.png").pixmap(16, 16));
    // one line whatever it says: the rows under it never move
    m_consoleState = new QLabel(page);
    m_consoleState->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_consoleState->setMinimumWidth(0);
    stateRow->addWidget(m_consoleBell);
    stateRow->addWidget(m_consoleState, 1);
    // its own number: a network number, the country digit and six digits
    m_consoleOwn = new QLineEdit(page);
    m_consoleOwn->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{7}"), m_consoleOwn));
    m_consoleOwn->setFixedWidth(m_consoleOwn->fontMetrics().horizontalAdvance("00000000") + 12);
    m_consoleOwn->setToolTip("The console's own number (7 digits, as a phone's): the phones call it on every "
        "network on the air, and its calls and messages come from it");
    // "Idle. Console number [1000001]": the number where it is read
    QLabel *own = new QLabel("Console number", page);
    stateRow->addWidget(own);
    stateRow->addWidget(m_consoleOwn);
    layout->addLayout(stateRow);
    // the phone of the call (two lines) and its touch tones (one): their room is always kept
    m_consoleRadio = new QLabel(page);
    m_consoleRadio->setToolTip("The phone of the call as the network hears it (as in Mobiles): its state, channel, "
        "power level and signal. The call's meters and log are in the Call section");
    m_consoleRadio->setFixedHeight(m_consoleRadio->fontMetrics().lineSpacing() * 2 + 2);
    m_consoleRadio->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(m_consoleRadio);
    m_consoleKeys = new QLabel(page);
    m_consoleKeys->setToolTip("Touch tones the phone sent in the call (in-band DTMF), as they come");
    m_consoleKeys->setFixedHeight(m_consoleKeys->fontMetrics().lineSpacing() + 2);
    m_consoleKeys->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(m_consoleKeys);

    // the number and the pad
    QHBoxLayout *numberRow = new QHBoxLayout();
    m_consoleNumber = new QLineEdit(page);
    m_consoleNumber->setPlaceholderText("number");
    m_consoleNumber->setToolTip("The number to call: a phone (country digit and six digits) or any other number "
        "(echo or music of its network). In a call the pad sends touch tones");
    QFont big = m_consoleNumber->font();
    big.setPointSizeF(big.pointSizeF() * 1.5);
    m_consoleNumber->setFont(big);
    QToolButton *back = new QToolButton(page);
    back->setText(QString(QChar(0x232B)));
    back->setToolTip("Delete the last digit");
    numberRow->addWidget(m_consoleNumber, 1);
    numberRow->addWidget(back);
    layout->addLayout(numberRow);

    QGridLayout *pad = new QGridLayout();
    pad->setSpacing(3);
    const char *keys = "123456789*0#";
    for (int i = 0; i < 12; i++)
    {
        const QChar key(keys[i]);
        QToolButton *b = new QToolButton(page);
        b->setText(key);
        b->setFont(big);
        b->setMinimumSize(40, 32);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        pad->addWidget(b, i / 3, i % 3);
        connect(b, &QToolButton::clicked, this, [this, key]() { consoleKey(key); });
    }
    layout->addLayout(pad);

    // the call, then the audio: as SDRangel's NFM demodulator and modulator (left: on/off, right: the device)
    // as on a phone: the green key calls, or answers the call that rings; the red one hangs up or refuses
    m_consoleCall = new QToolButton(page);
    m_consoleCall->setMinimumWidth(64);
    m_consoleHangUp = new QToolButton(page);
    m_consoleHangUp->setText("Hang up");
    m_consoleHangUp->setMinimumWidth(64);
    m_consoleHangUp->setToolTip("Hang up, or refuse the call that rings");
    m_consoleFlash = new QToolButton(page);
    m_consoleFlash->setText("R");
    m_consoleFlash->setMaximumWidth(22);
    m_consoleFlash->setEnabled(false);
    m_consoleFlash->setToolTip("Flash (register recall) to the far end. NMT has no signal that takes it to a phone: "
        "it waits for a far end that does (the network port)");
    m_consoleSpeakerMute = new QToolButton(page);
    QIcon sound;
    sound.addFile(":/sound_on.png", QSize(), QIcon::Normal, QIcon::Off);
    sound.addFile(":/sound_off.png", QSize(), QIcon::Normal, QIcon::On);
    m_consoleSpeakerMute->setIcon(sound);
    m_consoleSpeakerMute->setCheckable(true);
    m_consoleVolume = new QSpinBox(page);
    m_consoleVolume->setRange(-40, 0);
    m_consoleVolume->setSuffix(" dB");
    m_consoleVolume->setToolTip("The speaker's level, at most 0 dB");
    m_consoleMicOn = new ButtonSwitch(page);
    m_consoleMicOn->setIcon(QIcon(":/microphone.png"));
    m_consoleMicOn->setCheckable(true);
    m_consoleGain = new QSpinBox(page);
    m_consoleGain->setRange(-20, 20);
    m_consoleGain->setSuffix(" dB");
    m_consoleGain->setToolTip("The microphone's gain");
    m_consoleAuto = new ButtonSwitch(page);
    m_consoleAuto->setText("AUTO");
    m_consoleAuto->setToolTip("Auto answer: a call to the console is answered at once, without ringing "
        "(tests without anyone at the PC)");
    layout->addWidget(flowRow(page, {m_consoleCall, m_consoleHangUp, m_consoleAuto, m_consoleFlash,
        m_consoleSpeakerMute, m_consoleVolume, m_consoleMicOn, m_consoleGain}));
    CRightClickEnabler *speakerRight = new CRightClickEnabler(m_consoleSpeakerMute);
    connect(speakerRight, &CRightClickEnabler::rightClick, this, [this](const QPoint& p) { consoleDevice(p, false); });
    CRightClickEnabler *micRight = new CRightClickEnabler(m_consoleMicOn);
    connect(micRight, &CRightClickEnabler::rightClick, this, [this](const QPoint& p) { consoleDevice(p, true); });

    // messages to the number
    QFormLayout *form = new QFormLayout();
    // the text over the window's width, the send key at its end
    QHBoxLayout *smsRow = new QHBoxLayout();
    m_consoleSms = new QLineEdit(page);
    m_consoleSms->setPlaceholderText("text");
    QToolButton *sms = iconButton(page, ":/sms.png", "Send the text as a short message from the console's number");
    smsRow->addWidget(new QLabel("SMS", page));
    smsRow->addWidget(m_consoleSms, 1);
    smsRow->addWidget(sms);
    layout->addLayout(smsRow);
    connect(m_consoleSms, &QLineEdit::returnPressed, sms, &QToolButton::click);
    // the indicators: one message with the flags ticked, delivered when the phone can take it
    const QStringList kinds = {"SMS", "Voice mail", "Fax", "E-mail", "Data"};
    QList<QWidget*> flags;
    for (int i = 0; i < kinds.size(); i++)
    {
        QCheckBox *box = new QCheckBox(kinds[i], page);
        box->setProperty("flag", 1 << i);
        m_consoleMwi.append(box);
        flags.append(box);
    }
    QToolButton *mwi = iconButton(page, ":/bell_gradient.png", "Send the indicators ticked to the phone of the number, "
        "one message (frame 5c, NMT Doc 450-1 4.4.1.16). An idle phone gets it in a short call without ringing, a phone "
        "in a call at once, a busy phone when it is free. The network only sets indicators; the phone switches them off");
    flags.append(mwi);
    form->addRow("Waiting", flowRow(page, flags));
    layout->addLayout(form);

    // the call with a phone: what the network can do to it (as the Test tab)
    m_consoleInCall = new QWidget(page);
    QFormLayout *inCall = new QFormLayout(m_consoleInCall);
    inCall->setContentsMargins(0, 0, 0, 0);
    m_consoleChannel = new QComboBox(m_consoleInCall);
    m_consoleChannel->setToolTip("The voice (traffic) channel to move the call to: one of the network's, or any free one");
    QToolButton *move = iconButton(m_consoleInCall, ":/swap.png",
        "Move the call to that voice channel (switching a call in progress, NMT Doc 450-1 4.4.1.4)");
    inCall->addRow("Voice channel", flowRow(m_consoleInCall, {m_consoleChannel, move}));
    m_consolePower = new QComboBox(m_consoleInCall);
    m_consolePower->addItem("High", 3);
    m_consolePower->addItem("Medium", 2);
    m_consolePower->addItem("Low", 1);
    m_consolePower->setToolTip("MS output power change in the call (NMT Doc 450-1 4.4.1.8)");
    QToolButton *power = iconButton(m_consoleInCall, ":/antenna.png", "Order the power level");
    inCall->addRow("Power", flowRow(m_consoleInCall, {m_consolePower, power}));
    layout->addWidget(m_consoleInCall);
    connect(move, &QToolButton::clicked, this, [this]() {
        requestAction(m_consoleReport.m_network, m_consoleReport.m_peer, Cellular::MsgAction::Move, QString(), m_consoleChannel->currentData().toInt());
    });
    connect(power, &QToolButton::clicked, this, [this]() {
        requestAction(m_consoleReport.m_network, m_consoleReport.m_peer, Cellular::MsgAction::Power, QString(),
            m_consolePower->currentData().toInt());
    });

    // its settings
    layout->addStretch(1);

    connect(back, &QToolButton::clicked, this, [this]() { m_consoleNumber->backspace(); });
    connect(m_consoleNumber, &QLineEdit::returnPressed, this, [this]() {
        if (!m_consoleNumber->text().isEmpty()) {
            requestAction(0, m_consoleNumber->text(), Cellular::MsgAction::ConsoleDial, QString(), 0);
        }
    });
    connect(m_consoleCall, &QToolButton::clicked, this, [this]() {
        if (m_consoleReport.m_state == Cellular::MsgReportConsole::Incoming) {
            requestAction(0, QString(), Cellular::MsgAction::ConsoleAnswer, QString(), 0);
        } else if (!m_consoleNumber->text().isEmpty()) {
            requestAction(0, m_consoleNumber->text(), Cellular::MsgAction::ConsoleDial, QString(), 0);
        }
    });
    connect(m_consoleHangUp, &QToolButton::clicked, this, [this]() {
        requestAction(0, QString(), Cellular::MsgAction::ConsoleHangUp, QString(), 0);
    });
    connect(sms, &QToolButton::clicked, this, [this]() {
        const QString text = m_consoleSms->text();
        if (!consoleTarget().isEmpty() && !text.isEmpty()) {
            requestAction(0, consoleTarget(), Cellular::MsgAction::ConsoleSms, text, 0);
        }
    });
    connect(mwi, &QToolButton::clicked, this, [this]() {
        int flags = 0;
        for (const QCheckBox *box : m_consoleMwi) {
            flags |= box->isChecked() ? box->property("flag").toInt() : 0;
        }
        const QString number = consoleTarget();
        if (flags && !number.isEmpty()) {
            requestAction(networkOfPhone(number), number, Cellular::MsgAction::Mwi, QString(), flags);
        }
    });
    auto changed = [this]() {
        if (!m_doApplySettings) {
            return;
        }
        CellularConsole& c = m_settings.m_console;
        c.m_number = m_consoleOwn->text().trimmed();
        c.m_speakerMute = m_consoleSpeakerMute->isChecked();
        c.m_micMute = !m_consoleMicOn->isChecked();
        c.m_volumeDb = m_consoleVolume->value();
        c.m_micGainDb = m_consoleGain->value();
        c.m_autoAnswer = m_consoleAuto->isChecked();
        applySettings();
    };
    connect(m_consoleOwn, &QLineEdit::editingFinished, this, changed);
    connect(m_consoleSpeakerMute, &QToolButton::toggled, this, changed);
    connect(m_consoleMicOn, &QToolButton::toggled, this, changed);
    connect(m_consoleVolume, QOverload<int>::of(&QSpinBox::valueChanged), this, changed);
    connect(m_consoleGain, QOverload<int>::of(&QSpinBox::valueChanged), this, changed);
    connect(m_consoleAuto, &QToolButton::toggled, this, changed);
    showConsole();
    return page;
}

// The phone in the console's call, as its row in Mobiles shows it.
QString CellularGUI::radioOf(const QString& number) const
{
    for (int row = 0; row < m_phones->rowCount() && !number.isEmpty(); row++)
    {
        const QTableWidgetItem *item = m_phones->item(row, MobileName);
        const QTableWidgetItem *state = m_phones->item(row, MobileState);
        if (item && state && item->data(CellularMobileRole::Number).toString() == number) {
            return state->text();
        }
    }
    return QString();
}

void CellularGUI::showConsoleRadio()
{
    const QString text = m_consoleReport.m_state != Cellular::MsgReportConsole::Idle ? radioOf(m_consoleReport.m_peer) : QString();
    m_consoleRadio->setText(text);
}

// the speaker's or microphone's device, as SDRangel's channels select theirs
void CellularGUI::consoleDevice(const QPoint& p, bool input)
{
    CellularConsole& c = m_settings.m_console;
    AudioSelectDialog select(DSPEngine::instance()->getAudioDeviceManager(), input ? c.m_microphone : c.m_speaker, input);
    select.move(p);
    new DialogPositioner(&select, false);
    select.exec();
    if (!select.m_selected) {
        return;
    }
    (input ? c.m_microphone : c.m_speaker) = select.m_audioDeviceName;
    showConsole();
    applySettings();
}

// the number the console's messages go to: the phone of its call, else the number typed
QString CellularGUI::consoleTarget() const
{
    return (m_consoleReport.m_state != Cellular::MsgReportConsole::Idle && !m_consoleReport.m_peer.isEmpty())
        ? m_consoleReport.m_peer : m_consoleNumber->text().trimmed();
}

int CellularGUI::networkOfPhone(const QString& number) const
{
    if (m_consoleReport.m_network && consoleTarget() == m_consoleReport.m_peer) {
        return m_consoleReport.m_network;
    }
    for (int row = 0; row < m_phones->rowCount(); row++)
    {
        const QTableWidgetItem *item = m_phones->item(row, MobileName);
        if (item && item->data(CellularMobileRole::Number).toString() == number) {
            return item->data(CellularMobileRole::Network).toInt();
        }
    }
    return m_status.isEmpty() ? 0 : m_status.firstKey();
}

// A key of the pad: a touch tone in a call, else a digit of the number.
void CellularGUI::consoleKey(QChar key)
{
    if (m_consoleReport.m_state == Cellular::MsgReportConsole::Connected) {
        requestAction(0, QString(), Cellular::MsgAction::ConsoleDigit, QString(key), 0);
    } else {
        m_consoleNumber->insert(QString(key));
    }
}

void CellularGUI::showConsole()
{
    const CellularConsole& c = m_settings.m_console;
    const bool apply = m_doApplySettings;
    m_doApplySettings = false;
    if (!m_consoleOwn->hasFocus()) {
        m_consoleOwn->setText(c.m_number);
    }
    m_consoleSpeakerMute->setChecked(c.m_speakerMute);
    m_consoleMicOn->setChecked(!c.m_micMute);
    const auto deviceName = [](const QString& name) {
        return name.isEmpty() || name == AudioDeviceManager::m_defaultDeviceName ? QString("the default device") : name;
    };
    m_consoleSpeakerMute->setToolTip(QString("Left: mute/unmute the speaker (also listening in). Right: select the "
        "audio output device (now %1)").arg(deviceName(c.m_speaker)));
    m_consoleMicOn->setToolTip(QString("Left: microphone on/off (touch tones still go). Right: select the audio "
        "input device (now %1)").arg(deviceName(c.m_microphone)));
    m_consoleVolume->setValue(c.m_volumeDb);
    m_consoleGain->setValue(c.m_micGainDb);
    m_consoleAuto->setChecked(c.m_autoAnswer);
    m_doApplySettings = apply;

    using R = Cellular::MsgReportConsole;
    const ConsoleView& r = m_consoleReport;
    const qint64 s = r.m_since.isValid() ? r.m_since.secsTo(QDateTime::currentDateTime()) : 0;
    const QString time = QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    QString text;
    switch (r.m_state)
    {
    case R::Idle: text = "Idle."; break;
    case R::Calling: text = QString("Calling %1: paging").arg(r.m_peer); break;
    case R::Ringback: text = QString("Calling %1: it rings").arg(r.m_peer); break;
    case R::Incoming: text = QString("<b>%1 calls the console</b>").arg(r.m_peer); break;
    case R::Connected: text = QString("In a call with %1, %2").arg(r.m_peer).arg(time); break;
    }
    if (r.m_network && r.m_state != R::Idle) {
        text += QString(" (%1)").arg(networkName(r.m_network));
    }
    m_consoleState->setText(text);
    m_consoleBell->setPixmap(QIcon(r.m_state == R::Incoming ? ":/bell_red.png" : ":/bell_line.png").pixmap(16, 16));
    m_consoleKeys->setText(r.m_keys.isEmpty() ? QString() : QString("Tones from %1: %2").arg(r.m_peer).arg(r.m_keys));
    // the green key: dark idle (call), bright while a call rings (answer), mid in a call;
    // the red key red while there is a call; the speaker green while a call is heard
    const bool ringing = r.m_state == R::Incoming;
    const bool inCall = r.m_state != R::Idle;
    m_consoleCall->setText(ringing ? "Answer" : "Call");
    m_consoleCall->setToolTip(ringing ? "Answer the call that rings" : "Call the number");
    m_consoleCall->setEnabled(r.m_state == R::Idle || ringing);
    m_consoleCall->setStyleSheet(ringing ? "QToolButton { background-color: rgb(40,200,40); color: black; font-weight: bold; }"
        : inCall ? "QToolButton { background-color: rgb(40,120,40); }"
        : "QToolButton { background-color: rgb(30,80,30); }");
    m_consoleHangUp->setEnabled(inCall);
    m_consoleHangUp->setStyleSheet(inCall ? "QToolButton { background-color: rgb(200,40,40); }"
        : "QToolButton { background:rgb(79,79,79); }");
    m_consoleSpeakerMute->setStyleSheet(inCall && !c.m_speakerMute ? "QToolButton { background-color : green; }"
        : "QToolButton { background:rgb(79,79,79); }");
    // with a phone in the call: its orders (only a phone number has a call on a channel)
    // always there (rows never move), usable only in a call with a phone
    m_consoleInCall->setEnabled(r.m_state == R::Connected && r.m_peer.size() == 7);
    if (r.m_network) {
        voiceChannels(m_consoleChannel, r.m_network);
    }
}

void CellularGUI::requestAction(int network, const QString& number, int action, const QString& text, int value)
{
    m_cellular->getInputMessageQueue()->push(Cellular::MsgAction::create(network,
        static_cast<Cellular::MsgAction::Action>(action), number, text, value));
}

void CellularGUI::phoneMenu(const QPoint& pos)
{
    const QTableWidgetItem *item = m_phones->itemAt(pos);
    const QTableWidgetItem *numberItem = item ? m_phones->item(item->row(), MobileName) : nullptr;

    if (!numberItem) {
        return;
    }
    const QString number = numberItem->data(CellularMobileRole::Number).toString();
    const modemnmt::System system = static_cast<modemnmt::System>(numberItem->data(CellularMobileRole::System).toInt());
    // the network that heard it last acts; a phone no network heard: the one of the Test tab
    const int heardOn = numberItem->data(CellularMobileRole::Network).toInt();
    const int network = heardOn ? heardOn : m_testNetwork->currentData().toInt();
    QMenu menu(this);

    menu.addAction("Record...", this, [this, system, number]() { editMobile(system, number); });
    menu.addAction("Copy number", this, [number]() { QGuiApplication::clipboard()->setText(number); });
    menu.addSeparator();
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
    menu.addAction("Forget", this, [this, system, number]() { forgetMobile(system, number); });
    menu.exec(m_phones->viewport()->mapToGlobal(pos));
}

// ---- the book ----

modemnmt::System CellularGUI::systemOf(int network) const
{
    const CellularNetworkSettings *n = m_settings.network(network);
    return n && n->m_system == 1 ? modemnmt::System::Nmt900 : modemnmt::System::Nmt450;
}

// What the phone does on the networks now: the network that heard it last gives the state; the
// other networks that know it are listed in `line`.
QString CellularGUI::liveText(const QString& number, int& network, QColor& colour, QString& line) const
{
    QDateTime last;
    QString state;
    QStringList where;

    network = 0;
    colour = QColor(0x55, 0x55, 0x55);
    line.clear();
    for (auto it = m_status.constBegin(); it != m_status.constEnd(); ++it)
    {
        for (const Cellular::MsgReportStatus::Phone& p : it->phones)
        {
            if (p.number != number) {
                continue;
            }
            where.append(networkName(it.key()) + (p.registered ? " (reg)" : ""));
            if (!network || (p.lastHeard.isValid() && (!last.isValid() || p.lastHeard > last)))
            {
                network = it.key();
                last = p.lastHeard;
                state = p.state.isEmpty() ? QString("idle") : p.state;
                line = QString("%1%2").arg(networkName(it.key()))
                    .arg(p.lastHeard.isValid() ? QString(", %1 dB%2, %3").arg(p.level, 0, 'f', 1)
                        .arg(p.power >= 0 ? QString(", power %1").arg(p.power) : QString())   // level in its frames
                        .arg(p.lastHeard.toString("HH:mm:ss")) : QString());
                colour = state.contains("call") || state.contains("conversation") ? QColor(0x3f, 0xbf, 0x5f)
                    : state.contains("ring") || state.contains("pag") || state.contains("sms") ? QColor(0xe0, 0xa0, 0x30)
                    : p.registered ? QColor(0x4a, 0x8a, 0xd0) : QColor(0x9a, 0x9a, 0x9a);
            }
        }
    }
    if (!network) {
        return "not seen";
    }
    if (where.size() > 1) {
        line += " (also " + where.mid(1).join(", ") + ")";
    }
    return state;
}

int CellularGUI::mobileRow(modemnmt::System system, const QString& number) const
{
    for (int row = 0; row < m_phones->rowCount(); row++)
    {
        const QTableWidgetItem *item = m_phones->item(row, MobileName);
        if (item && item->data(CellularMobileRole::Number).toString() == number
            && item->data(CellularMobileRole::System).toInt() == static_cast<int>(system)) {
            return row;
        }
    }
    return -1;
}

// One row per phone, the book and the phones the networks hear. Rows stay where they are; new
// phones come at the end; a forgotten phone that no network hears goes.
void CellularGUI::showMobiles()
{
    struct Key { modemnmt::System system; QString number; };
    std::vector<Key> keys;
    const auto add = [&keys](modemnmt::System system, const QString& number) {
        if (std::none_of(keys.begin(), keys.end(), [&](const Key& k) { return k.system == system && k.number == number; })) {
            keys.push_back({system, number});
        }
    };
    for (const modemnmt::NmtPhoneRecord *r : m_book.all()) {
        add(r->system, QString::fromStdString(r->number));
    }
    for (auto it = m_status.constBegin(); it != m_status.constEnd(); ++it) {
        for (const Cellular::MsgReportStatus::Phone& p : it->phones) {
            add(systemOf(it.key()), p.number);
        }
    }

    bool heightChanged = false;
    for (const Key& k : keys)
    {
        const modemnmt::NmtPhoneRecord *r = m_book.find(k.system, k.number.toStdString());
        int network;
        QColor colour;
        QString line;
        const QString state = liveText(k.number, network, colour, line);
        const QString name = r ? QString::fromStdString(r->value(modemnmt::NmtPhoneRecord::Name)) : QString();
        QStringList chips;
        if (r)
        {
            if (!r->areas.empty()) {
                chips << QString("Y1 %1|#2b4867").arg(r->areas.front().first);
            }
            if (!r->value(modemnmt::NmtPhoneRecord::Password).empty()) {
                chips << QString("K %1|#2b4867").arg(QString::fromStdString(r->value(modemnmt::NmtPhoneRecord::Password)));
            }
            if (!r->value(modemnmt::NmtPhoneRecord::SmsCentre).empty()) {
                chips << QString("SMSC %1|#5b4a20").arg(QString::fromStdString(r->value(modemnmt::NmtPhoneRecord::SmsCentre)));
            }
            if (!r->value(modemnmt::NmtPhoneRecord::VoiceMail).empty()) {
                chips << QString("VM %1|#2b5b47").arg(QString::fromStdString(r->value(modemnmt::NmtPhoneRecord::VoiceMail)));
            }
        }
        // what waits for it on the network that knows it
        for (auto it = m_status.constBegin(); it != m_status.constEnd(); ++it)
        {
            for (const Cellular::MsgReportStatus::Phone& p : it->phones)
            {
                if (p.number != k.number) {
                    continue;
                }
                if (p.held > 0) {
                    chips << QString("%1 SMS kept|#4a3b6b").arg(p.held);
                }
                if (p.pendingMwi) {
                    chips << QString("indicator waits|#4a3b6b");
                }
            }
        }

        int row = mobileRow(k.system, k.number);
        if (row < 0)
        {
            row = m_phones->rowCount();
            m_phones->insertRow(row);
            QTableWidgetItem *item = new QTableWidgetItem();
            m_phones->setItem(row, MobileName, item);
            setCell(m_phones, row, MobileState, QString());
            heightChanged = true;
        }
        QTableWidgetItem *item = m_phones->item(row, MobileName);
        const QStringList oldChips = item->data(CellularMobileRole::Chips).toStringList();
        item->setData(CellularMobileRole::Number, k.number);
        item->setData(CellularMobileRole::System, static_cast<int>(k.system));
        item->setData(CellularMobileRole::Network, network);
        item->setData(CellularMobileRole::Title, name.isEmpty() ? k.number : name);
        item->setData(CellularMobileRole::Subtitle, name.isEmpty() ? QString() : k.number);
        item->setData(CellularMobileRole::StateColour, colour);
        item->setData(CellularMobileRole::Chips, chips);
        item->setText(name.isEmpty() ? k.number : name + " " + k.number);     // for search and copy
        setCell(m_phones, row, MobileState, line.isEmpty() ? state : state + "\n" + line);
        heightChanged = heightChanged || oldChips != chips;
    }
    // rows of phones that are neither in the book nor heard any more
    for (int row = m_phones->rowCount() - 1; row >= 0; row--)
    {
        const QTableWidgetItem *item = m_phones->item(row, MobileName);
        const QString number = item ? item->data(CellularMobileRole::Number).toString() : QString();
        const modemnmt::System system = static_cast<modemnmt::System>(item ? item->data(CellularMobileRole::System).toInt() : 0);
        const bool keep = std::any_of(keys.begin(), keys.end(), [&](const Key& k) { return k.system == system && k.number == number; });
        if (!keep)
        {
            m_phones->removeRow(row);
            heightChanged = true;
        }
    }
    if (heightChanged) {
        m_phones->resizeRowsToContents();
    }
    showSpeech();       // the followed call's radio line
}

void CellularGUI::editMobile(modemnmt::System system, const QString& number)
{
    const modemnmt::NmtPhoneRecord *found = m_book.find(system, number.toStdString());
    modemnmt::NmtPhoneRecord record;

    if (found) {
        record = *found;
    }
    else
    {
        record.system = system;
        record.number = number.toStdString();
    }
    int network;
    QColor colour;
    QString line;
    const QString state = liveText(number, network, colour, line);
    CellularMobileDialog dialog(record, line.isEmpty() ? state : state + ", " + line, this);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // only what the user changed: what was learned meanwhile stays
    for (const auto& change : dialog.changes()) {
        m_cellular->getInputMessageQueue()->push(Cellular::MsgBookEdit::create(system, number.toStdString(), change.first, change.second));
    }
}

void CellularGUI::addMobile()
{
    bool ok = false;
    const QString number = QInputDialog::getText(this, "Add a phone", "Number: the country digit and the six digits",
        QLineEdit::Normal, QString(), &ok).trimmed();

    if (!ok || number.size() != 7 || number.contains(QRegularExpression("[^0-9]"))) {
        return;
    }
    const modemnmt::System system = systemOf(m_testNetwork->currentData().toInt());
    // a record with no name yet: the feature makes it and sends it back
    m_cellular->getInputMessageQueue()->push(Cellular::MsgBookEdit::create(system, number.toStdString(),
        modemnmt::NmtPhoneRecord::Name, std::string()));
    editMobile(system, number);
}

void CellularGUI::forgetMobile(modemnmt::System system, const QString& number)
{
    if (QMessageBox::question(this, "Forget", QString("Forget the phone %1 and what was learned of it?").arg(number))
            != QMessageBox::Yes) {
        return;
    }
    m_cellular->getInputMessageQueue()->push(Cellular::MsgBookEdit::create(system, number.toStdString(), -1, std::string()));
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
    m_logEnable->setChecked(m_settings.m_logEnabled);
    m_logFilename->setToolTip(logFileTip(m_settings.m_logFilename));
    showConsole();

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
    CellularColumns::apply(m_networks, m_settings.m_networkColumnIndexes, m_settings.m_networkColumnSizes);
    CellularColumns::apply(m_carriers, m_settings.m_carrierColumnIndexes, m_settings.m_carrierColumnSizes);

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

const CellularSpeechSeen *CellularGUI::speechOf(const QString& number) const
{
    const auto it = m_speech.constFind(number);
    return it == m_speech.constEnd() ? nullptr : &it.value();
}

// The status line follows the selected phone (its call, or its last call), else the only call
// that is on.
void CellularGUI::showSpeech()
{
    const QModelIndexList rows = m_phones->selectionModel()->selectedRows();
    const QTableWidgetItem *item = rows.isEmpty() ? nullptr : m_phones->item(rows.first().row(), MobileName);
    QString number = item ? item->data(CellularMobileRole::Number).toString() : QString();
    const CellularSpeechSeen *seen = number.isEmpty() ? nullptr : speechOf(number);
    const bool console = m_consoleReport.m_state != Cellular::MsgReportConsole::Idle && !m_consoleReport.m_peer.isEmpty();

    // on the Console tab: the call of the console (its other side, a phone of a network)
    if (console && m_tabs->currentWidget() == m_consolePage)
    {
        number = m_consoleReport.m_peer;
        seen = speechOf(number);
    }
    else if (!seen)
    {
        int live = 0;
        for (auto it = m_speech.constBegin(); it != m_speech.constEnd(); ++it)
        {
            if (it->live())
            {
                live++;
                seen = &it.value();
            }
        }
        if (live != 1) {
            seen = nullptr;
        }
        number = seen ? seen->call.phone : console ? m_consoleReport.m_peer : number;
    }
    m_speechLine->show(seen);
    showConsoleRadio();

    // The Call section follows a phone from before its call, through its channel moves, to after
    // it, until a call of another phone is followed; stopped, it keeps what it shows.
    if (!m_callFollow->isChecked()) {
        return;
    }
    if (!number.isEmpty() && number != m_callNumber)
    {
        m_callNumber = number;
        m_callNetwork = number == m_consoleReport.m_peer && m_consoleReport.m_network ? m_consoleReport.m_network
            : item && item->data(CellularMobileRole::Number).toString() == number ? item->data(CellularMobileRole::Network).toInt() : 0;
        m_callChannel = 0;
        m_callLog->clear();
        // what the phone did before: the lines that name it
        for (const LogLine& line : m_logLines)
        {
            if (callLine(line.network, line.text)) {
                m_callLog->appendPlainText(line.time + " " + line.text);
            }
        }
    }
    const CellularSpeechSeen *followed = m_callNumber.isEmpty() ? nullptr : speechOf(m_callNumber);
    const int channel = followed && followed->live() ? followed->call.channel : 0;
    if (channel != m_callChannel)
    {
        // its channel's lines from now on (the channel it moved to)
        m_callChannel = channel;
        if (channel > 0) {
            m_callLog->appendPlainText(QString("--- %1 on channel %2").arg(m_callNumber).arg(channel));
        }
    }
    QString title = m_callNumber;
    if (console && m_callNumber == m_consoleReport.m_peer) {
        title += " with the console";
    }
    m_callPanel->show(followed, title, radioOf(m_callNumber));
}

void CellularGUI::showStatus(const Cellular::MsgReportStatus& report)
{
    NetworkStatus& s = m_status[report.m_network];

    s.onAir = report.m_onAir;
    s.registered = report.m_registered;
    s.calls = report.m_calls;
    s.summary = report.m_summary;
    s.carriers = report.m_carriers;
    s.phones = report.m_phones;
    // the last calls of the phones, for a GUI that opened after them
    for (const Cellular::MsgReportStatus::Phone& p : report.m_phones)
    {
        if (p.lastCall.call > 0 && !m_speech.contains(p.number))
        {
            m_speech[p.number].call = p.lastCall;
            m_speech[p.number].received = p.lastCall.ended;
        }
    }
    showMobiles();
    showSpeech();
    showCarriers();
    showSummary();
    showTestPhone();
}

// The carriers of all networks, in the order of the networks.
void CellularGUI::showCarriers()
{
    int row = 0;
    QList<QPair<double, double>> spans;
    QList<CellularBandStrip::Carrier> bars;

    for (const CellularNetworkSettings& n : m_settings.m_networks)
    {
        const auto it = m_status.constFind(n.m_id);
        if (it == m_status.constEnd()) {
            continue;
        }
        double low, high, centre;
        int rate;
        if (CellularNetworkDialog::deviceSpan(n.m_rxDeviceSetIndex, false, low, high, centre, rate)
            && !spans.contains(qMakePair(low, high)))
        {
            spans.append(qMakePair(low, high));
        }
        for (const Cellular::MsgReportStatus::Carrier& c : it->carriers)
        {
            CellularBandStrip::Carrier bar;
            bar.key = n.m_id * 10000 + c.channel;
            bar.network = n.m_name;
            bar.channel = c.channel;
            bar.type = c.type;
            bar.colour = CellularChannelGrid::typeColourOf(c.type);
            bar.uplink = c.uplink;
            bar.state = c.state;
            bar.rxLevel = c.rxLevel;
            bar.phone = c.phone;
            bar.framesReceived = c.framesReceived;
            bars.append(bar);

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
    m_strip->setData(spans, bars);
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
void CellularGUI::voiceChannels(QComboBox *box, int network) const
{
    const int kept = box->currentData().toInt();
    std::vector<modemnmt::NmtCarrierConfig> carriers;
    QString error;
    const CellularNetworkSettings *n = m_settings.network(network);
    if (n) {
        CellularNetworkSettings::parseCarriers(n->m_carriers, carriers, error);
    }
    QList<int> channels;
    for (const modemnmt::NmtCarrierConfig& c : carriers)
    {
        if (c.type == modemnmt::ChannelType::Tc || c.type == modemnmt::ChannelType::AcTc || c.type == modemnmt::ChannelType::CcTc) {
            channels.append(c.channel);
        }
    }
    QList<int> shown;
    for (int i = 1; i < box->count(); i++) {
        shown.append(box->itemData(i).toInt());
    }
    if (box->count() > 0 && shown == channels) {
        return;
    }
    box->clear();
    box->addItem("any free", 0);
    for (int c : channels) {
        box->addItem(QString::number(c), c);
    }
    box->setCurrentIndex(std::max(0, box->findData(kept)));
}

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
    voiceChannels(m_moveChannel, network);

    // the orders of a call only while the phone has one: release any call, power and voice
    // channel in a conversation on a voice channel
    bool busy = false;
    if (it != m_status.constEnd())
    {
        for (const Cellular::MsgReportStatus::Phone& p : it->phones) {
            busy = busy || (p.number == number && !p.state.isEmpty());
        }
    }
    const CellularSpeechSeen *seen = speechOf(number);
    const bool talking = seen && seen->live();
    m_testRelease->setEnabled(busy || talking);
    for (QWidget *w : {static_cast<QWidget*>(m_powerLevel), static_cast<QWidget*>(m_testPower),
                       static_cast<QWidget*>(m_moveChannel), static_cast<QWidget*>(m_testMove)}) {
        w->setEnabled(talking);
    }

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
    return m_logFrames->isChecked() || line.kind != Cellular::MsgReportLog::Frame;
}

// "12:00:01 [SMS] Y1=8: SMS from ...": the kind as a coloured badge, the text escaped (it may
// carry what a phone sent)
QString CellularGUI::logHtml(const LogLine& line, bool withNetwork) const
{
    using K = Cellular::MsgReportLog;
    // by kind: Event (no badge), Frame, Call, Sms, Mwi, Power, Phone, Tone, Network, Warning
    static const char *const colours[] = {
        "", "#3a3a3a", "#2e6b3e", "#5b4a20", "#4a3b6b", "#6b4a2b", "#2b4867", "#2b5b5b", "#3b3b5b", "#7a2b2b"
    };
    const int kind = (line.kind >= K::Event && line.kind <= K::Warning) ? line.kind : K::Event;
    const QString badge = K::kindName(static_cast<K::Kind>(kind));
    QString html = line.time.toHtmlEscaped() + " ";

    if (!badge.isEmpty()) {
        html += QString("<span style=\"background-color:%1;\">&nbsp;%2&nbsp;</span> ").arg(colours[kind]).arg(badge);
    }
    if (withNetwork && line.network) {
        html += networkName(line.network).toHtmlEscaped() + ": ";
    }
    return html + line.text.toHtmlEscaped();
}

void CellularGUI::addLog(int network, const QString& text, int kind)
{
    LogLine line{QDateTime::currentDateTime().toString("HH:mm:ss"), network, text, kind};

    m_logLines.push_back(line);
    if (m_logLines.size() > static_cast<size_t>(m_maxLogLines)) {
        m_logLines.pop_front();
    }
    if (m_callFollow->isChecked() && callLine(network, text)) {
        m_callLog->appendPlainText(line.time + " " + text);
    }
    if (!logShown(line)) {
        return;
    }
    // the view stays where the user scrolled to; at the end it follows
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atEnd = bar->value() >= bar->maximum();
    const int value = bar->value();
    const bool all = m_logNetwork->currentData().toInt() < 0;

    m_log->appendHtml(logHtml(line, all));
    bar->setValue(atEnd ? bar->maximum() : value);
}

void CellularGUI::renderLog()
{
    const bool all = m_logNetwork->currentData().toInt() < 0;

    m_log->setMaximumBlockCount(0);
    m_log->clear();
    m_log->setUpdatesEnabled(false);
    for (const LogLine& line : m_logLines)
    {
        if (logShown(line)) {
            m_log->appendHtml(logHtml(line, all));
        }
    }
    m_log->setUpdatesEnabled(true);
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

// the settings with the book (presets are saved from here)
QByteArray CellularGUI::serialize() const
{
    CellularSettings settings = m_settings;

    settings.m_book = CellularBook::toBytes(m_book);
    return settings.serialize();
}

bool CellularGUI::deserialize(const QByteArray& data)
{
    const bool ok = m_settings.deserialize(data);

    m_feature->setWorkspaceIndex(m_settings.m_workspaceIndex);
    displaySettings();
    applySettings();
    // the book of the settings replaces the feature's (settings without a book leave it); the
    // feature sends its records back
    if (!m_settings.m_book.isEmpty())
    {
        m_book = modemnmt::NmtPhoneBook();
        CellularBook::fromBytes(m_settings.m_book, m_book);
        m_cellular->getInputMessageQueue()->push(Cellular::MsgBookReplace::create(m_settings.m_book));
        m_phones->setRowCount(0);
        showMobiles();
    }
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
        addLog(report.getNetwork(), report.getText(), report.getKind());
        return true;
    }
    else if (Cellular::MsgReportStatus::match(message))
    {
        showStatus(static_cast<const Cellular::MsgReportStatus&>(message));
        return true;
    }
    else if (Cellular::MsgReportConsole::match(message))
    {
        const Cellular::MsgReportConsole& r = static_cast<const Cellular::MsgReportConsole&>(message);
        const bool rings = r.m_state == Cellular::MsgReportConsole::Incoming && m_consoleReport.m_state != r.m_state;
        m_consoleReport.m_state = r.m_state;
        m_consoleReport.m_network = r.m_network;
        m_consoleReport.m_peer = r.m_peer;
        m_consoleReport.m_since = r.m_since;
        m_consoleReport.m_keys = r.m_keys;
        showConsole();
        showSpeech();
        // a call to the console: its tab shows
        if (rings) {
            m_tabs->setCurrentIndex(1);
        }
        return true;
    }
    else if (Cellular::MsgReportSpeech::match(message))
    {
        const Cellular::MsgReportSpeech& report = static_cast<const Cellular::MsgReportSpeech&>(message);
        const QDateTime now = QDateTime::currentDateTime();
        for (const CellularCallSpeech& call : report.m_calls)
        {
            m_speech[call.phone].call = call;
            m_speech[call.phone].received = now;
        }
        m_phones->viewport()->update();
        showSpeech();
        return true;
    }
    else if (Cellular::MsgBookRecord::match(message))
    {
        // the feature's record as it is now: the copy follows it exactly
        const Cellular::MsgBookRecord& msg = static_cast<const Cellular::MsgBookRecord&>(message);
        m_book.forget(msg.m_record.system, msg.m_record.number);
        if (!msg.m_removed) {
            m_book.merge(msg.m_record);
        }
        showMobiles();
        return true;
    }
    else if (Cellular::MsgConfigureCellular::match(message))
    {
        // the columns are the GUI's: moving one does not reach the feature, its copy is old
        const CellularSettings view = m_settings;
        m_settings = static_cast<const Cellular::MsgConfigureCellular&>(message).getSettings();
        m_settings.m_networkColumnIndexes = view.m_networkColumnIndexes;
        m_settings.m_networkColumnSizes = view.m_networkColumnSizes;
        m_settings.m_carrierColumnIndexes = view.m_carrierColumnIndexes;
        m_settings.m_carrierColumnSizes = view.m_carrierColumnSizes;
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
