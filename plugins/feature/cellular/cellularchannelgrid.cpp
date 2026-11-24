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

#include <algorithm>
#include <cmath>

#include <QFontMetricsF>
#include <QHeaderView>
#include <QMenu>

#include "cellularchannelgrid.h"

const int CellularChannelGrid::kColumns;

namespace
{

QColor typeColour(modemnmt::ChannelType type)
{
    using T = modemnmt::ChannelType;
    switch (type)
    {
    case T::Cc: return QColor(0xc0, 0x7a, 0x1a);
    case T::Cca:
    case T::Ccb: return QColor(0xa0, 0x5a, 0x2a);
    case T::Tc: return QColor(0x2f, 0x6d, 0xb3);
    case T::AcTc: return QColor(0x2f, 0x8f, 0xa3);
    case T::CcTc: return QColor(0x7a, 0x4f, 0xb3);
    case T::Test: return QColor(0x3a, 0x7a, 0x3a);
    }
    return QColor();
}

bool isTraffic(modemnmt::ChannelType type)
{
    return type == modemnmt::ChannelType::Tc || type == modemnmt::ChannelType::AcTc || type == modemnmt::ChannelType::CcTc;
}

// what the standard says of a group, for the tooltip of its header
QString groupTip(modemnmt::System system, modemnmt::NmtChannelGroup group, double spacingKHz)
{
    using G = modemnmt::NmtChannelGroup;
    switch (group)
    {
    case G::Ordinary:
        return QString("Ordinary channels, %1 kHz apart").arg(spacingKHz, 0, 'g', 3);
    case G::Extension:
        return "The extension band: optional, subject to national regulation (NMT Doc 450-1)";
    case G::Interleaved:
        if (system == modemnmt::System::Nmt900) {
            return "12.5 kHz from the ordinary channels, channel numbers from 1024 up: the phones have them (NMT Doc 900-3)";
        }
        return "12.5 kHz above the ordinary channels, channel number + 200: optional (NMT Doc 450-1)";
    case G::InterleavedExtension:
        return "12.5 kHz above the extension channels, channel number + 200: optional (NMT Doc 450-1, 1997)";
    }
    return QString();
}

} // namespace

QString CellularChannelGrid::typeText(modemnmt::ChannelType type)
{
    using T = modemnmt::ChannelType;
    switch (type)
    {
    case T::Cc: return "CC";
    case T::Cca: return "CC A";
    case T::Ccb: return "CC B";
    case T::Tc: return "TC";
    case T::AcTc: return "AC/TC";
    case T::CcTc: return "CC/TC";
    case T::Test: return "test";
    }
    return QString();
}

QColor CellularChannelGrid::typeColourOf(const QString& text)
{
    using T = modemnmt::ChannelType;
    for (T type : {T::Cc, T::Cca, T::Ccb, T::Tc, T::AcTc, T::CcTc, T::Test})
    {
        if (typeText(type) == text) {
            return typeColour(type);
        }
    }
    return QColor();
}

CellularChannelGrid::CellularChannelGrid(QWidget *parent) :
    QTableWidget(parent),
    m_country(nullptr),
    m_minColumn(20),
    m_downLow(0.0),
    m_downHigh(0.0),
    m_upLow(0.0),
    m_upHigh(0.0)
{
    setColumnCount(kColumns);
    horizontalHeader()->setVisible(false);
    horizontalHeader()->setDefaultSectionSize(27);
    verticalHeader()->setDefaultSectionSize(17);
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    setFont(small);
    // the columns share the width there is (a small screen has no room for 27 px each), but not
    // below the widest channel number of the plan: then the grid scrolls (fitColumns)
    horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    verticalHeader()->setFont(small);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::NoSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setMinimumHeight(120);

    connect(this, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (row >= 0 && row < static_cast<int>(m_rowFirst.size()) && m_rowFirst[row] < 0)
        {
            Section& s = m_sections[m_rowSection[row]];
            s.open = !s.open;
            showSection(m_rowSection[row]);
            return;
        }
        const int index = cellIndex(row, col);
        if (index >= 0) {
            cycle(index);
        }
    });
    connect(this, &QTableWidget::customContextMenuRequested, this, &CellularChannelGrid::menu);
}

