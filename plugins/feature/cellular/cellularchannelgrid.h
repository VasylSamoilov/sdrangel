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

// The channels of a country plan as cells, in the order of their frequency, ten in a row (the row
// shows the downlink frequency of its first cell). A click cycles the type of a channel (free, CC,
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

    void refresh();
    void paintCell(int index);
    int cellIndex(int row, int col) const;
    void cycle(int index);
    void menu(const QPoint& pos);

    const modemnmt::NmtCountry *m_country;
    std::vector<Cell> m_cells;              // in the order of the downlink frequency
    QMap<int, int> m_byChannel;             // channel -> index of its cell
    QMap<int, QString> m_others;
    double m_downLow, m_downHigh, m_upLow, m_upHigh;

    static const int kColumns = 10;
};

#endif // INCLUDE_FEATURE_CELLULARCHANNELGRID_H_
