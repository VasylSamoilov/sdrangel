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
#include "speechpaint.h"

// The speech of a phone's call as the GUI keeps it: the last report, and when it came.
struct CellularSpeechSeen
{
    CellularCallSpeech call;
    QDateTime received;
    // a live call whose reports stopped (the network went off) is shown as ended
    bool live() const { return call.live && received.msecsTo(QDateTime::currentDateTime()) < 2000; }
};

class QPainter;

// The drawing of the call meters is shared with the radio channels (speechpaint.h); here the text.
namespace CellularSpeechPaint
{
    // both directions as text, for tooltips (several lines)
    QString text(const CellularCallSpeech& call, bool whole);
    // the speaker button of a live call: its box at the left of `rect`, and its drawing with the
    // listening mode (0 off, 1 both sides, 2 from the phone, 3 to the phone)
    QRect speakerRect(const QRect& rect);
    void speaker(QPainter& painter, const QRect& rect, int listen);
    // the menu of the speaker button; returns the mode picked, -1 if none
    int speakerMenu(QWidget *parent, const QPoint& globalPos, int listen);
}

// The "Speech" column of the Mobiles list: the two directions of the phone's call (its last 2 s),
// or of its last call (the whole call, dimmed); empty if it had none since the start. The phone
// is the number in `numberRole` of the row's first column. Tooltip: the numbers.
class CellularSpeechDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using Lookup = std::function<const CellularSpeechSeen*(const QString& number)>;
    // the speaker button of a row was clicked (a live call)
    using Speaker = std::function<void(const QModelIndex& index, const QPoint& globalPos, int listen)>;
    CellularSpeechDelegate(QObject *parent, int numberRole, Lookup lookup);
    void setSpeaker(Speaker speaker) { m_speaker = std::move(speaker); }
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    void paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem& option, const QModelIndex& index) override;

private:
    const CellularSpeechSeen *find(const QModelIndex& index) const;
    int m_numberRole;
    Lookup m_lookup;
    Speaker m_speaker;
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

signals:
    // the speaker button was clicked and a mode picked (the call shown)
    void listen(const QString& number, int mode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    CellularCallSpeech m_call;
    bool m_live = false;
};

#endif // INCLUDE_FEATURE_CELLULARSPEECH_H_
