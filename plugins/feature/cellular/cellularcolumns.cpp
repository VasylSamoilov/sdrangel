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

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QTableWidget>

#include "cellularcolumns.h"

void CellularColumns::setup(QTableWidget *table, QList<int> *indexes, QList<int> *sizes, const QStringList& sample)
{
    QHeaderView *header = table->horizontalHeader();
    QMenu *menu = new QMenu(table);

    header->setSectionsMovable(true);
    header->setSectionResizeMode(QHeaderView::Interactive);
    // the widths of the columns from a sample row, as SDRangel's tables size themselves
    const int row = table->rowCount();
    table->insertRow(row);
    for (int col = 0; col < sample.size() && col < table->columnCount(); col++) {
        table->setItem(row, col, new QTableWidgetItem(sample[col]));
    }
    table->resizeColumnsToContents();
    table->removeRow(row);

    for (int col = 0; col < table->columnCount(); col++)
    {
        QAction *action = menu->addAction(table->horizontalHeaderItem(col)->text());
        action->setCheckable(true);
        action->setChecked(true);
        QObject::connect(action, &QAction::triggered, table, [table, col](bool checked) {
            table->setColumnHidden(col, !checked);
        });
    }
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(header, &QHeaderView::customContextMenuRequested, menu, [header, menu](const QPoint& pos) {
        menu->popup(header->viewport()->mapToGlobal(pos));
    });

    apply(table, *indexes, *sizes);
    // a move shifts the columns between its two places too: keep the place of every column
    QObject::connect(header, &QHeaderView::sectionMoved, table, [header, indexes]() {
        for (int col = 0; col < indexes->size() && col < header->count(); col++) {
            (*indexes)[col] = header->visualIndex(col);
        }
    });
    QObject::connect(header, &QHeaderView::sectionResized, table, [sizes](int logical, int, int size) {
        if (logical >= 0 && logical < sizes->size()) {
            (*sizes)[logical] = size;
        }
    });
}

void CellularColumns::apply(QTableWidget *table, const QList<int>& indexes, const QList<int>& sizes)
{
    QHeaderView *header = table->horizontalHeader();
    QMenu *menu = table->findChild<QMenu*>();

    // copies: moving and resizing the sections reports back into the settings
    const QList<int> order = indexes;
    const QList<int> widths = sizes;
    for (int col = 0; col < table->columnCount() && col < widths.size(); col++)
    {
        const bool hidden = widths[col] == 0;
        header->setSectionHidden(col, hidden);
        if (menu && col < menu->actions().size()) {
            menu->actions().at(col)->setChecked(!hidden);
        }
        if (widths[col] > 0) {
            table->setColumnWidth(col, widths[col]);
        }
    }
    // place by place from the left: a column moved in does not shift those placed before it
    for (int visual = 0; visual < table->columnCount(); visual++)
    {
        const int col = order.indexOf(visual);
        if (col >= 0 && col < table->columnCount()) {
            header->moveSection(header->visualIndex(col), visual);
        }
    }
}
