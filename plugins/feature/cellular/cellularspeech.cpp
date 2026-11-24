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
#include <QToolTip>

namespace
{

using modemcellular::SpeechStats;

const int kBinWidth = 4;            // px per level bin, with its gap
const int kPauseWidth = 4;
const int kBandWidth = 6;           // px per band, with its gap
const int kGap = 5;
const double kBandFloorDb = -40.0;  // the spectrum drawing: -40 .. 0 dB of the speech level
// the limit of the call meters: 4.7 kHz of the 3 kHz reference (+3.9 dB)
const double kLimitDb = 20.0 * std::log10(4.7 / 3.0);

QColor binColour(int bin)
{
    const double db = SpeechStats::kLowDb + (bin + 0.5) * (SpeechStats::kHighDb - SpeechStats::kLowDb) / SpeechStats::kBins;

    if (bin == SpeechStats::kBins - 1) {
        return QColor(0xd2, 0x48, 0x3f);        // the bin of the limit
    }
    if (db > kLimitDb - 3.0) {
        return QColor(0xd4, 0x9a, 0x2a);        // within 3 dB of the limit
    }
    if (db < -20.0) {
        return QColor(0x4f, 0x6f, 0x9f);        // quiet
    }
    return QColor(0x3f, 0x9f, 0x6a);
}

int levelWidth()
{
    return kPauseWidth + 3 + SpeechStats::kBins * kBinWidth;
}

int spectrumWidth()
{
    return SpeechStats::kBands * kBandWidth;
}

void paintLevel(QPainter& p, const QRect& r, const SpeechStats& s)
{
    const int h = r.height();
    // pauses: the share of the time
    const int pause = static_cast<int>(std::lround(s.pauseShare() * h));
    p.fillRect(QRect(r.left(), r.bottom() - std::max(pause, 1) + 1, kPauseWidth, std::max(pause, 1)), QColor(0x55, 0x5c, 0x66));

    const uint64_t most = *std::max_element(s.bins.begin(), s.bins.end());
    const int x0 = r.left() + kPauseWidth + 3;
    for (int i = 0; i < SpeechStats::kBins; i++)
    {
        int bh = most ? static_cast<int>(std::ceil(static_cast<double>(s.bins[i]) / static_cast<double>(most) * h)) : 0;
        // samples at the limit always show, however few
        if (i == SpeechStats::kBins - 1 && s.atLimit > 0) {
            bh = std::max(bh, 3);
        }
        if (bh > 0) {
            p.fillRect(QRect(x0 + i * kBinWidth, r.bottom() - bh + 1, kBinWidth - 1, bh), binColour(i));
        }
    }
    // the scale's floor and the limit
    p.setPen(QColor(0x3a, 0x40, 0x48));
    p.drawLine(x0, r.bottom(), x0 + SpeechStats::kBins * kBinWidth - 2, r.bottom());
    const double frac = (kLimitDb - SpeechStats::kLowDb) / (SpeechStats::kHighDb - SpeechStats::kLowDb);
    const int lx = x0 + static_cast<int>(frac * SpeechStats::kBins * kBinWidth) - 1;
    QPen pen(QColor(0xd2, 0x48, 0x3f));
    pen.setStyle(Qt::DotLine);
    p.setPen(pen);
    p.drawLine(lx, r.top(), lx, r.bottom());
}

void paintSpectrum(QPainter& p, const QRect& r, const SpeechStats& s)
{
    const int h = r.height();
    const std::array<double, SpeechStats::kBands> db = s.bandsDb();

    for (int i = 0; i < SpeechStats::kBands; i++)
    {
        if (s.speech == 0) {
            continue;
        }
        const double v = std::max(0.0, std::min(1.0, (db[i] - kBandFloorDb) / -kBandFloorDb));
        const int bh = std::max(1, static_cast<int>(std::lround(v * h)));
        p.fillRect(QRect(r.left() + i * kBandWidth, r.bottom() - bh + 1, kBandWidth - 1, bh), QColor(0x6a, 0x8f, 0xb8));
    }
    p.setPen(QColor(0x3a, 0x40, 0x48));
    p.drawLine(r.left(), r.bottom(), r.left() + spectrumWidth() - 2, r.bottom());
}

QString limitText(const SpeechStats& s)
{
    return s.speech ? QString("%1%").arg(100.0 * s.limitShare(), 0, 'f', 1) : QString("-");
}

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
    return QString("%1 dB rms, 99 % below %2 dB, %3 % at the limit, pauses %4 %, bands %5 dB")
        .arg(s.levelDb(), 0, 'f', 1).arg(s.p99Db(), 0, 'f', 1).arg(100.0 * s.limitShare(), 0, 'f', 1)
        .arg(100.0 * s.pauseShare(), 0, 'f', 0).arg(bands.join(" "));
}

} // namespace

QString CellularSpeechPaint::up()
{
    return QString(QChar(0x2191));
}

QString CellularSpeechPaint::down()
{
    return QString(QChar(0x2193));
}

int CellularSpeechPaint::lineHeight(const QFontMetrics& fm)
{
    return fm.height() + 2;
}

int CellularSpeechPaint::lineWidth(const QFontMetrics& fm)
{
    return fm.horizontalAdvance(down()) + 4 + levelWidth() + kGap + spectrumWidth() + kGap + fm.horizontalAdvance("00.0%");
}

void CellularSpeechPaint::direction(QPainter& p, const QRect& rect, const QString& arrow, const SpeechStats& stats, bool dim)
{
    const QFontMetrics fm = p.fontMetrics();
    const QRect draw(rect.left(), rect.top() + 2, rect.width(), rect.height() - 4);

    p.save();
    if (dim) {
        p.setOpacity(0.55);
    }
    int x = rect.left();
    p.setPen(QColor(0x8a, 0x92, 0x9c));
    p.drawText(QRect(x, rect.top(), fm.horizontalAdvance(arrow) + 2, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, arrow);
    x += fm.horizontalAdvance(down()) + 4;
    paintLevel(p, QRect(x, draw.top(), levelWidth(), draw.height()), stats);
    x += levelWidth() + kGap;
    paintSpectrum(p, QRect(x, draw.top(), spectrumWidth(), draw.height()), stats);
    x += spectrumWidth() + kGap;
    p.setPen(stats.atLimit ? QColor(0xd2, 0x48, 0x3f) : QColor(0x8a, 0x92, 0x9c));
    p.drawText(QRect(x, rect.top(), rect.right() - x, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, limitText(stats));
    p.restore();
}

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
