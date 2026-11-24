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

#include "cellularbandstrip.h"

#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace
{

const int kTop = 14;                // channel numbers above the bars
const int kBottom = 14;             // the frequency axis
const int kSide = 4;

bool isOff(const QString& state)
{
    return state.startsWith("off");
}

bool isBusy(const CellularBandStrip::Carrier& c)
{
    return !isOff(c.state) && (!c.phone.isEmpty() || (!c.state.isEmpty() && c.state != "idle"));
}

} // namespace

CellularBandStrip::CellularBandStrip(QWidget *parent) :
    QWidget(parent)
{
    m_clock.start();
    m_fade.setInterval(40);
    connect(&m_fade, &QTimer::timeout, this, [this]() {
        bool flashing = false;
        for (qint64 at : m_flashAt) {
            flashing = flashing || (m_clock.elapsed() - at < kFlashMs);
        }
        if (!flashing) {
            m_fade.stop();
        }
        update();
    });
    setMinimumHeight(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip("The uplink band: the span of each receiver (green), one bar per carrier at its "
        "uplink frequency, as high as its received level (-120 to 0 dB of full scale). Dim: idle; "
        "full: it serves a phone (its number in the bar); red frame: off. A flash: frames came in");
}

void CellularBandStrip::setData(const QList<QPair<double, double>>& spans, const QList<Carrier>& carriers)
{
    m_spans = spans;
    m_carriers = carriers;

    // frames since the last report: a flash
    const qint64 now = m_clock.elapsed();
    for (const Carrier& c : m_carriers)
    {
        const auto it = m_frames.constFind(c.key);
        if (it != m_frames.constEnd() && c.framesReceived > *it)
        {
            m_flashAt[c.key] = now;
            if (!m_fade.isActive()) {
                m_fade.start();
            }
        }
        m_frames[c.key] = c.framesReceived;
    }

    // the frequencies shown: the spans and the carriers, with a little room
    double low = 0.0, high = 0.0;
    bool first = true;
    auto take = [&](double f) {
        low = first ? f : std::min(low, f);
        high = first ? f : std::max(high, f);
        first = false;
    };
    for (const QPair<double, double>& s : m_spans)
    {
        take(s.first);
        take(s.second);
    }
    for (const Carrier& c : m_carriers) {
        take(c.uplink);
    }
    const double room = std::max(25e3, 0.03 * (high - low));
    m_low = low - room;
    m_high = high + room;
    update();
}

double CellularBandStrip::x(double hz) const
{
    if (m_high <= m_low) {
        return width() / 2.0;
    }
    return kSide + (hz - m_low) / (m_high - m_low) * (width() - 2 * kSide);
}

int CellularBandStrip::carrierAt(const QPoint& pos) const
{
    int best = -1;
    double distance = 6.0;      // pixels

    for (int i = 0; i < m_carriers.size(); i++)
    {
        const double d = std::fabs(x(m_carriers[i].uplink) - pos.x());
        if (d < distance)
        {
            distance = d;
            best = i;
        }
    }
    return best;
}

void CellularBandStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRectF bars(0, kTop, width(), height() - kTop - kBottom);

    p.fillRect(rect(), QColor(0x1a, 0x1a, 0x1a));
    if (m_carriers.isEmpty() && m_spans.isEmpty())
    {
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(rect(), Qt::AlignCenter, "No network on the air");
        return;
    }

    // the spans of the receivers, and lines every 20 dB
    for (const QPair<double, double>& s : m_spans) {
        p.fillRect(QRectF(x(s.first), bars.top(), x(s.second) - x(s.first), bars.height()), QColor(0x20, 0x3a, 0x28));
    }
    p.setPen(QColor(0x30, 0x30, 0x30));
    for (float db = kLevelLow + 20.0f; db < kLevelHigh; db += 20.0f)
    {
        const double y = bars.bottom() - (db - kLevelLow) / (kLevelHigh - kLevelLow) * bars.height();
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    }

    // the bars, as wide as the room between the carriers allows
    double spacing = width();
    QList<double> xs;
    for (const Carrier& c : m_carriers) {
        xs.append(x(c.uplink));
    }
    std::sort(xs.begin(), xs.end());
    for (int i = 1; i < xs.size(); i++)
    {
        if (xs[i] - xs[i - 1] > 0.5) {
            spacing = std::min(spacing, xs[i] - xs[i - 1]);
        }
    }
    const double w = std::max(3.0, std::min(14.0, spacing * 0.7));
    const qint64 now = m_clock.elapsed();
    QFont small = font();
    small.setPointSizeF(std::max(6.0, small.pointSizeF() - 1.5));
    p.setFont(small);
    double labelEnd = -1e9;

    for (const Carrier& c : m_carriers)
    {
        const double cx = x(c.uplink);
        const QRectF column(cx - w / 2.0, bars.top(), w, bars.height());

        if (isOff(c.state))
        {
            p.setPen(QPen(QColor(0xff, 0x60, 0x60), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(column.adjusted(0.5, 0.5, -0.5, -0.5));
        }
        else
        {
            const float level = std::max(kLevelLow, std::min(kLevelHigh, c.rxLevel));
            const double h = (level - kLevelLow) / (kLevelHigh - kLevelLow) * bars.height();
            const QColor colour = c.colour.isValid() ? c.colour : QColor(0x80, 0x80, 0x80);
            p.fillRect(QRectF(column.left(), bars.bottom() - h, w, h), isBusy(c) ? colour.lighter(130) : colour.darker(160));
        }

        // frames came in: a white frame that fades
        const auto flash = m_flashAt.constFind(c.key);
        if (flash != m_flashAt.constEnd() && now - *flash < kFlashMs)
        {
            QColor white(Qt::white);
            white.setAlphaF(1.0 - static_cast<double>(now - *flash) / kFlashMs);
            p.setPen(QPen(white, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(column.adjusted(-1, -1, 1, 1));
        }

        // the phone's number in the bar, from the foot up
        if (!c.phone.isEmpty() && w >= 8.0)
        {
            p.save();
            p.translate(cx + small.pointSizeF() / 2.0, bars.bottom() - 2);
            p.rotate(-90);
            p.setPen(Qt::white);
            p.drawText(QPointF(0, 0), c.phone);
            p.restore();
        }

        // the channel number above, if there is room
        const QString label = QString::number(c.channel);
        const double lw = p.fontMetrics().horizontalAdvance(label);
        if (cx - lw / 2.0 > labelEnd + 2.0)
        {
            p.setPen(isBusy(c) ? Qt::white : QColor(0xaa, 0xaa, 0xaa));
            p.drawText(QPointF(cx - lw / 2.0, kTop - 3), label);
            labelEnd = cx + lw / 2.0;
        }
    }

    // the frequency axis: a round step for about one label per 80 pixels
    const double span = m_high - m_low;
    if (span > 0.0)
    {
        const double steps[] = {1e3, 2.5e3, 5e3, 10e3, 25e3, 50e3, 100e3, 250e3, 500e3, 1e6, 2.5e6, 5e6};
        double step = steps[0];
        for (double s : steps)
        {
            step = s;
            if (span / s <= std::max(1.0, width() / 80.0)) {
                break;
            }
        }
        // as many decimals of MHz as the step has (0.25 MHz: 2)
        int decimals = 0;
        while (decimals < 4 && std::fabs(step / 1e6 * std::pow(10.0, decimals)
            - std::round(step / 1e6 * std::pow(10.0, decimals))) > 1e-6) {
            decimals++;
        }
        p.setPen(QColor(0xaa, 0xaa, 0xaa));
        for (double f = std::ceil(m_low / step) * step; f <= m_high; f += step)
        {
            const QString text = QString::number(f / 1e6, 'f', decimals);
            const double tw = p.fontMetrics().horizontalAdvance(text);
            const double tx = std::max(0.0, std::min(width() - tw, x(f) - tw / 2.0));
            p.drawText(QPointF(tx, height() - 3), text);
        }
    }
}

bool CellularBandStrip::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip)
    {
        QHelpEvent *help = static_cast<QHelpEvent*>(event);
        const int i = carrierAt(help->pos());
        if (i >= 0)
        {
            const Carrier& c = m_carriers[i];
            QString text = QString("%1 channel %2 (%3), up %4 MHz: %5")
                .arg(c.network).arg(c.channel).arg(c.type).arg(c.uplink / 1e6, 0, 'f', 4).arg(c.state);
            if (!isOff(c.state)) {
                text += QString(", %1 dB, %2 frames received").arg(c.rxLevel, 0, 'f', 1).arg(c.framesReceived);
            }
            if (!c.phone.isEmpty()) {
                text += QString(", phone %1").arg(c.phone);
            }
            QToolTip::showText(help->globalPos(), text, this);
        } else {
            QToolTip::showText(help->globalPos(), toolTip(), this);
        }
        return true;
    }
    return QWidget::event(event);
}
