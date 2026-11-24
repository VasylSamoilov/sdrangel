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

#ifndef INCLUDE_FEATURE_CELLULARCHANNELGRID_H_
#define INCLUDE_FEATURE_CELLULARCHANNELGRID_H_

#include <QMap>
#include <QTableWidget>

#include <vector>

#include "nmtbasestation.h"
#include "nmtplan.h"

// The channels of a country plan as cells, ten in a row (the row shows the downlink frequency of its
// first cell), in sections by the group of the channels (ordinary, extension, interleaved,
// interleaved extension), each in the order of the frequency. A click on the header of a section
// opens or closes it; the ordinary channels, the interleaved ones of NMT-900 (the phones have them)
// and a section with carriers start open. A click on a cell cycles the type of a channel (free, CC,
// TC, CC/TC); right click: any type and the supervisory tone of a traffic channel. Channels of other
// networks are grey, channels outside the span of the devices dimmed.
class CellularChannelGrid : public QTableWidget
{
    Q_OBJECT
public:
    explicit CellularChannelGrid(QWidget *parent = nullptr);

    // the plan; the carriers that are not in it are dropped
    void setPlan(modemnmt::System system, const QString& country);
    void setCarriers(const std::vector<modemnmt::NmtCarrierConfig>& carriers);
    std::vector<modemnmt::NmtCarrierConfig> carriers() const;
    // channels another network on the same devices uses, with its name
    void setOthers(const QMap<int, QString>& others);
    // the spans of the devices in Hz (0, 0: not known): downlink of the transmitter, uplink of the receiver
    void setSpans(double downLow, double downHigh, double upLow, double upHigh);
    const modemnmt::NmtCountry *country() const { return m_country; }

    static QString typeText(modemnmt::ChannelType type);
    // the colour of a type by its text (typeText), as the grid shows it; invalid if unknown
    static QColor typeColourOf(const QString& text);

signals:
    void changed();

private:
    struct Cell
    {
        int channel = 0;
        modemnmt::NmtChannelFrequency frequency;
        bool used = false;
        modemnmt::NmtCarrierConfig carrier;
    };

    // the cells of one group: m_cells[first, first + count)
    struct Section
    {
        modemnmt::NmtChannelGroup group;
        int first = 0;
        int count = 0;
        int headerRow = -1;                 // -1: no header (a plan of one group)
        bool open = true;
        QString title;
        QString tip;
    };

    void layOut();
    void showSection(int section);
    void paintHeader(int section);
    void refresh();
    void paintCell(int index);
    void cellChanged(int index);            // after a click or the menu: the cell, its header, changed()
    int cellIndex(int row, int col) const;
    void cycle(int index);
    void menu(const QPoint& pos);
    void fitColumns();
    void resizeEvent(QResizeEvent *event) override;
    // a header is drawn from the left of what is visible: scrolling sideways repaints it all
    void scrollContentsBy(int dx, int dy) override;

    const modemnmt::NmtCountry *m_country;
    int m_minColumn;                        // px: the widest channel number of the plan
    std::vector<Cell> m_cells;              // by section, in each in the order of the downlink frequency
    std::vector<Section> m_sections;
    std::vector<int> m_rowFirst;            // the first cell of a row, -1 for a header
    std::vector<int> m_rowSection;
    std::vector<int> m_cellRow;
    QMap<int, int> m_byChannel;             // channel -> index of its cell
    QMap<int, QString> m_others;
    double m_downLow, m_downHigh, m_upLow, m_upHigh;

    static const int kColumns = 10;
};

#endif // INCLUDE_FEATURE_CELLULARCHANNELGRID_H_
