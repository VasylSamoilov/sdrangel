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

#ifndef MODEMCELLULAR_SPEECHBOX_H_
#define MODEMCELLULAR_SPEECHBOX_H_

// The box of a carrier in the Cellular Rx and Cellular Tx channels (Qt Widgets, header only, for
// those GUIs): its text, and under it the call meter of one direction of the call on the carrier
// (the last 2 s during the call, the whole call dimmed after it; empty before the first call).

#include <QLabel>
#include <QPainter>

#include "carrierwire.h"
#include "speechpaint.h"

class CellularCarrierBox : public QLabel
{
public:
    // arrow: the direction the radio carries (receiver: from the phone, transmitter: to it)
    CellularCarrierBox(QWidget *parent, const QString& arrow) :
        QLabel(parent),
        m_arrow(arrow)
    {
        setFrameShape(QFrame::Box);
        setAlignment(Qt::AlignCenter);
    }

    // the lines of the box; an empty line under them takes the meter, there also before a call so
    // the boxes do not grow
    void setLines(const QString& text) { setText(text + "\n "); }

    static int meterWidth(const QFontMetrics& fm) { return CellularSpeechPaint::lineWidth(fm) + 8; }

    void setSpeech(const modemcellular::WireSpeech::Carrier *speech)
    {
        m_has = speech != nullptr;
        if (speech) {
            m_speech = *speech;
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QLabel::paintEvent(event);
        if (!m_has) {
            return;
        }
        QPainter p(this);
        const QFontMetrics fm = fontMetrics();
        const int h = CellularSpeechPaint::lineHeight(fm);
        const int w = CellularSpeechPaint::lineWidth(fm);
        const QRect line((width() - w) / 2, height() - h - 3, w, h);
        CellularSpeechPaint::direction(p, line, m_arrow, m_speech.live ? m_speech.recent : m_speech.call, !m_speech.live);
    }

private:
    QString m_arrow;
    bool m_has = false;
    modemcellular::WireSpeech::Carrier m_speech;
};

#endif // MODEMCELLULAR_SPEECHBOX_H_
