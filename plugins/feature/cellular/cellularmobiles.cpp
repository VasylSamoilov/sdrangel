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

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QTableView>
#include <QVBoxLayout>

#include "cellularmobiles.h"

namespace
{
const int kPad = 3;         // around the text of a chip
const int kGap = 4;         // between chips
}

// ---- the rows of the Mobiles table ----

CellularMobileDelegate::CellularMobileDelegate(QTableView *view) :
    QStyledItemDelegate(view),
    m_view(view)
{
}

int CellularMobileDelegate::layoutChips(const QStringList& chips, int width, const QFontMetrics& fm, std::vector<QRect> *rects) const
{
    const int h = fm.height() + 2;
    int x = 0, y = 0;

    for (const QString& chip : chips)
    {
        const int w = fm.horizontalAdvance(chip.section('|', 0, 0)) + 2 * kPad;
        if (x > 0 && x + w > width)
        {
            x = 0;
            y += h + 2;
        }
        if (rects) {
            rects->push_back(QRect(x, y, w, h));
        }
        x += w + kGap;
    }
    return chips.isEmpty() ? 0 : y + h;
}

void CellularMobileDelegate::paint(QPainter *painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    opt.icon = QIcon();
    // the selection background and focus as any item
    opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const QRect r = option.rect.adjusted(4, 2, -4, -2);
    const QFontMetrics fm(option.font);
    const QColor dot = index.data(CellularMobileRole::StateColour).value<QColor>();
    const QString title = index.data(CellularMobileRole::Title).toString();
    const QString subtitle = index.data(CellularMobileRole::Subtitle).toString();
    const QStringList chips = index.data(CellularMobileRole::Chips).toStringList();
    const bool selected = option.state & QStyle::State_Selected;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    // first line: state dot, name (bold) and number
    const int d = fm.height() * 2 / 3;
    painter->setPen(Qt::NoPen);
    painter->setBrush(dot.isValid() ? dot : QColor(0x55, 0x55, 0x55));
    painter->drawEllipse(QRect(r.left(), r.top() + (fm.height() - d) / 2, d, d));
    QFont bold = option.font;
    bold.setBold(true);
    painter->setFont(bold);
    painter->setPen(selected ? option.palette.highlightedText().color() : option.palette.text().color());
    const int textLeft = r.left() + d + 6;
    painter->drawText(QRect(textLeft, r.top(), r.right() - textLeft, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
    if (!subtitle.isEmpty())
    {
        const int titleWidth = QFontMetrics(bold).horizontalAdvance(title) + 6;
        painter->setFont(option.font);
        painter->drawText(QRect(textLeft + titleWidth, r.top(), r.right() - textLeft - titleWidth, fm.height()),
            Qt::AlignLeft | Qt::AlignVCenter, subtitle);
    }
    // the chips, wrapping
    painter->setFont(option.font);
    std::vector<QRect> rects;
    layoutChips(chips, r.right() - textLeft, fm, &rects);
    for (int i = 0; i < chips.size() && i < static_cast<int>(rects.size()); i++)
    {
        const QRect c = rects[i].translated(textLeft, r.top() + fm.height() + 3);
        const QColor background(chips[i].section('|', 1, 1));
        painter->setPen(Qt::NoPen);
        painter->setBrush(background.isValid() ? background : QColor(0x2b, 0x48, 0x67));
        painter->drawRoundedRect(c, 3, 3);
        painter->setPen(Qt::white);
        painter->drawText(c, Qt::AlignCenter, chips[i].section('|', 0, 0));
    }
    painter->restore();
}

QSize CellularMobileDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QFontMetrics fm(option.font);
    const int column = m_view ? m_view->columnWidth(index.column()) : option.rect.width();
    const int d = fm.height() * 2 / 3;
    const int chipsHeight = layoutChips(index.data(CellularMobileRole::Chips).toStringList(), column - 8 - d - 6, fm, nullptr);

    return QSize(fm.horizontalAdvance(index.data(CellularMobileRole::Title).toString()) + 60,
        fm.height() + (chipsHeight ? chipsHeight + 3 : 0) + 6);
}

// ---- the record of one phone ----

CellularMobileDialog::CellularMobileDialog(const modemnmt::NmtPhoneRecord& record, const QString& live, QWidget *parent) :
    QDialog(parent)
{
    using F = modemnmt::NmtPhoneRecord;
    setWindowTitle(QString("Mobile %1").arg(QString::fromStdString(record.number)));
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    QWidget *page = new QWidget(scroll);
    QFormLayout *form = new QFormLayout(page);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);    // as the settings of a network
    form->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(page);
    layout->addWidget(scroll);

    auto section = [form, page](const QString& title) {
        form->addRow(new QLabel("<b>" + title + "</b>", page));
    };
    auto mark = [page](const modemnmt::NmtBookField& f) {
        QLabel *m = new QLabel(page);
        m->setTextFormat(Qt::RichText);
        if (f.typed) {
            m->setText("<span style=\"background-color:#2b4867;\">&nbsp;typed&nbsp;</span>");
        } else if (!f.value.empty()) {
            m->setText("<span style=\"background-color:#3a3a3a; color:#bbbbbb;\">&nbsp;learned&nbsp;</span>");
        }
        m->setToolTip("Learned: from the phone; typed: by you (learning does not change it; clear it to learn again)");
        return m;
    };
    // a field: a line, or an editable list of numbers the phone dialled
    auto field = [&](const QString& label, F::Field f, const QString& tip, bool numbers) {
        Edit e;
        e.field = f;
        e.initial = QString::fromStdString(record.value(f));
        QWidget *row = new QWidget(page);
        QHBoxLayout *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        if (numbers)
        {
            e.combo = new QComboBox(row);
            e.combo->setEditable(true);
            for (const modemnmt::NmtPhoneInfo::Seen& s : record.dialled) {
                e.combo->addItem(QString::fromStdString(s.number));
            }
            e.combo->setCurrentText(e.initial);
            e.combo->setToolTip(tip);
            h->addWidget(e.combo, 1);
        }
        else
        {
            e.line = new QLineEdit(e.initial, row);
            e.line->setToolTip(tip);
            h->addWidget(e.line, 1);
        }
        h->addWidget(mark(record.fields[f]));
        form->addRow(label, row);
        m_edits.push_back(e);
    };
    auto info = [&](const QString& label, const QString& value) {
        QLabel *l = new QLabel(value.isEmpty() ? QString("-") : value, page);
        l->setWordWrap(true);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, l);
    };

    section("Identity");
    info("Number", QString::fromStdString(record.number) + (record.system == modemnmt::System::Nmt900 ? " (NMT-900)" : " (NMT-450)"));
    field("Name", F::Name, "A name for the phone, in the lists and the log", false);
    field("Notes", F::Notes, "Anything about the phone", false);
    section("Now");
    info("On the air", live);
    section("Network");
    QStringList areas;
    for (const std::pair<int, int>& a : record.areas) {
        areas.append(QString("Y1 %1, area %2").arg(a.first).arg(a.second));
    }
    info("Heard on", areas.join("; "));
    section("Security");
    field("Password K", F::Password, "The password in its seizure frames (taken when two frames agree)", false);
    section("SMS");
    field("SMS centre", F::SmsCentre, "The number its short messages go to: learned when it really sends to or "
        "fetches from a number; pick one it dialled or type one", true);
    info("Centres it used", QString::fromStdString(modemnmt::NmtPhoneInfo::text(record.smsCentresUsed, true)));
    section("Voice mail");
    field("Number", F::VoiceMail, "The number it calls for its voice mail: pick the one it dialled after the lamp lit", true);
    field("Code", F::VoiceMailCode, "The code of the voice mail, if the phone keeps one", false);
    section("Seen");
    info("Dialled", QString::fromStdString(modemnmt::NmtPhoneInfo::text(record.dialled, false)));

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(380, 560);
}

QString CellularMobileDialog::text(const Edit& e) const
{
    return (e.combo ? e.combo->currentText() : e.line->text()).trimmed();
}

std::vector<std::pair<modemnmt::NmtPhoneRecord::Field, std::string>> CellularMobileDialog::changes() const
{
    std::vector<std::pair<modemnmt::NmtPhoneRecord::Field, std::string>> out;

    for (const Edit& e : m_edits)
    {
        const QString now = text(e);
        if (now != e.initial) {
            out.emplace_back(e.field, now.toStdString());
        }
    }
    return out;
}
