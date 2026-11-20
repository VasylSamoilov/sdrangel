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

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "gui/dialogpositioner.h"

#include "messagecentercontacts.h"

QIcon MessageCenterContactIcons::icon(const QString& icon)
{
    return icon.isEmpty() ? QIcon() : QIcon(MessageCenterContact::iconPath(icon));
}

// ---------------------------------------------------------------------------
// Icon picker
// ---------------------------------------------------------------------------

MessageCenterIconPicker::MessageCenterIconPicker(const QString& current, QWidget *parent) :
    QDialog(parent),
    m_icon(current)
{
    setWindowTitle(tr("Icon"));
    QVBoxLayout *layout = new QVBoxLayout(this);
    QTabWidget *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    const int columns = 10;
    const int size = 2 * fontMetrics().height();

    // A tab of icons for each category: a click picks the icon
    for (const auto& category : MessageCenterContact::iconCategories())
    {
        QWidget *page = new QWidget(tabs);
        QGridLayout *grid = new QGridLayout(page);
        grid->setSpacing(2);
        int n = 0;

        for (const QString& name : category.second)
        {
            QToolButton *button = new QToolButton(page);
            button->setIcon(MessageCenterContactIcons::icon(":" + name));
            button->setIconSize(QSize(size, size));
            button->setAutoRaise(true);
            button->setCheckable(true);
            button->setChecked(current == ":" + name);
            button->setToolTip(QString(name).replace('_', ' '));
            connect(button, &QToolButton::clicked, this, [this, name]() {
                m_icon = ":" + name;
                QDialog::accept();
            });
            grid->addWidget(button, n / columns, n % columns);
            n++;
        }

        grid->setRowStretch(grid->rowCount(), 1);
        grid->setColumnStretch(columns, 1);
        QScrollArea *scroll = new QScrollArea(tabs);
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        tabs->addTab(scroll, category.first);

        if (category.second.contains(current.mid(1))) {
            tabs->setCurrentWidget(scroll);
        }
    }

    layout->addWidget(tabs);

    // An image of its own, or none
    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *file = new QPushButton(tr("Image file..."), this);
    file->setToolTip(tr("An image of your own (PNG, SVG...), shown at the height of a line"));
    QPushButton *none = new QPushButton(tr("No icon"), this);
    QPushButton *cancel = new QPushButton(tr("Cancel"), this);
    buttons->addWidget(file);
    buttons->addWidget(none);
    buttons->addStretch();
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    QLabel *credit = new QLabel(tr("Icons: Microsoft Fluent Emoji (flat), MIT License"), this);
    credit->setEnabled(false);
    layout->addWidget(credit);

    connect(file, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Icon image"), QString(),
            tr("Images (*.png *.svg *.jpg *.jpeg *.bmp *.gif *.ico);;All files (*)"));

        if (!path.isEmpty())
        {
            m_icon = path;
            QDialog::accept();
        }
    });
    connect(none, &QPushButton::clicked, this, [this]() {
        m_icon.clear();
        QDialog::accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    // All the category tabs in view, and the largest category (Radio) without scrolling
    int tabsWidth = 0;

    for (int i = 0; i < tabs->count(); i++) {
        tabsWidth += tabs->tabBar()->fontMetrics().horizontalAdvance(tabs->tabText(i)) + 24;
    }

    resize(std::max(columns * (size + 12) + 40, tabsWidth + 30), 11 * (size + 10));
    new DialogPositioner(this, true);
}

// ---------------------------------------------------------------------------
// Contact dialog
// ---------------------------------------------------------------------------

MessageCenterContactDialog::MessageCenterContactDialog(const MessageCenterContact& contact, const QStringList& otherNames,
        const QStringList& fields, const std::vector<QJsonObject>& log, QWidget *parent) :
    QDialog(parent),
    m_contact(contact),
    m_otherNames(otherNames),
    m_fields(fields),
    m_log(log)
{
    setWindowTitle(contact.m_name.isEmpty() ? tr("New contact") : tr("Contact %1").arg(contact.m_name));
    QVBoxLayout *layout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();

    // The icon and the colour beside the name, as the log shows them
    QWidget *nameRow = new QWidget(this);
    QHBoxLayout *nameLayout = new QHBoxLayout(nameRow);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    m_icon = new QToolButton(nameRow);
    m_icon->setIconSize(QSize(fontMetrics().height() + 4, fontMetrics().height() + 4));
    m_icon->setToolTip(tr("Icon of the contact in the log: a built-in one or an image of your own"));
    m_name = new QLineEdit(contact.m_name, nameRow);
    m_name->setPlaceholderText(tr("e.g. Fire station 3"));
    m_name->setToolTip(tr("Name of the contact: the log shows it beside its messages, the filters select by it"));
    m_color = new QToolButton(nameRow);
    m_color->setToolTip(tr("Colour of the name in the log; right click for none"));
    m_color->setContextMenuPolicy(Qt::CustomContextMenu);
    nameLayout->addWidget(m_icon);
    nameLayout->addWidget(m_name, 1);
    nameLayout->addWidget(m_color);
    form->addRow(tr("Name"), nameRow);

    // The rules: a row a condition, the conditions of a rule (its number) all met
    QWidget *rulesBox = new QWidget(this);
    QVBoxLayout *rulesLayout = new QVBoxLayout(rulesBox);
    rulesLayout->setContentsMargins(0, 0, 0, 0);
    m_conditions = new QTableWidget(0, 5, rulesBox);
    m_conditions->setHorizontalHeaderLabels({tr("Rule"), tr("Field"), QString(), tr("Value"), tr("Finds")});
    m_conditions->verticalHeader()->setVisible(false);
    m_conditions->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_conditions->setSelectionMode(QAbstractItemView::SingleSelection);
    m_conditions->horizontalHeader()->setSectionResizeMode(ColRule, QHeaderView::ResizeToContents);
    m_conditions->horizontalHeader()->setSectionResizeMode(ColOperator, QHeaderView::ResizeToContents);
    m_conditions->horizontalHeader()->setSectionResizeMode(ColField, QHeaderView::Interactive);
    m_conditions->horizontalHeader()->setSectionResizeMode(ColValue, QHeaderView::Stretch);
    m_conditions->horizontalHeader()->setSectionResizeMode(ColCheck, QHeaderView::ResizeToContents);
    m_conditions->setColumnWidth(ColField, 16 * fontMetrics().averageCharWidth());
    m_conditions->setMinimumHeight(7 * fontMetrics().lineSpacing());
    m_conditions->setToolTip(tr("The messages of the contact: those that meet all the conditions of one of its rules (the rows of "
        "the same rule number).\nA condition is on a field of the message: protocol, address, msgType, channel, frequency, "
        "or a field of the protocol\n(ric, function, capcode, group_capcodes...; the details of a message in the log show them). "
        "It is the value\n(numbers as numbers: 0001234567 is 1234567) or matches a regular expression of the whole value; "
        "a list field\nwhen one of its values does. E.g. rule 1: protocol is pocsag, ric is 1234567; "
        "rule 2: protocol is flex, capcode is 1100001.\nFinds: the messages of the log the rule finds"));
    QHBoxLayout *ruleButtons = new QHBoxLayout();
    QToolButton *addCondition = new QToolButton(rulesBox);
    addCondition->setText(tr("+ Condition"));
    addCondition->setToolTip(tr("Add a condition to the rule selected (all of them must be met)"));
    QToolButton *addRule = new QToolButton(rulesBox);
    addRule->setText(tr("+ Rule"));
    addRule->setToolTip(tr("Add a rule (any of the rules finds the messages of the contact)"));
    QToolButton *remove = new QToolButton(rulesBox);
    remove->setIcon(QIcon(":/bin.png"));
    remove->setToolTip(tr("Remove the condition selected"));
    ruleButtons->addWidget(addRule);
    ruleButtons->addWidget(addCondition);
    ruleButtons->addWidget(remove);
    ruleButtons->addStretch();
    rulesLayout->addWidget(m_conditions);
    rulesLayout->addLayout(ruleButtons);
    form->addRow(tr("Rules"), rulesBox);

    for (int r = 0; r < contact.m_rules.size(); r++)
    {
        for (const MessageCenterContact::Condition& condition : contact.m_rules[r]) {
            addConditionRow(r + 1, condition);
        }
    }

    // A new contact: the address of a protocol to start with
    if (contact.m_rules.isEmpty())
    {
        MessageCenterContact::Condition protocol;
        protocol.m_field = "protocol";
        protocol.m_value = "pocsag";
        MessageCenterContact::Condition ric;
        ric.m_field = "ric";
        addConditionRow(1, protocol);
        addConditionRow(1, ric);
    }

    m_note = new QLineEdit(contact.m_note, this);
    m_note->setToolTip(tr("A note of your own"));
    form->addRow(tr("Note"), m_note);
    layout->addLayout(form);

    m_error = new QLabel(this);
    m_error->setStyleSheet("QLabel { color: red; }");
    m_error->setWordWrap(true);
    m_error->setVisible(false);
    layout->addWidget(m_error);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &MessageCenterContactDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(addCondition, &QToolButton::clicked, this, [this]() {
        const int row = m_conditions->currentRow();
        const int rule = (row >= 0) ? rowRule(row) : 1;
        addConditionRow(rule, MessageCenterContact::Condition());
        showChecks();
    });
    connect(addRule, &QToolButton::clicked, this, [this]() {
        int last = 0;

        for (int row = 0; row < m_conditions->rowCount(); row++) {
            last = std::max(last, rowRule(row));
        }

        MessageCenterContact::Condition protocol;
        protocol.m_field = "protocol";
        addConditionRow(last + 1, protocol);
        showChecks();
    });
    connect(remove, &QToolButton::clicked, this, [this]() {
        const int row = m_conditions->currentRow();

        if (row >= 0)
        {
            m_conditions->removeRow(row);
            showChecks();
        }
    });
    connect(m_icon, &QToolButton::clicked, this, [this]() {
        MessageCenterIconPicker picker(m_contact.m_icon, this);

        if (picker.exec() == QDialog::Accepted)
        {
            m_contact.m_icon = picker.icon();
            showIcon();
        }
    });
    connect(m_color, &QToolButton::clicked, this, [this]() {
        QColor color = QColorDialog::getColor(m_contact.m_color ? QColor::fromRgba(m_contact.m_color) : QColor(Qt::white), this,
            tr("Colour of the contact"));

        if (color.isValid())
        {
            m_contact.m_color = color.rgba();
            showColor();
        }
    });
    connect(m_color, &QWidget::customContextMenuRequested, this, [this]() {
        m_contact.m_color = 0;
        showColor();
    });
    connect(m_name, &QLineEdit::textChanged, this, [this]() { check(); });

    showIcon();
    showColor();
    showChecks();
    setMinimumWidth(80 * fontMetrics().averageCharWidth());
    new DialogPositioner(this, true);
}

void MessageCenterContactDialog::addConditionRow(int rule, const MessageCenterContact::Condition& condition)
{
    const int row = m_conditions->rowCount();
    m_conditions->insertRow(row);

    QSpinBox *ruleBox = new QSpinBox(m_conditions);
    ruleBox->setRange(1, 99);
    ruleBox->setValue(rule);
    ruleBox->setToolTip(tr("Rule of the condition: the conditions of a rule must all be met"));
    QComboBox *field = new QComboBox(m_conditions);
    field->setEditable(true);
    field->addItems(m_fields);
    field->setCurrentText(condition.m_field);
    field->setToolTip(tr("Field of the message: those of the messages in the log, or another one typed in"));
    QComboBox *op = new QComboBox(m_conditions);
    op->addItems({tr("is"), tr("matches")});
    op->setCurrentIndex(condition.m_regExp ? 1 : 0);
    op->setToolTip(tr("is: the value (numbers as numbers); matches: a regular expression of the whole value"));
    QLineEdit *value = new QLineEdit(condition.m_value, m_conditions);
    value->setFrame(false);
    QLabel *finds = new QLabel(m_conditions);
    finds->setContentsMargins(4, 0, 4, 0);

    m_conditions->setCellWidget(row, ColRule, ruleBox);
    m_conditions->setCellWidget(row, ColField, field);
    m_conditions->setCellWidget(row, ColOperator, op);
    m_conditions->setCellWidget(row, ColValue, value);
    m_conditions->setCellWidget(row, ColCheck, finds);

    connect(ruleBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { showChecks(); });
    connect(field, &QComboBox::currentTextChanged, this, [this]() { showChecks(); });
    connect(op, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { showChecks(); });
    connect(value, &QLineEdit::textChanged, this, [this]() { showChecks(); });

    for (QWidget *w : {(QWidget *) ruleBox, (QWidget *) field, (QWidget *) op, (QWidget *) value}) {
        w->installEventFilter(this);
    }
}

int MessageCenterContactDialog::rowRule(int row) const
{
    const QSpinBox *rule = qobject_cast<QSpinBox *>(m_conditions->cellWidget(row, ColRule));
    return rule ? rule->value() : 1;
}

MessageCenterContact::Condition MessageCenterContactDialog::rowCondition(int row) const
{
    MessageCenterContact::Condition condition;
    const QComboBox *field = qobject_cast<QComboBox *>(m_conditions->cellWidget(row, ColField));
    const QComboBox *op = qobject_cast<QComboBox *>(m_conditions->cellWidget(row, ColOperator));
    const QLineEdit *value = qobject_cast<QLineEdit *>(m_conditions->cellWidget(row, ColValue));

    if (field && op && value)
    {
        condition.m_field = field->currentText().trimmed();
        condition.m_regExp = (op->currentIndex() == 1);
        condition.m_value = condition.m_regExp ? value->text() : value->text().trimmed();
    }

    return condition;
}

MessageCenterContact MessageCenterContactDialog::contact() const
{
    MessageCenterContact c = m_contact;
    c.m_name = m_name->text().trimmed();
    c.m_note = m_note->text().trimmed();
    c.m_rules.clear();

    // The rules in the order of their numbers, the conditions without a field left out
    QMap<int, MessageCenterContact::Rule> rules;

    for (int row = 0; row < m_conditions->rowCount(); row++)
    {
        const MessageCenterContact::Condition condition = rowCondition(row);

        if (!condition.m_field.isEmpty()) {
            rules[rowRule(row)].append(condition);
        }
    }

    for (const MessageCenterContact::Rule& rule : rules) {
        c.m_rules.append(rule);
    }

    return c;
}

// Each condition checked, and what its rule finds in the log: the messages of the contact, before it is saved
void MessageCenterContactDialog::showChecks()
{
    const MessageCenterContact c = contact();
    QMap<int, int> found;
    QMap<int, MessageCenterContact::Rule> rules;

    for (int row = 0; row < m_conditions->rowCount(); row++)
    {
        const MessageCenterContact::Condition condition = rowCondition(row);

        if (!condition.m_field.isEmpty() && condition.check().isEmpty()) {
            rules[rowRule(row)].append(condition);
        }
    }

    for (auto it = rules.begin(); it != rules.end(); ++it)
    {
        MessageCenterContact one;
        one.m_rules.append(it.value());
        int n = 0;

        for (const QJsonObject& fields : m_log) {
            n += one.matches(fields) ? 1 : 0;
        }

        found[it.key()] = n;
    }

    for (int row = 0; row < m_conditions->rowCount(); row++)
    {
        QLabel *label = qobject_cast<QLabel *>(m_conditions->cellWidget(row, ColCheck));
        const MessageCenterContact::Condition condition = rowCondition(row);
        const QString error = condition.check();

        if (!label) {
            continue;
        }
        if (!error.isEmpty() && !condition.m_field.isEmpty())
        {
            label->setText(tr("not valid"));
            label->setToolTip(error);
            label->setStyleSheet("QLabel { color: red; }");
        }
        else
        {
            const int n = found.value(rowRule(row), 0);
            label->setText((n == 1) ? tr("1 message") : tr("%1 messages").arg(n));
            label->setToolTip(tr("Rule %1 finds %2 of the last %3 messages of the log").arg(rowRule(row)).arg(n).arg(m_log.size()));
            label->setStyleSheet(QString());
        }
    }

    check();
}

void MessageCenterContactDialog::showIcon()
{
    QIcon icon = MessageCenterContactIcons::icon(m_contact.m_icon);
    m_icon->setIcon(icon);
    m_icon->setText(icon.isNull() ? tr("Icon") : QString());
    m_icon->setToolButtonStyle(icon.isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
}

void MessageCenterContactDialog::showColor()
{
    if (m_contact.m_color == 0)
    {
        m_color->setText(tr("Colour"));
        m_color->setStyleSheet(QString());
        m_name->setStyleSheet(QString());
    }
    else
    {
        const QString name = QColor::fromRgba(m_contact.m_color).name();
        m_color->setText(QString());
        m_color->setStyleSheet(QString("QToolButton { background-color: %1; min-width: 2em; }").arg(name));
        m_name->setStyleSheet(QString("QLineEdit { color: %1; }").arg(name));
    }
}

// The name is there and unique, every condition can match: else what is wrong, under the fields
bool MessageCenterContactDialog::check()
{
    const MessageCenterContact c = contact();
    QString why;

    if (c.m_name.isEmpty()) {
        why = tr("The contact needs a name");
    } else if (m_otherNames.contains(c.m_name)) {
        why = tr("Another contact is named %1").arg(c.m_name);
    } else if (c.m_rules.isEmpty()) {
        why = tr("The contact needs a rule: a condition at least");
    }

    for (const MessageCenterContact::Rule& rule : c.m_rules)
    {
        for (const MessageCenterContact::Condition& condition : rule)
        {
            if (why.isEmpty()) {
                why = condition.check();
            }
        }
    }

    m_error->setText(why);
    m_error->setVisible(!why.isEmpty());
    return why.isEmpty();
}

bool MessageCenterContactDialog::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        for (int r = 0; r < m_conditions->rowCount(); r++)
        {
            for (int c = 0; c < m_conditions->columnCount(); c++)
            {
                if (m_conditions->cellWidget(r, c) == object) {
                    m_conditions->selectRow(r);
                }
            }
        }
    }

    return QDialog::eventFilter(object, event);
}

void MessageCenterContactDialog::accept()
{
    if (check()) {
        QDialog::accept();
    }
}
