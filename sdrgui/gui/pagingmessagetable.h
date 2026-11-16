///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

#ifndef SDRGUI_GUI_PAGINGMESSAGETABLE_H_
#define SDRGUI_GUI_PAGINGMESSAGETABLE_H_

#include <QByteArray>
#include <QList>
#include <QMenu>
#include <QString>
#include <QTableWidget>

#include "export.h"
#include "util/messagerecord.h"

// The messages of the paging channels and of the message center (POCSAG, FLEX, GSC) as a table: the same
// columns and the same chips for the received and the transmitted messages.
//
// A record is a row, found by its id: the record that completes a message (the end of a voice message, the
// state of a transmitted message, a transmitted copy) updates its row. Each user shows the columns it needs;
// the header menu shows and hides them, and columns() and setColumns() keep the order, the sizes and what is
// hidden. Duplicates are grey, without chips. Double-clicking the row of a voice message plays its file.
class SDRGUI_API PagingMessageTable : public QTableWidget
{
    Q_OBJECT
public:
    enum Column {
        ColTime,
        ColDirection,
        ColChannel,
        ColProtocol,
        ColRate,
        ColPolarity,
        ColAddress,
        ColType,
        ColMessage,
        ColFragment,
        ColInfo,
        ColErrors,
        ColState,
        ColumnCount
    };

    explicit PagingMessageTable(QWidget *parent = nullptr);

    //!< Columns shown when none were saved
    void setDefaultColumns(const QList<Column>& shown);
    //!< The order, the sizes and the hidden columns, as saved by the settings of the user
    QByteArray columns() const;
    bool setColumns(const QByteArray& state);
    //!< Rows kept, the oldest go first
    void setMaxRows(int rows) { m_maxRows = rows; }
    //!< The record is a row, or updates the row of its id. Newest at the bottom: the view follows while it is at the
    //!< bottom. State: the text of the state column (queued, sent, ...), none for the received messages
    void addRecord(const MessageRecord& record, const QString& state = QString());
    //!< Sizes the columns to their usual content (when no sizes were saved)
    void sizeColumns();
    //!< The id of the record of the selected row, empty if none; selects the row of an id
    QString selectedId() const;
    void selectId(const QString& id);
    //!< Plain text of the record and the chips of its protocol fields (HTML), and the tool tip of the chips
    static void recordInfo(const MessageRecord& record, QString& plain, QString& html, QString& tip);
    //!< The text of the row of the record
    static QString recordText(const MessageRecord& record);

signals:
    //!< The user moved, resized, showed or hid a column
    void columnsChanged();

private:
    int m_maxRows = 1000;
    QList<Column> m_defaultColumns;
    QMenu *m_columnMenu;

    int findRow(const QString& id) const;
    void fillRow(int row, const MessageRecord& record, const QString& state);
    void showColumnMenu(const QPoint& pos);
    void showRowMenu(const QPoint& pos);
    void playVoice(int row);
};

#endif // SDRGUI_GUI_PAGINGMESSAGETABLE_H_
