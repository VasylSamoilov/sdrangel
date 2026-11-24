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

#include <QLabel>
#include <QVBoxLayout>

#include "device/deviceuiset.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "gui/flowlayout.h"
#include "gui/rollupcontents.h"
#include "maincore.h"
#include "plugin/pluginapi.h"

#include "cellularrx.h"
#include "cellularrxgui.h"
#include "speechbox.h"

namespace
{

// carrier ids are network * 10000 + channel number
const int kIdStride = 10000;
// the widest line of numbers a box has: its width
const char * const kWidest = "-1999.9 kHz, ch 9999";

// the name of the network takes the room the line of the channel leaves
void setBoxText(CellularCarrierBox *box, const QString& name, int channel, const QString& line2, const QString& line3)
{
    const QFontMetrics fm = box->fontMetrics();
    const QString ch = QString(" ch %1").arg(channel);
    const int room = box->contentsRect().width() - 8 - fm.horizontalAdvance(ch);
    box->setLines(fm.elidedText(name, Qt::ElideRight, room) + ch + "\n" + line2 + "\n" + line3);
}

} // namespace

CellularRxGUI* CellularRxGUI::create(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel)
{
    return new CellularRxGUI(pluginAPI, deviceUISet, rxChannel);
}

void CellularRxGUI::destroy()
{
    delete this;
}

CellularRxGUI::CellularRxGUI(PluginAPI* pluginAPI, DeviceUISet *deviceUISet, BasebandSampleSink *rxChannel, QWidget* parent) :
    ChannelGUI(parent),
    m_pluginAPI(pluginAPI),
    m_deviceUISet(deviceUISet),
    m_channelMarker(this),
    m_tickCount(0)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/channelrx/cellularrx/readme.md";
    m_cellularRx = reinterpret_cast<CellularRx*>(rxChannel);
    m_cellularRx->setMessageQueueToGUI(getInputMessageQueue());

    RollupContents *rollupContents = getRollupContents();
    QWidget *carriers = new QWidget(rollupContents);
    carriers->setObjectName("carriers");
    carriers->setWindowTitle("Carriers");
    QVBoxLayout *layout = new QVBoxLayout(carriers);
    layout->setContentsMargins(3, 3, 3, 3);
    m_span = new QLabel(carriers);
    m_span->setWordWrap(true);
    layout->addWidget(m_span);
    QWidget *boxes = new QWidget(carriers);
    // the boxes flow into as many rows as the width of the window needs
    m_boxLayout = new FlowLayout(boxes, 0, 3, 3);
    layout->addWidget(boxes);
    carriers->setMinimumHeight(60);

    setSizePolicy(rollupContents->sizePolicy());
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));
    connect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick())); // 50 ms

    m_channelMarker.blockSignals(true);
    m_channelMarker.setColor(QColor::fromRgb(m_settings.m_rgbColor));
    m_channelMarker.setBandwidth(0);
    m_channelMarker.setCenterFrequency(0);
    m_channelMarker.setTitle(m_settings.m_title);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setVisible(false);
    m_settings.setRollupState(&m_rollupState);

    displaySettings();
    sizeToContents();
    m_resizer.enableChildMouseTracking();
}

CellularRxGUI::~CellularRxGUI()
{
    disconnect(&MainCore::instance()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));
}

void CellularRxGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
}

QByteArray CellularRxGUI::serialize() const
{
    return m_settings.serialize();
}

bool CellularRxGUI::deserialize(const QByteArray& data)
{
    const bool ok = m_settings.deserialize(data);

    displaySettings();
    return ok;
}

void CellularRxGUI::displaySettings()
{
    m_channelMarker.blockSignals(true);
    m_channelMarker.setTitle(m_settings.m_title);
    m_channelMarker.blockSignals(false);
    m_channelMarker.setColor(QColor::fromRgb(m_settings.m_rgbColor));
    setTitleColor(QColor::fromRgb(m_settings.m_rgbColor));
    setWindowTitle(m_settings.m_title);
    setTitle(m_settings.m_title);
    getRollupContents()->restoreState(m_rollupState);
}

void CellularRxGUI::handleInputMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop())) {
        delete message;
    }
}

void CellularRxGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;
    getRollupContents()->saveState(m_rollupState);
}

