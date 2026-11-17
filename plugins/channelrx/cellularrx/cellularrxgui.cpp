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

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "device/deviceuiset.h"
#include "gui/basicchannelsettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "gui/rollupcontents.h"
#include "maincore.h"
#include "plugin/pluginapi.h"

#include "cellularrx.h"
#include "cellularrxgui.h"

namespace
{

// carrier ids are network * 10000 + channel number
const int kIdStride = 10000;
const int kColumns = 4;

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
    m_boxLayout = new QGridLayout(boxes);
    m_boxLayout->setContentsMargins(0, 0, 0, 0);
    m_boxLayout->setSpacing(3);
    layout->addWidget(boxes);
    carriers->setMinimumWidth(360);
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
    for (QLabel *box : m_boxes) {
        delete box;
    }
    m_boxes.clear();
    for (size_t i = 0; i < ids.size(); i++)
    {
        QLabel *box = new QLabel(m_boxLayout->parentWidget());
        box->setFrameShape(QFrame::Box);
        box->setMinimumWidth(80);
        box->setAlignment(Qt::AlignCenter);
        m_boxLayout->addWidget(box, static_cast<int>(i) / kColumns, static_cast<int>(i) % kColumns);
        m_boxes.push_back(box);
    }
    m_boxIds = ids;
    getRollupContents()->arrangeRollups();
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
    if (ids != m_boxIds) {
        rebuildBoxes(ids);
    }
    for (size_t i = 0; i < m_boxes.size() && i < status.carriers.size(); i++)
    {
        const modemcellular::CarrierTuning& t = status.carriers[i];
        const float level = i < status.levels.size() ? status.levels[i] : -100.0f;

        m_boxes[i]->setText(QString("net %1 ch %2\n%3 kHz\n%4 dB")
            .arg(t.id / kIdStride)
            .arg(t.id % kIdStride)
            .arg(t.offset / 1e3, 0, 'f', 1)
            .arg(level, 0, 'f', 1));
    }
}