void CellularChannelGrid::setPlan(modemnmt::System system, const QString& country)
{
    const std::vector<modemnmt::NmtCarrierConfig> kept = carriers();

    m_country = modemnmt::nmtCountry(system, country.toStdString());
    m_cells.clear();
    m_sections.clear();
    m_byChannel.clear();
    if (m_country)
    {
        // the groups in the order of the plan; the blocks of a group form one section
        std::vector<modemnmt::NmtChannelGroup> groups;
        for (const modemnmt::NmtFrequencyBlock& block : m_country->blocks)
        {
            if (std::find(groups.begin(), groups.end(), block.group) == groups.end()) {
                groups.push_back(block.group);
            }
        }
        for (modemnmt::NmtChannelGroup group : groups)
        {
            Section section;
            QStringList ranges;
            double spacing = 0.0;
            section.group = group;
            section.first = static_cast<int>(m_cells.size());
            for (const modemnmt::NmtFrequencyBlock& block : m_country->blocks)
            {
                if (block.group != group) {
                    continue;
                }
                ranges.append(QString("%1%2%3").arg(block.firstChannel).arg(QChar(0x2013)).arg(block.lastChannel));
                spacing = std::abs(block.spacing) * 1000.0;
                for (int ch = block.firstChannel; ch <= block.lastChannel; ch++)
                {
                    Cell cell;
                    cell.channel = ch;
                    if (modemnmt::nmtChannelFrequency(*m_country, ch, cell.frequency)) {
                        m_cells.push_back(cell);
                    }
                }
            }
            std::stable_sort(m_cells.begin() + section.first, m_cells.end(), [](const Cell& a, const Cell& b) {
                return a.frequency.downlink < b.frequency.downlink;
            });
            std::sort(ranges.begin(), ranges.end(), [](const QString& a, const QString& b) {
                return a.section(QChar(0x2013), 0, 0).toInt() < b.section(QChar(0x2013), 0, 0).toInt();
            });
            section.count = static_cast<int>(m_cells.size()) - section.first;
            section.title = QString("%1  %2").arg(modemnmt::nmtGroupName(group)).arg(ranges.join(", "));
            section.tip = groupTip(system, group, spacing);
            section.open = group == modemnmt::NmtChannelGroup::Ordinary
                || (system == modemnmt::System::Nmt900 && group == modemnmt::NmtChannelGroup::Interleaved);
            m_sections.push_back(section);
        }
    }
    // the text is laid out with fractional advances (2023 is 22.19 px in the small Liberation
    // Sans): the integer width (22) would leave a column too narrow by a fraction, and elided
    const QFontMetricsF metrics(font());
    int widest = 0;
    for (size_t i = 0; i < m_cells.size(); i++)
    {
        m_byChannel[m_cells[i].channel] = static_cast<int>(i);
        widest = std::max(widest, static_cast<int>(std::ceil(metrics.horizontalAdvance(QString::number(m_cells[i].channel)))));
    }
    // the margins the style puts around the text of an item, and the grid line
    const int margins = 2 * (style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, this) + 1) + 1;
    m_minColumn = std::max(20, widest + margins);
    fitColumns();
    layOut();
    setCarriers(kept);
}

// the rows: per section its header (when there are several) and its cells, ten in a row
void CellularChannelGrid::layOut()
{
    const bool headers = m_sections.size() > 1;
    QStringList labels;

    m_rowFirst.clear();
    m_rowSection.clear();
    m_cellRow.assign(m_cells.size(), -1);
    for (size_t s = 0; s < m_sections.size(); s++)
    {
        Section& section = m_sections[s];
        section.headerRow = -1;
        if (headers)
        {
            section.headerRow = static_cast<int>(m_rowFirst.size());
            m_rowFirst.push_back(-1);
            m_rowSection.push_back(static_cast<int>(s));
            labels.append(QString());
        }
        for (int i = 0; i < section.count; i += kColumns)
        {
            const int first = section.first + i;
            for (int c = 0; c < kColumns && i + c < section.count; c++) {
                m_cellRow[first + c] = static_cast<int>(m_rowFirst.size());
            }
            m_rowFirst.push_back(first);
            m_rowSection.push_back(static_cast<int>(s));
            // MHz with the decimals it has, at least two: 463.00, 462.5125 (interleaved)
            QString mhz = QString::number(m_cells[first].frequency.downlink / 1e6, 'f', 4);
            while (mhz.endsWith('0') && mhz.indexOf('.') < mhz.size() - 3) {
                mhz.chop(1);
            }
            labels.append(mhz);
        }
    }

    clearSpans();
    setRowCount(0);                         // drops the items of the plan before
    setRowCount(static_cast<int>(m_rowFirst.size()));
    setVerticalHeaderLabels(labels);
    for (int r = 0; r < rowCount(); r++)
    {
        if (m_rowFirst[r] < 0)
        {
            setSpan(r, 0, 1, kColumns);
            QTableWidgetItem *header = new QTableWidgetItem();
            header->setFlags(Qt::ItemIsEnabled);
            QFont bold = font();
            bold.setBold(true);
            header->setFont(bold);
            setItem(r, 0, header);
            continue;
        }
        const Section& section = m_sections[m_rowSection[r]];
        for (int c = 0; c < kColumns; c++)
        {
            if (m_rowFirst[r] + c >= section.first + section.count)
            {
                QTableWidgetItem *empty = new QTableWidgetItem();
                empty->setFlags(Qt::NoItemFlags);
                setItem(r, c, empty);
            }
        }
    }
    for (size_t s = 0; s < m_sections.size(); s++) {
        showSection(static_cast<int>(s));
    }
}