void CellularRxGUI::onMenuDialogCalled(const QPoint& p)
{
    if (m_contextMenuType == ContextMenuType::ContextMenuChannelSettings)
    {
        BasicChannelSettingsDialog dialog(&m_channelMarker, this);
        dialog.setDefaultTitle(m_displayedName);
        dialog.move(p);
        new DialogPositioner(&dialog, true);
        dialog.exec();

        m_settings.m_rgbColor = m_channelMarker.getColor().rgb();
        m_settings.m_title = m_channelMarker.getTitle();
        setWindowTitle(m_settings.m_title);
        setTitle(m_settings.m_title);
        setTitleColor(m_settings.m_rgbColor);
    }

    resetContextMenuType();
}

void CellularRxGUI::rebuildBoxes(const std::vector<int>& ids)
{
    for (CellularCarrierBox *box : m_boxes) {
        delete box;
    }
    m_boxes.clear();
    for (size_t i = 0; i < ids.size(); i++)
    {
        // from the phone: the receiver hears it
        CellularCarrierBox *box = new CellularCarrierBox(m_boxLayout->parentWidget(), CellularSpeechPaint::up());
        box->setFixedWidth(std::max(box->fontMetrics().horizontalAdvance(kWidest) + 12,
            CellularCarrierBox::meterWidth(box->fontMetrics())));
        m_boxLayout->addWidget(box);
        box->show();        // now, not queued as the layout does: fitHeight() needs its size
        m_boxes.push_back(box);
    }
    m_boxIds = ids;
}

// the boxes flow into as many rows as the width needs: the window takes the height of the rows
// (SDRangel fits a channel window to its contents only when a section rolls up or down)
void CellularRxGUI::fitHeight()
{
    m_boxLayout->parentWidget()->updateGeometry();      // its height for the width is cached
    getRollupContents()->arrangeRollups();
    sizeToContents();
    if (isMaximized()) {
        return;
    }
    const int h = std::max(minimumHeight(), std::min(height(), maximumHeight()));
    if (h != height()) {
        resize(width(), h);
    }
}

void CellularRxGUI::resizeEvent(QResizeEvent *event)
{
    ChannelGUI::resizeEvent(event);
    fitHeight();
}

void CellularRxGUI::tick()
{
    if (++m_tickCount % 10 != 0) {          // twice a second
        return;
    }

    const CellularRxBaseband::Status status = m_cellularRx->status();
    const double centre = static_cast<double>(m_cellularRx->getDeviceCenterFrequency());
    QString span = QString("Stream %1 MHz, %2 kS/s").arg(centre / 1e6, 0, 'f', 4).arg(status.sampleRate / 1e3, 0, 'f', 1);

    if (status.sampleRate <= 0.0) {
        span = "The device is not running";
    } else if (!status.supported) {
        span += " - not usable: the rate has to be an even multiple of 48 kHz (960 kS/s, 1.92 MS/s ...)";
    } else if (status.carriers.empty()) {
        span += " - no carriers: start a network in the Cellular feature";
    } else {
        span += QString(" - %1 blocks").arg(status.batches);
    }
    m_span->setText(span);

    std::vector<int> ids;
    for (const modemcellular::CarrierTuning& t : status.carriers) {
        ids.push_back(t.id);
    }
    const bool rebuilt = ids != m_boxIds;
    if (rebuilt) {
        rebuildBoxes(ids);
    }
    const std::map<int, QString> names = m_cellularRx->networkNames();
    const std::map<int, modemcellular::WireSpeech::Carrier> speech = m_cellularRx->speech();
    for (size_t i = 0; i < m_boxes.size() && i < status.carriers.size(); i++)
    {
        const auto s = speech.find(status.carriers[i].id);
        m_boxes[i]->setSpeech(s != speech.end() ? &s->second : nullptr);
    }
    for (size_t i = 0; i < m_boxes.size() && i < status.carriers.size(); i++)
    {
        const modemcellular::CarrierTuning& t = status.carriers[i];
        const float level = i < status.levels.size() ? status.levels[i] : -100.0f;
        const int network = t.id / kIdStride;
        const auto it = names.find(network);
        const QString name = it != names.end() && !it->second.isEmpty() ? it->second : QString("net %1").arg(network);
        CellularCarrierBox *box = m_boxes[i];

        setBoxText(box, name, t.id % kIdStride,
            QString("%1 kHz").arg(t.offset / 1e3, 0, 'f', 1),
            QString("%1 dB").arg(level, 0, 'f', 1));
        box->setToolTip(QString("Network %1 (id %2), channel %3").arg(name).arg(network).arg(t.id % kIdStride));
    }
    if (rebuilt) {
        fitHeight();        // the boxes have their text now, and their height
    }
}
