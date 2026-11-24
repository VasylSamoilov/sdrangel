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

#include "cellularspeech.h"

#include <algorithm>
#include <cmath>

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStringList>
#include <QToolTip>

using modemcellular::SpeechStats;

namespace
{

QString directionText(const SpeechStats& s)
{
    if (s.speech == 0) {
        return QString("no speech, pauses %1 %").arg(100.0 * s.pauseShare(), 0, 'f', 0);
    }
    const std::array<double, SpeechStats::kBands> b = s.bandsDb();
    QStringList bands;
    for (double d : b) {
        bands << QString::number(std::max(d, -40.0), 'f', 0);
    }
    return QString("%1 dB rms, 99 % below %2 dB, %3 % at the limit, peak %4 kHz, pauses %5 %, bands %6 dB")
        .arg(s.levelDb(), 0, 'f', 1).arg(s.p99Db(), 0, 'f', 1).arg(100.0 * s.limitShare(), 0, 'f', 1)
        .arg(3.0 * s.peak, 0, 'f', 1).arg(100.0 * s.pauseShare(), 0, 'f', 0).arg(bands.join(" "));
}

} // namespace

QString CellularSpeechPaint::text(const CellularCallSpeech& call, bool whole)
{
    const SpeechStats& rx = whole ? call.rxCall : call.rxRecent;
    const SpeechStats& tx = whole ? call.txCall : call.txRecent;
    QString head = whole
        ? QString("Last call on channel %1%2 (whole call)").arg(call.channel)
            .arg(call.ended.isValid() ? ", ended " + call.ended.toString("HH:mm:ss") : QString())
        : QString("Call on channel %1, the last 2 s").arg(call.channel);

    return QString("%1\n%2 from the phone (demodulated): %3\n%4 to the phone (to the modulator): %5\n"
        "Limit 4.7 kHz deviation. Transmitter's limiter in the call: %6 blocks")
        .arg(head).arg(up()).arg(directionText(rx)).arg(down()).arg(directionText(tx)).arg(call.limited);
}

QRect CellularSpeechPaint::speakerRect(const QRect& rect)
{
    return QRect(rect.left() + 2, rect.center().y() - 9, 30, 18);
}

// A speaker with its sound waves when on, and what goes to it: L R both sides, an arrow one side.
void CellularSpeechPaint::speaker(QPainter& p, const QRect& r, int listen)
{
    const bool on = listen > 0;
    const QColor line = on ? QColor(0x4f, 0xa3, 0xe0) : QColor(0x5a, 0x61, 0x6a);
    const QColor body = on ? QColor(0xe3, 0xe6, 0xea) : QColor(0x8a, 0x92, 0x9c);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(line);
    p.setBrush(on ? QColor(0x23, 0x4a, 0x66) : Qt::NoBrush);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    const double x = r.left() + 4, y = r.center().y();
    const QPointF cone[] = {{x, y - 2.5}, {x + 3, y - 2.5}, {x + 7, y - 6}, {x + 7, y + 6}, {x + 3, y + 2.5}, {x, y + 2.5}};
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawPolygon(cone, 6);
    if (on)
    {
        p.setPen(QPen(line, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(x + 5, y - 4, 6, 8), -60 * 16, 120 * 16);
        // what goes to the speaker
        const QString mark = listen == 1 ? QString("LR") : (listen == 2 ? up() : down());
        QFont f = p.font();
        f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.75));
        p.setFont(f);
        p.setPen(body);
        p.drawText(QRectF(x + 11, r.top(), r.right() - x - 11, r.height()), Qt::AlignVCenter | Qt::AlignHCenter, mark);
    }
    p.restore();
}

int CellularSpeechPaint::speakerMenu(QWidget *parent, const QPoint& globalPos, int listen)
{
    QMenu menu(parent);
    const QString items[] = {"Off", QString("Both sides: %1 the phone left, %2 the far end right").arg(up()).arg(down()),
        QString("%1 The phone only (mono)").arg(up()), QString("%1 The far end only (mono)").arg(down())};
    QAction *picked[4];
    for (int i = 0; i < 4; i++)
    {
        picked[i] = menu.addAction(items[i]);
        picked[i]->setCheckable(true);
        picked[i]->setChecked(i == listen);
    }
    QAction *chosen = menu.exec(globalPos);
    for (int i = 0; i < 4; i++)
    {
        if (chosen == picked[i]) {
            return i;
        }
    }
    return -1;
}

CellularSpeechLine::CellularSpeechLine(QWidget *parent) :
    QWidget(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    hide();
}

void CellularSpeechLine::show(const CellularSpeechSeen *seen)
{
    if (!seen)
    {
        setVisible(false);
        return;
    }
    m_call = seen->call;
    m_live = seen->live();
    setToolTip(CellularSpeechPaint::text(m_call, !m_live));
    setVisible(true);
    update();
}

