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

#ifndef INCLUDE_FEATURE_CELLULARBANDSTRIP_H_
#define INCLUDE_FEATURE_CELLULARBANDSTRIP_H_

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QPair>
#include <QTimer>
#include <QWidget>

// The uplink band as a strip, for watching the networks: the span of each receiver behind, one
// bar per carrier at its uplink frequency, as high as its received level, in the colour of its
// type (dim when idle, full when it serves a phone or does something, a red frame when it is
// off), the channel number above and the phone's number in it; a flash when frames came in since
// the last report. The tooltip of a bar gives its values.
class CellularBandStrip : public QWidget
{
    Q_OBJECT
public:
    struct Carrier
    {
        int key = 0;                // unique over the networks (network id * 10000 + channel)
        QString network;
        int channel = 0;
        QString type;
        QColor colour;
        double uplink = 0.0;        // Hz
        QString state;              // "idle", other states, or "off: ..."
        float rxLevel = -100.0f;    // dB of full scale
        QString phone;
        quint64 framesReceived = 0;
    };

    explicit CellularBandStrip(QWidget *parent = nullptr);

    // spans of the receivers in Hz (low, high); the carriers of all networks
    void setData(const QList<QPair<double, double>>& spans, const QList<Carrier>& carriers);

    QSize sizeHint() const override { return QSize(360, 96); }
    QSize minimumSizeHint() const override { return QSize(120, 80); }

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    double x(double hz) const;
    int carrierAt(const QPoint& pos) const;

    static constexpr float kLevelLow = -120.0f;     // the foot of the bars, dB of full scale
    static constexpr float kLevelHigh = 0.0f;
    static constexpr int kFlashMs = 400;

    QList<QPair<double, double>> m_spans;
    QList<Carrier> m_carriers;
    double m_low = 0.0;             // the frequencies shown
    double m_high = 0.0;
    QHash<int, quint64> m_frames;   // frames of each carrier at the last report
    QHash<int, qint64> m_flashAt;   // ms of the clock at the last flash
    QElapsedTimer m_clock;
    QTimer m_fade;
};

#endif // INCLUDE_FEATURE_CELLULARBANDSTRIP_H_