void CellularChannelGrid::showSection(int section)
{
    const Section& s = m_sections[section];

    for (int r = 0; r < rowCount(); r++)
    {
        if (m_rowSection[r] == section && m_rowFirst[r] >= 0) {
            setRowHidden(r, !s.open);
        }
    }
    paintHeader(section);
}

void CellularChannelGrid::paintHeader(int section)
{
    const Section& s = m_sections[section];
    QTableWidgetItem *header = s.headerRow >= 0 ? item(s.headerRow, 0) : nullptr;

    if (!header) {
        return;
    }
    int used = 0;
    for (int i = s.first; i < s.first + s.count; i++) {
        used += m_cells[i].used ? 1 : 0;
    }
    // a triangle pointing down when open, right when closed
    QString text = QString("%1 %2").arg(QChar(s.open ? 0x25BE : 0x25B8)).arg(s.title);
    if (used > 0) {
        text += QString("  (%1 used)").arg(used);
    }
    header->setText(text);
    header->setToolTip(s.tip + (s.open ? ". Click to close." : ". Click to open."));
}

void CellularChannelGrid::fitColumns()
{
    const int width = std::max(m_minColumn, viewport()->width() / kColumns);

    for (int c = 0; c < kColumns; c++) {
        setColumnWidth(c, width);
    }
}

void CellularChannelGrid::resizeEvent(QResizeEvent *event)
{
    QTableWidget::resizeEvent(event);
    fitColumns();
}

void CellularChannelGrid::setCarriers(const std::vector<modemnmt::NmtCarrierConfig>& carriers)
{
    for (Cell& cell : m_cells) {
        cell.used = false;
    }
    for (const modemnmt::NmtCarrierConfig& c : carriers)
    {
        const auto it = m_byChannel.constFind(c.channel);
        if (it != m_byChannel.constEnd())
        {
            m_cells[*it].used = true;
            m_cells[*it].carrier = c;
        }
    }
    // a section with carriers is open
    for (size_t s = 0; s < m_sections.size(); s++)
    {
        Section& section = m_sections[s];
        for (int i = section.first; i < section.first + section.count && !section.open; i++)
        {
            if (m_cells[i].used)
            {
                section.open = true;
                showSection(static_cast<int>(s));
            }
        }
    }
    refresh();
}

// the carriers in the order of the channel numbers
std::vector<modemnmt::NmtCarrierConfig> CellularChannelGrid::carriers() const
{
    std::vector<modemnmt::NmtCarrierConfig> out;

    for (const Cell& cell : m_cells)
    {
        if (cell.used) {
            out.push_back(cell.carrier);
        }
    }
    std::sort(out.begin(), out.end(), [](const modemnmt::NmtCarrierConfig& a, const modemnmt::NmtCarrierConfig& b) {
        return a.channel < b.channel;
    });
    return out;
}

void CellularChannelGrid::setOthers(const QMap<int, QString>& others)
{
    m_others = others;
    refresh();
}

void CellularChannelGrid::setSpans(double downLow, double downHigh, double upLow, double upHigh)
{
    m_downLow = downLow;
    m_downHigh = downHigh;
    m_upLow = upLow;
    m_upHigh = upHigh;
    refresh();
}

int CellularChannelGrid::cellIndex(int row, int col) const
{
    if (row < 0 || row >= static_cast<int>(m_rowFirst.size()) || m_rowFirst[row] < 0 || col < 0 || col >= kColumns) {
        return -1;
    }
    const Section& section = m_sections[m_rowSection[row]];
    const int index = m_rowFirst[row] + col;

    return index < section.first + section.count ? index : -1;
}

void CellularChannelGrid::refresh()
{
    for (size_t i = 0; i < m_cells.size(); i++) {
        paintCell(static_cast<int>(i));
    }
    for (size_t s = 0; s < m_sections.size(); s++) {
        paintHeader(static_cast<int>(s));
    }
}