void CellularSpeechLine::mousePressEvent(QMouseEvent *event)
{
    if (m_live && CellularSpeechPaint::speakerRect(rect()).contains(event->pos()))
    {
        const int mode = CellularSpeechPaint::speakerMenu(this, event->globalPos(), m_call.listen);
        if (mode >= 0) {
            emit listen(m_call.phone, mode);
        }
        return;
    }
    QWidget::mousePressEvent(event);
}

QSize CellularSpeechLine::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(2 * CellularSpeechPaint::lineWidth(fm) + fm.horizontalAdvance("0000000  ") + 20, CellularSpeechPaint::lineHeight(fm) + 4);
}

QSize CellularSpeechLine::minimumSizeHint() const
{
    return QSize(0, sizeHint().height());
}

void CellularSpeechLine::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QFontMetrics fm(font());
    const int h = CellularSpeechPaint::lineHeight(fm);
    const int y = (height() - h) / 2;
    const bool dim = !m_live;
    const SpeechStats& rx = dim ? m_call.rxCall : m_call.rxRecent;
    const SpeechStats& tx = dim ? m_call.txCall : m_call.txRecent;
    // the phone, then each direction; the text goes first so a narrow panel still shows whose call
    const QString who = m_call.phone + (dim ? " last call" : QString());
    int x = 4;
    if (!dim)
    {
        CellularSpeechPaint::speaker(p, CellularSpeechPaint::speakerRect(rect()), m_call.listen);
        x = CellularSpeechPaint::speakerRect(rect()).right() + 6;
    }
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRect(x, y, fm.horizontalAdvance(who) + 2, h), Qt::AlignVCenter | Qt::AlignLeft, who);
    x += fm.horizontalAdvance(who) + 10;
    const int w = CellularSpeechPaint::lineWidth(fm);
    CellularSpeechPaint::direction(p, QRect(x, y, w, h), CellularSpeechPaint::up(), rx, dim);
    x += w + 8;
    CellularSpeechPaint::direction(p, QRect(x, y, w, h), CellularSpeechPaint::down(), tx, dim);
}

CellularSpeechDelegate::CellularSpeechDelegate(QObject *parent, int numberRole, Lookup lookup) :
    QStyledItemDelegate(parent),
    m_numberRole(numberRole),
    m_lookup(std::move(lookup))
{
}

const CellularSpeechSeen *CellularSpeechDelegate::find(const QModelIndex& index) const
{
    const QString number = index.sibling(index.row(), 0).data(m_numberRole).toString();
    return number.isEmpty() ? nullptr : m_lookup(number);
}

void CellularSpeechDelegate::paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, QModelIndex());     // background and selection only
    const CellularSpeechSeen *seen = find(index);

    if (!seen) {
        return;
    }
    const bool live = seen->live();
    const SpeechStats& rx = live ? seen->call.rxRecent : seen->call.rxCall;
    const SpeechStats& tx = live ? seen->call.txRecent : seen->call.txCall;
    const QFontMetrics fm(option.font);
    const int h = CellularSpeechPaint::lineHeight(fm);
    const int y = option.rect.top() + std::max(0, (option.rect.height() - 2 * h) / 2);
    // the speaker button takes the left of the cell (only a live call has one to press)
    const int left = CellularSpeechPaint::speakerRect(option.rect).right() + 6;
    const QRect line(left, y, option.rect.right() - left, h);

    painter->save();
    painter->setFont(option.font);
    painter->setClipRect(option.rect);
    if (live) {
        CellularSpeechPaint::speaker(*painter, CellularSpeechPaint::speakerRect(option.rect), seen->call.listen);
    }
    CellularSpeechPaint::direction(*painter, line, CellularSpeechPaint::up(), rx, !live);
    CellularSpeechPaint::direction(*painter, line.translated(0, h), CellularSpeechPaint::down(), tx, !live);
    painter->restore();
}

QSize CellularSpeechDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    const QFontMetrics fm(option.font);
    return QSize(CellularSpeechPaint::lineWidth(fm) + 44, 2 * CellularSpeechPaint::lineHeight(fm));
}

bool CellularSpeechDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() == QEvent::MouseButtonRelease && m_speaker)
    {
        const QMouseEvent *mouse = static_cast<QMouseEvent*>(event);
        const CellularSpeechSeen *seen = find(index);

        if (seen && seen->live() && CellularSpeechPaint::speakerRect(option.rect).contains(mouse->pos()))
        {
            m_speaker(index, mouse->globalPos(), seen->call.listen);
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

bool CellularSpeechDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    const CellularSpeechSeen *seen = find(index);

    if (!seen) {
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }
    QToolTip::showText(event->globalPos(), CellularSpeechPaint::text(seen->call, !seen->live()), view);
    return true;
}
