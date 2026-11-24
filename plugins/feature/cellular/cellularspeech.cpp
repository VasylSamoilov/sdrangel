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
    p.setPen(palette().color(QPalette::WindowText));
    int x = 4;
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
    const QRect line(option.rect.left() + 4, y, option.rect.width() - 6, h);

    painter->save();
    painter->setFont(option.font);
    painter->setClipRect(option.rect);
    CellularSpeechPaint::direction(*painter, line, CellularSpeechPaint::up(), rx, !live);
    CellularSpeechPaint::direction(*painter, line.translated(0, h), CellularSpeechPaint::down(), tx, !live);
    painter->restore();
}

QSize CellularSpeechDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    const QFontMetrics fm(option.font);
    return QSize(CellularSpeechPaint::lineWidth(fm) + 10, 2 * CellularSpeechPaint::lineHeight(fm));
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