void CellularChannelGrid::paintCell(int index)
{
    const Cell& cell = m_cells[index];
    const int row = m_cellRow[index];
    const int col = index - m_rowFirst[row];
    QTableWidgetItem *it = item(row, col);

    if (!it)
    {
        it = new QTableWidgetItem();
        it->setTextAlignment(Qt::AlignCenter);
        it->setFlags(Qt::ItemIsEnabled);
        setItem(row, col, it);
    }
    it->setText(QString::number(cell.channel));

    const bool downIn = m_downHigh <= m_downLow || (cell.frequency.downlink >= m_downLow && cell.frequency.downlink <= m_downHigh);
    const bool upIn = m_upHigh <= m_upLow || (cell.frequency.uplink >= m_upLow && cell.frequency.uplink <= m_upHigh);
    const auto other = m_others.constFind(cell.channel);
    QString tip = QString("Channel %1: down %2 MHz, up %3 MHz").arg(cell.channel)
        .arg(cell.frequency.downlink / 1e6, 0, 'f', 4).arg(cell.frequency.uplink / 1e6, 0, 'f', 4);

    if (cell.used)
    {
        it->setBackground(typeColour(cell.carrier.type));
        it->setForeground(Qt::white);
        tip += ". " + typeText(cell.carrier.type);
        if (isTraffic(cell.carrier.type))
        {
            tip += cell.carrier.supervisory > 0 ? QString(", supervisory tone %1").arg(cell.carrier.supervisory)
                : cell.carrier.supervisory < 0 ? QString(", no supervisory tone") : QString(", supervisory tone auto");
        }
    }
    else if (other != m_others.constEnd())
    {
        it->setBackground(QColor(0x4a, 0x4a, 0x4a));
        it->setForeground(QColor(0xbb, 0xbb, 0xbb));
    }
    else if (!downIn || !upIn)
    {
        it->setBackground(QColor(0x1c, 0x1c, 0x1c));
        it->setForeground(QColor(0x66, 0x66, 0x66));
    }
    else
    {
        it->setBackground(QBrush());
        it->setForeground(QBrush());
    }
    if (other != m_others.constEnd()) {
        tip += ". Used by " + *other;
    }
    if (!downIn) {
        tip += ". Outside the span of the transmitter";
    }
    if (!upIn) {
        tip += ". Outside the span of the receiver";
    }
    it->setToolTip(tip);
}

// free, CC, TC, CC/TC, free
void CellularChannelGrid::cycle(int index)
{
    using T = modemnmt::ChannelType;
    Cell& cell = m_cells[index];

    if (!cell.used)
    {
        cell.used = true;
        cell.carrier = modemnmt::NmtCarrierConfig();
        cell.carrier.channel = cell.channel;
        cell.carrier.type = T::Cc;
    }
    else if (cell.carrier.type == T::Cc) {
        cell.carrier.type = T::Tc;
    } else if (cell.carrier.type == T::Tc) {
        cell.carrier.type = T::CcTc;
    } else {
        cell.used = false;
    }
    if (!cell.used || !isTraffic(cell.carrier.type)) {
        cell.carrier.supervisory = 0;
    }
    cellChanged(index);
}

void CellularChannelGrid::cellChanged(int index)
{
    paintCell(index);
    paintHeader(m_rowSection[m_cellRow[index]]);
    emit changed();
}

void CellularChannelGrid::menu(const QPoint& pos)
{
    using T = modemnmt::ChannelType;
    const QModelIndex at = indexAt(pos);
    const int index = at.isValid() ? cellIndex(at.row(), at.column()) : -1;

    if (index < 0) {
        return;
    }
    Cell& cell = m_cells[index];
    QMenu menu(this);
    const bool nmt900 = m_country && m_country->system == modemnmt::System::Nmt900;
    QList<T> types = {T::Cc, T::Tc, T::CcTc};

    if (nmt900) {
        types += {T::Cca, T::Ccb, T::AcTc};
    }
    types += T::Test;
    menu.addAction("Free", this, [this, index]() {
        m_cells[index].used = false;
        cellChanged(index);
    });
    for (T type : types)
    {
        QAction *a = menu.addAction(typeText(type), this, [this, index, type]() {
            Cell& c = m_cells[index];
            c.used = true;
            c.carrier.channel = c.channel;
            c.carrier.type = type;
            if (!isTraffic(type)) {
                c.carrier.supervisory = 0;
            }
            cellChanged(index);
        });
        a->setCheckable(true);
        a->setChecked(cell.used && cell.carrier.type == type);
    }
    if (cell.used && isTraffic(cell.carrier.type))
    {
        // 0: auto (the network gives 1..4 in turn), -1: none on this channel, 1..4
        QMenu *tones = menu.addMenu("Supervisory tone");
        for (int tone = 0; tone <= 5; tone++)
        {
            const int value = tone == 1 ? -1 : (tone == 0 ? 0 : tone - 1);
            const QString label = value == 0 ? QString("Auto (1..4 in turn)") : value < 0 ? QString("None") : QString("Tone %1").arg(value);
            QAction *a = tones->addAction(label, this, [this, index, value]() {
                m_cells[index].carrier.supervisory = value;
                cellChanged(index);
            });
            a->setCheckable(true);
            a->setChecked(cell.carrier.supervisory == value);
        }
    }
    menu.exec(viewport()->mapToGlobal(pos));
}
