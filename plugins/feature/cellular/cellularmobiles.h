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

#ifndef INCLUDE_FEATURE_CELLULARMOBILES_H_
#define INCLUDE_FEATURE_CELLULARMOBILES_H_

#include <QDialog>
#include <QStyledItemDelegate>

#include <vector>

#include "nmtphonebook.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;

// A phone in the Mobiles table: the item of its first column carries these roles, the delegate
// paints them in two lines (state dot, name and number; then the chips).
namespace CellularMobileRole
{
enum Role
{
    Title = Qt::UserRole + 10,  // QString: the name, or the number when it has none
    Subtitle,                   // QString: the number when there is a name
    StateColour,                // QColor of the dot
    Chips,                      // QStringList "text|background colour"
    Number,                     // QString
    System,                     // int (modemnmt::System)
    Network                     // int: the network it was heard on last, 0 none
};
}

// Two lines per phone; the chips wrap to the width of the column.
class CellularMobileDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit CellularMobileDelegate(QTableView *view);
    void paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    // the chips laid out in rows within `width`; returns the height they take
    int layoutChips(const QStringList& chips, int width, const QFontMetrics& fm, std::vector<QRect> *rects) const;

    QTableView *m_view;
};

// The record of one phone: what was learned (grey mark) and what the user typed (blue mark).
// Only the fields the user changed are taken with OK.
class CellularMobileDialog : public QDialog
{
    Q_OBJECT
public:
    // `live`: what the phone does now, one line per network that heard it
    CellularMobileDialog(const modemnmt::NmtPhoneRecord& record, const QString& live, QWidget *parent = nullptr);

    // the fields the user changed, with their new values (empty: back to learning)
    std::vector<std::pair<modemnmt::NmtPhoneRecord::Field, std::string>> changes() const;

private:
    struct Edit
    {
        modemnmt::NmtPhoneRecord::Field field;
        QLineEdit *line = nullptr;
        QComboBox *combo = nullptr;     // editable, offers numbers it dialled
        QString initial;
    };

    QString text(const Edit& e) const;

    std::vector<Edit> m_edits;
};

#endif // INCLUDE_FEATURE_CELLULARMOBILES_H_
