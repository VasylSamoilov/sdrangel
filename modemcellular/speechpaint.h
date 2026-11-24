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

#ifndef MODEMCELLULAR_SPEECHPAINT_H_
#define MODEMCELLULAR_SPEECHPAINT_H_

// The drawing of a call meter, for the GUIs of the Cellular feature and its radio channels (Qt
// Gui; the module itself does not use it). Header only, so the plugins share it without a library.

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>
#include <QString>

#include "speechmeter.h"

// One direction of a call's speech: the level drawing (a grey bar for the share of pauses, then
// where the speech samples landed from -40 to +4 dB of the reference, the red line at the limit)
// and the spectrum drawing (six bands of the telephone band, -40..0 dB of the speech level), then
// the share at the limit.
namespace CellularSpeechPaint
{

namespace detail
{
    const int kBinWidth = 4;            // px per level bin, with its gap
    const int kPauseWidth = 4;
    const int kBandWidth = 6;           // px per band, with its gap
    const int kGap = 5;
    const double kBandFloorDb = -40.0;

    // the limit of the call meters: 4.7 kHz of the 3 kHz reference (+3.9 dB)
    inline double limitDb() { return 20.0 * std::log10(4.7 / 3.0); }

    inline QColor binColour(int bin)
    {
        using modemcellular::SpeechStats;
        const double db = SpeechStats::kLowDb + (bin + 0.5) * (SpeechStats::kHighDb - SpeechStats::kLowDb) / SpeechStats::kBins;

        if (bin == SpeechStats::kBins - 1) {
            return QColor(0xd2, 0x48, 0x3f);        // the bin of the limit
        }
        if (db > limitDb() - 3.0) {
            return QColor(0xd4, 0x9a, 0x2a);        // within 3 dB of the limit
        }
        if (db < -20.0) {
            return QColor(0x4f, 0x6f, 0x9f);        // quiet
        }
        return QColor(0x3f, 0x9f, 0x6a);
    }

    inline int levelWidth() { return kPauseWidth + 3 + modemcellular::SpeechStats::kBins * kBinWidth; }
    inline int spectrumWidth() { return modemcellular::SpeechStats::kBands * kBandWidth; }

    inline void paintLevel(QPainter& p, const QRect& r, const modemcellular::SpeechStats& s)
    {
        using modemcellular::SpeechStats;
        const int h = r.height();
        const int pause = std::max(1, static_cast<int>(std::lround(s.pauseShare() * h)));
        p.fillRect(QRect(r.left(), r.bottom() - pause + 1, kPauseWidth, pause), QColor(0x55, 0x5c, 0x66));

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
        p.setPen(QColor(0x3a, 0x40, 0x48));
        p.drawLine(x0, r.bottom(), x0 + SpeechStats::kBins * kBinWidth - 2, r.bottom());
        const double frac = (limitDb() - SpeechStats::kLowDb) / (SpeechStats::kHighDb - SpeechStats::kLowDb);
        const int lx = x0 + static_cast<int>(frac * SpeechStats::kBins * kBinWidth) - 1;
        QPen pen(QColor(0xd2, 0x48, 0x3f));
        pen.setStyle(Qt::DotLine);
        p.setPen(pen);
        p.drawLine(lx, r.top(), lx, r.bottom());
    }

    inline void paintSpectrum(QPainter& p, const QRect& r, const modemcellular::SpeechStats& s)
    {
        using modemcellular::SpeechStats;
        const int h = r.height();
        const std::array<double, SpeechStats::kBands> db = s.bandsDb();

        for (int i = 0; i < SpeechStats::kBands && s.speech > 0; i++)
        {
            const double v = std::max(0.0, std::min(1.0, (db[i] - kBandFloorDb) / -kBandFloorDb));
            const int bh = std::max(1, static_cast<int>(std::lround(v * h)));
            p.fillRect(QRect(r.left() + i * kBandWidth, r.bottom() - bh + 1, kBandWidth - 1, bh), QColor(0x6a, 0x8f, 0xb8));
        }
        p.setPen(QColor(0x3a, 0x40, 0x48));
        p.drawLine(r.left(), r.bottom(), r.left() + spectrumWidth() - 2, r.bottom());
    }

    inline QString limitText(const modemcellular::SpeechStats& s)
    {
        return s.speech ? QString("%1%").arg(100.0 * s.limitShare(), 0, 'f', 1) : QString("-");
    }
} // namespace detail

// the arrows of the two directions: from the phone, to the phone
inline QString up() { return QString(QChar(0x2191)); }
inline QString down() { return QString(QChar(0x2193)); }

// the height of one direction for a font
inline int lineHeight(const QFontMetrics& fm) { return fm.height() + 2; }

// the width it needs
inline int lineWidth(const QFontMetrics& fm)
{
    return fm.horizontalAdvance(down()) + 4 + detail::levelWidth() + detail::kGap + detail::spectrumWidth()
        + detail::kGap + fm.horizontalAdvance("00.0%");
}

// one direction in `rect`: arrow, level, spectrum, share at the limit; dim: a call that ended
inline void direction(QPainter& p, const QRect& rect, const QString& arrow, const modemcellular::SpeechStats& stats, bool dim)
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
    detail::paintLevel(p, QRect(x, draw.top(), detail::levelWidth(), draw.height()), stats);
    x += detail::levelWidth() + detail::kGap;
    detail::paintSpectrum(p, QRect(x, draw.top(), detail::spectrumWidth(), draw.height()), stats);
    x += detail::spectrumWidth() + detail::kGap;
    p.setPen(stats.atLimit ? QColor(0xd2, 0x48, 0x3f) : QColor(0x8a, 0x92, 0x9c));
    p.drawText(QRect(x, rect.top(), rect.right() - x, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, detail::limitText(stats));
    p.restore();
}

} // namespace CellularSpeechPaint

#endif // MODEMCELLULAR_SPEECHPAINT_H_
