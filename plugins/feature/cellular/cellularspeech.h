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

#ifndef INCLUDE_FEATURE_CELLULARSPEECH_H_
#define INCLUDE_FEATURE_CELLULARSPEECH_H_

#include <functional>

#include <QDateTime>
#include <QString>
#include <QStyledItemDelegate>
#include <QWidget>

#include "cellular.h"

// The speech of a phone's call as the GUI keeps it: the last report, and when it came.
struct CellularSpeechSeen
{
    CellularCallSpeech call;
    QDateTime received;
    // a live call whose reports stopped (the network went off) is shown as ended
    bool live() const { return call.live && received.msecsTo(QDateTime::currentDateTime()) < 2000; }
};

class QPainter;

// The call meters: for one direction of a call's speech, the level drawing (a grey bar for the
// share of pauses, then where the speech samples landed from -40 to +4 dB of the reference, the
// red line at the limit) and the spectrum drawing (six bands of the telephone band relative to
// the speech level, -30..+0 dB).
namespace CellularSpeechPaint
{
    // the arrows of the two directions: from the phone, to the phone
    QString up();
    QString down();
    // one direction in `rect`: arrow, level, spectrum, share at the limit; dim: a call that ended
    void direction(QPainter& painter, const QRect& rect, const QString& arrow, const modemcellular::SpeechStats& stats, bool dim);
    // the height of one direction for a font
    int lineHeight(const QFontMetrics& fm);
    // the width it needs
    int lineWidth(const QFontMetrics& fm);
    // both directions as text, for tooltips and the record (several lines)
    QString text(const CellularCallSpeech& call, bool whole);
}

// The "Speech" column of the Mobiles list: the two directions of the phone's call (its last 2 s),
// or of its last call (the whole call, dimmed); empty if it had none since the start. The phone
// is the number in `numberRole` of the row's first column. Tooltip: the numbers.
class CellularSpeechDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using Lookup = std::function<const CellularSpeechSeen*(const QString& number)>;
    CellularSpeechDelegate(QObject *parent, int numberRole, Lookup lookup);
    void paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem& option, const QModelIndex& index) override;

private:
    const CellularSpeechSeen *find(const QModelIndex& index) const;
    int m_numberRole;
    Lookup m_lookup;
};

// The status line: the call the operator follows (the selected phone, or the only call) with its
// last 2 s both ways, or the last call of the selected phone (whole call, dimmed). Hidden when
// there is nothing to show.
class CellularSpeechLine : public QWidget
{
    Q_OBJECT

public:
    explicit CellularSpeechLine(QWidget *parent = nullptr);
    void show(const CellularSpeechSeen *seen);      // null: hidden

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    CellularCallSpeech m_call;
    bool m_live = false;
};

#endif // INCLUDE_FEATURE_CELLULARSPEECH_H_
