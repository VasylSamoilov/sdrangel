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

#include <QDateTime>
#include <QSet>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QToolButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QColorDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

#include "feature/featureuiset.h"
#include "gui/basicfeaturesettingsdialog.h"
#include "gui/dialogpositioner.h"
#include "gui/flowlayout.h"
#include "gui/pagingmessagetable.h"

#include "radiopagingpage.h"
#include "messagecentergui.h"

namespace {

const int idRole = Qt::UserRole;

QTableWidget *makeTable(const QStringList& headers, QWidget *parent)
{
    QTableWidget *table = new QTableWidget(parent);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    // Each column as wide as its contents (a number, yes or no, a name), the last one takes the width left
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setMinimumHeight(60);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // One line a row: a long text ends with an ellipsis, its tooltip has all of it
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);
    return table;
}

void setRow(QTableWidget *table, int row, const QStringList& cells)
{
    for (int c = 0; c < cells.size(); c++)
    {
        QTableWidgetItem *item = table->item(row, c);

        if (!item)
        {
            item = new QTableWidgetItem();
            table->setItem(row, c, item);
        }

        item->setText(cells[c]);
        item->setToolTip(cells[c]);
    }
}

QString filterSummary(const MessageCenterFilter& f)
{
    QStringList parts;

    if (!f.m_protocols.isEmpty()) {
        parts << f.m_protocols.join("/");
    }
    if (f.m_direction >= 0) {
        parts << (f.m_direction == (int) MessageRecord::DirectionRX ? "RX" : "TX");
    }
    if (f.m_polarity >= 0) {
        parts << (f.m_polarity == (int) MessageRecord::PolarityInverted ? "inverted" : "normal");
    }
    if (!f.m_channels.isEmpty()) {
        parts << "channel " + f.m_channels.join(",");
    }
    if (!f.m_address.isEmpty()) {
        parts << "address /" + f.m_address + "/";
    }
    if (!f.m_msgType.isEmpty()) {
        parts << f.m_msgType;
    }
    if (!f.m_text.isEmpty()) {
        parts << "text /" + f.m_text + "/";
    }
    if (!f.m_sources.isEmpty()) {
        parts << "from " + f.m_sources.join(",");
    }
    if (f.m_hideDuplicates) {
        parts << "no duplicates";
    }
    if (f.m_voiceOnly) {
        parts << "voice files";
    }
    if (!f.m_regExp.isEmpty()) {
        parts << QString("%1 ~ /%2/").arg(MessageCenterFilter::matchFieldName(f.m_matchField).toLower()).arg(f.m_regExp);
    }

    return parts.isEmpty() ? QString("everything") : parts.join(", ");
}

// A button with an icon of the project and its tool tip, as the buttons of the log of the Pager demodulator
QToolButton *iconButton(QWidget *parent, const char *icon, const QString& tip)
{
    QToolButton *button = new QToolButton(parent);
    button->setIcon(QIcon(icon));
    button->setToolTip(tip);
    return button;
}

} // namespace

// ---------------------------------------------------------------------------
// Filter widget
// ---------------------------------------------------------------------------

MessageCenterFilterWidget::MessageCenterFilterWidget(QWidget *parent) :
    QWidget(parent)
{
    // The filters in a line that wraps at the width of the window (two lines in the default width of the Message
    // Center, one when it is wide enough), each at its size, as the settings lines of the channels
    FlowLayout *layout = new FlowLayout(this, 0, 6, 3);

    m_protocol = new QComboBox(this);
    m_protocol->addItems({"Any protocol", "pocsag", "flex", "gsc"});
    m_direction = new QComboBox(this);
    m_direction->addItems({"RX and TX", "RX", "TX"});
    m_polarity = new QComboBox(this);
    m_polarity->addItems({"Any polarity", "Normal", "Inverted"});
    m_type = new QComboBox(this);
    m_type->addItems({"Any type", "alphanumeric", "numeric", "tone", "voice", "short_message", "binary", "secure"});
    m_channel = new QLineEdit(this);
    m_channel->setPlaceholderText("channel name");
    m_address = new QLineEdit(this);
    m_address->setPlaceholderText("regular expression");
    m_text = new QLineEdit(this);
    m_text->setPlaceholderText("regular expression");
    m_source = new QComboBox(this);
    m_source->addItems({"Any source", "channels", "api"});
    m_source->setVisible(false);
    m_hideDuplicates = new QCheckBox("Hide duplicates", this);
    m_voiceOnly = new QCheckBox("Voice files", this);
    m_matchField = new QComboBox(this);

    for (int field = MessageCenterFilter::MatchAddress; field <= MessageCenterFilter::MatchAny; field++) {
        m_matchField->addItem(MessageCenterFilter::matchFieldName(field));
    }

    m_regExp = new QLineEdit(this);
    m_regExp->setPlaceholderText("regular expression");

    m_protocol->setToolTip("Protocol of the messages: any, POCSAG, FLEX or GSC");
    m_direction->setToolTip("Received (RX), transmitted (TX) or both");
    m_polarity->setToolTip("Polarity the message was received or sent with");
    m_type->setToolTip("Message type as the decoder or encoder names it");
    m_channel->setToolTip("Name of the channel (its title), or the transmitter of a page (exact). Empty: any channel");
    m_address->setToolTip("Regular expression the whole address must match, as the filters of the other channels:\n"
        "e.g. 1234567 for that RIC, 12345.. for 1234500 to 1234599, 001001/. for a GSC cap code. Empty: any address");
    m_text->setToolTip("Regular expression the whole text must match: e.g. .*FIRE.* for a text that contains FIRE,\n"
        "(?i).*fire.* not case sensitive. Empty: any text");
    m_source->setToolTip("Where the message comes from: the channels, the API (ingest), or a source of I/O");
    m_hideDuplicates->setToolTip("Hide the messages marked as duplicates (copies received again)");
    m_voiceOnly->setToolTip("Only the GSC voice pages that have a sound file");
    m_matchField->setToolTip("Field the regular expression is matched on: address, message, protocol, type, channel,\n"
        "or any of them (separated by spaces)");
    m_regExp->setToolTip("Regular expression the field must match, anywhere in it (^ and $ to anchor). Case sensitive,\n"
        "(?i) at the start for not. Its captures are ${1}, ${2}... in the texts of the actions. Empty: no expression");

    // The text fields with their names (a placeholder alone is hard to see), at a width that fits an expression
    auto labelled = [this](const QString& name, QWidget *field, QLineEdit *edit, int chars) {
        QWidget *w = new QWidget(this);
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(3);

        if (!name.isEmpty())
        {
            QLabel *label = new QLabel(name, w);
            label->setToolTip(edit->toolTip());
            l->addWidget(label);
        }

        if (field) {
            l->addWidget(field);
        }

        edit->setFixedWidth(edit->fontMetrics().averageCharWidth() * chars);
        l->addWidget(edit);
        return w;
    };

    layout->addWidget(m_protocol);
    layout->addWidget(m_direction);
    layout->addWidget(m_polarity);
    layout->addWidget(m_type);
    layout->addWidget(m_hideDuplicates);
    layout->addWidget(m_voiceOnly);
    layout->addWidget(m_source);
    layout->addWidget(labelled("Channel", nullptr, m_channel, 14));
    layout->addWidget(labelled("Address", nullptr, m_address, 14));
    layout->addWidget(labelled("Text", nullptr, m_text, 16));
    layout->addWidget(labelled(QString(), m_matchField, m_regExp, 16));

    for (QComboBox *combo : {m_protocol, m_direction, m_polarity, m_type, m_source}) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MessageCenterFilterWidget::changed);
    }
    connect(m_matchField, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MessageCenterFilterWidget::changed);

    // A regular expression that is not valid is shown in red with its error, and matches nothing
    auto validate = [](QLineEdit *edit, bool anchored, const QString& help) {
        QRegularExpression expression(anchored ? QRegularExpression::anchoredPattern(edit->text()) : edit->text());
        bool valid = edit->text().isEmpty() || expression.isValid();
        edit->setStyleSheet(valid ? QString() : QString("QLineEdit { color: red; }"));
        edit->setToolTip(valid ? help : expression.errorString());
    };

    for (QLineEdit *edit : {m_address, m_text, m_regExp})
    {
        const QString help = edit->toolTip();
        const bool anchored = (edit != m_regExp);
        connect(edit, &QLineEdit::textChanged, this, [validate, edit, anchored, help]() { validate(edit, anchored, help); });
    }

    for (QLineEdit *edit : {m_channel, m_address, m_text, m_regExp}) {
        connect(edit, &QLineEdit::editingFinished, this, &MessageCenterFilterWidget::changed);
    }
    for (QCheckBox *check : {m_hideDuplicates, m_voiceOnly}) {
        connect(check, &QCheckBox::toggled, this, &MessageCenterFilterWidget::changed);
    }
}

MessageCenterFilter MessageCenterFilterWidget::filter() const
{
    MessageCenterFilter f;

    if (m_protocol->currentIndex() > 0) {
        f.m_protocols = QStringList(m_protocol->currentText());
    }
    if (m_direction->currentIndex() > 0) {
        f.m_direction = (m_direction->currentIndex() == 1) ? (int) MessageRecord::DirectionRX : (int) MessageRecord::DirectionTX;
    }
    if (m_polarity->currentIndex() > 0) {
        f.m_polarity = (m_polarity->currentIndex() == 1) ? (int) MessageRecord::PolarityNormal : (int) MessageRecord::PolarityInverted;
    }
    if (m_type->currentIndex() > 0) {
        f.m_msgType = m_type->currentText();
    }
    if (!m_channel->text().trimmed().isEmpty()) {
        f.m_channels = QStringList(m_channel->text().trimmed());
    }

    f.m_address = m_address->text().trimmed();
    f.m_text = m_text->text();

    if (m_sourcesShown && (m_source->currentIndex() > 0)) {
        f.m_sources = QStringList(m_source->currentText());
    }

    f.m_hideDuplicates = m_hideDuplicates->isChecked();
    f.m_voiceOnly = m_voiceOnly->isChecked();
    f.m_regExp = m_regExp->text();
    f.m_matchField = m_matchField->currentIndex();
    return f;
}

void MessageCenterFilterWidget::setFilter(const MessageCenterFilter& f)
{
    m_protocol->setCurrentIndex(f.m_protocols.isEmpty() ? 0 : std::max(0, m_protocol->findText(f.m_protocols.first())));
    m_direction->setCurrentIndex(f.m_direction < 0 ? 0 : (f.m_direction == (int) MessageRecord::DirectionRX ? 1 : 2));
    m_polarity->setCurrentIndex(f.m_polarity < 0 ? 0 : (f.m_polarity == (int) MessageRecord::PolarityNormal ? 1 : 2));
    m_type->setCurrentIndex(f.m_msgType.isEmpty() ? 0 : std::max(0, m_type->findText(f.m_msgType)));
    m_channel->setText(f.m_channels.isEmpty() ? QString() : f.m_channels.first());
    m_address->setText(f.m_address);
    m_text->setText(f.m_text);
    m_hideDuplicates->setChecked(f.m_hideDuplicates);
    m_voiceOnly->setChecked(f.m_voiceOnly);
    m_matchField->setCurrentIndex(f.m_matchField);
    m_regExp->setText(f.m_regExp);

    if (!f.m_sources.isEmpty() && (m_source->findText(f.m_sources.first()) < 0)) {
        m_source->addItem(f.m_sources.first());
    }

    m_source->setCurrentIndex(f.m_sources.isEmpty() ? 0 : std::max(0, m_source->findText(f.m_sources.first())));
    emit changed();     // The line edits tell their changes when edited only
}

void MessageCenterFilterWidget::showSources(const QStringList& sources)
{
    m_source->blockSignals(true);
    QString current = m_source->currentText();
    m_source->clear();
    m_source->addItems(QStringList({"Any source", "channels", "api"}) + sources);
    m_source->setCurrentIndex(std::max(0, m_source->findText(current)));
    m_source->blockSignals(false);
    m_source->setVisible(true);
    m_sourcesShown = true;
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------

MessageCenterGUI* MessageCenterGUI::create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature)
{
    return new MessageCenterGUI(pluginAPI, featureUISet, feature);
}

void MessageCenterGUI::destroy()
{
    delete this;
}

void MessageCenterGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(QStringList(), true);
}

QByteArray MessageCenterGUI::serialize() const
{
    return m_settings.serialize();
}

bool MessageCenterGUI::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        m_feature->setWorkspaceIndex(m_settings.m_workspaceIndex);
        displaySettings();
        applySettings(QStringList(), true);
        return true;
    }

    resetToDefaults();
    return false;
}

void MessageCenterGUI::setWorkspaceIndex(int index)
{
    m_settings.m_workspaceIndex = index;
    m_feature->setWorkspaceIndex(index);
}

QWidget *MessageCenterGUI::makeRollup(const QString& title, bool expanding)
{
    QWidget *widget = new QWidget(getRollupContents());
    widget->setWindowTitle(title);

    // A section with a table takes the height the window is given: the window can be resized vertically
    if (expanding) {
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    return widget;
}

MessageCenterGUI::MessageCenterGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent) :
    FeatureGUI(parent),
    m_center(reinterpret_cast<MessageCenter*>(feature)),
    m_doApplySettings(true),
    m_shownLogged(-1)
{
    (void) pluginAPI;
    (void) featureUISet;

    m_feature = feature;
    setAttribute(Qt::WA_DeleteOnClose, true);
    m_helpURL = "plugins/feature/messagecenter/readme.md";
    m_settings = m_center->settings();
    m_settings.setRollupState(&m_rollupState);

    // The sections as tabs (as the APRS and Aircraft features), so that the window fits a small screen; the options in
    // a dialog opened from the corner of the tabs
    RollupContents *rollupContents = getRollupContents();
    QWidget *main = makeRollup("Message Center", true);
    QVBoxLayout *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget(main);
    m_tabs->setDocumentMode(true);
    auto tab = [this](const QString& title) {
        QWidget *w = new QWidget(m_tabs);
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_tabs->addTab(w, title);
        return w;
    };
    buildLog(tab(tr("Log")));
    buildEndpoints(tab(tr("Channels")));
    buildQueue(tab(tr("Queue")));
    buildCompose(tab(tr("Page")));
    buildRoutes(tab(tr("Routes")));
    buildFiles(tab(tr("I/O")));
    m_tabs->setTabToolTip(m_tabs->count() - 1, tr("Sources and sinks: where messages come from besides the channels "
        "(files, UDP),\nand where the routes send them besides the log and the queue (files, UDP, commands, speech, "
        "highlight, Map)"));
    mainLayout->addWidget(m_tabs);

    m_optionsDialog = new QDialog(this);
    m_optionsDialog->setWindowTitle(tr("Message Center options"));
    QVBoxLayout *optionsLayout = new QVBoxLayout(m_optionsDialog);
    QWidget *options = new QWidget(m_optionsDialog);
    buildOptions(options);
    optionsLayout->addWidget(options);
    QDialogButtonBox *optionsButtons = new QDialogButtonBox(QDialogButtonBox::Close, m_optionsDialog);
    connect(optionsButtons, &QDialogButtonBox::rejected, m_optionsDialog, &QDialog::hide);
    optionsLayout->addWidget(optionsButtons);
    new DialogPositioner(m_optionsDialog, true);
    QToolButton *optionsButton = new QToolButton(m_tabs);
    optionsButton->setIcon(QIcon(":/listing.png"));
    optionsButton->setToolTip(tr("Options: log size, history file, claim time of the pages, time to offline of the channels"));
    connect(optionsButton, &QToolButton::clicked, this, [this]() {
        m_optionsDialog->show();
        m_optionsDialog->raise();
    });
    m_tabs->setCornerWidget(optionsButton, Qt::TopRightCorner);
    rollupContents->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rollupContents->arrangeRollups();
    connect(rollupContents, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    sizeToContents();

    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));
    m_center->setMessageQueueToGUI(&m_inputMessageQueue);
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    displaySettings();
    refresh();
    m_resizer.enableChildMouseTracking();
}

MessageCenterGUI::~MessageCenterGUI()
{
}

void MessageCenterGUI::buildLog(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    m_logFilter = new MessageCenterFilterWidget(container);
    m_logTable = new PagingMessageTable(container);
    m_logTable->setMinimumHeight(80);
    m_logTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_logTable->setMaxRows(m_maxLogRows);
    m_logTable->setDefaultColumns({PagingMessageTable::ColDate, PagingMessageTable::ColTime, PagingMessageTable::ColDirection, PagingMessageTable::ColChannel,
        PagingMessageTable::ColProtocol, PagingMessageTable::ColRate, PagingMessageTable::ColPolarity, PagingMessageTable::ColAddress,
        PagingMessageTable::ColType, PagingMessageTable::ColMessage, PagingMessageTable::ColInfo});
    m_logTable->setToolTip("The messages the routes put in the log. Click the icon of a voice message to play it.\nDouble-click a message for all its fields. Right click the header to choose the columns");
    layout->addWidget(m_logFilter);
    layout->addWidget(m_logTable);

    // Icon buttons, as those of the log of the Pager demodulator
    QHBoxLayout *buttons = new QHBoxLayout();
    QToolButton *import = iconButton(container, ":/load.png", "Read the messages of a file into the log: a JSON lines file, "
        "or a CSV log of a message center\nor of the Pager demodulator. They are not routed: no action is taken on them again");
    QToolButton *exportLog = iconButton(container, ":/save.png", "Save the messages the filters select to a CSV file\n"
        "(it can be imported again, here or in another message center)");
    QToolButton *clear = iconButton(container, ":/bin.png", "Remove all the messages of the log, and of the history file when it is kept");
    buttons->addWidget(import);
    buttons->addWidget(exportLog);
    buttons->addStretch(1);
    buttons->addWidget(clear);
    connect(import, &QToolButton::clicked, this, &MessageCenterGUI::importFile);
    connect(exportLog, &QToolButton::clicked, this, &MessageCenterGUI::exportLog);
    layout->addLayout(buttons);

    connect(clear, &QToolButton::clicked, this, [this]() {
        QString text = m_settings.m_historyEnabled
            ? QString("Remove all the messages of the log and of the history file?\n%1").arg(MessageCenter::historyFilePath())
            : QString("Remove all the messages of the log?");

        if (QMessageBox::question(this, "Clear the log", text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            m_center->getInputMessageQueue()->push(MessageCenter::MsgClearLog::create());
        }
    });
    connect(m_logFilter, &MessageCenterFilterWidget::changed, this, [this]() { m_shownLogged = -1; refreshLog(); });
    connect(m_logTable, &PagingMessageTable::columnsChanged, this, [this]() {
        m_settings.m_logColumns = m_logTable->columns();
        applySettings(QStringList("logColumns"));
    });
    connect(m_logTable, &PagingMessageTable::audioDeviceChanged, this, [this](const QString& deviceName) {
        m_settings.m_voicePlaybackDeviceName = deviceName;
        applySettings(QStringList("voicePlaybackDeviceName"));
    });
    // A message decoded again replaces its record in the log
    connect(m_logTable, &PagingMessageTable::recordRedecoded, this, [this](const MessageRecord& record) {
        m_center->getInputMessageQueue()->push(MessageCenter::MsgRedecoded::create(record));
    });
    // A member of a group: its messages in the log (the group messages it is a member of too)
    connect(m_logTable, &PagingMessageTable::addressFilterRequested, this, [this](const QString& address) {
        MessageCenterFilter f = m_logFilter->filter();
        f.m_address = QRegularExpression::escape(address);
        m_logFilter->setFilter(f);
    });
}

void MessageCenterGUI::buildEndpoints(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    m_endpointsTable = makeTable({"Channel", "Role", "Status", "Protocols", "Speeds", "Polarity", "Queue", "Seen"}, container);
    m_endpointsTable->setToolTip("The channels that announced themselves");
    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *remove = new QPushButton("Remove", container);
    remove->setToolTip("Remove the channel selected from the list; a channel there comes back when it announces itself.\n"
        "The pages of a transmitter removed go back to the queue");
    QPushButton *removeOffline = new QPushButton("Remove offline", container);
    removeOffline->setToolTip("Remove the channels that are offline");
    buttons->addWidget(remove);
    buttons->addWidget(removeOffline);
    buttons->addStretch();
    layout->addWidget(m_endpointsTable);
    layout->addLayout(buttons);

    connect(remove, &QPushButton::clicked, this, [this]() {
        const QString key = selectedId(m_endpointsTable);

        if (!key.isEmpty()) {
            m_center->getInputMessageQueue()->push(MessageCenter::MsgRemoveEndpoints::create(QStringList(key), false));
        }
    });
    connect(removeOffline, &QPushButton::clicked, this, [this]() {
        m_center->getInputMessageQueue()->push(MessageCenter::MsgRemoveEndpoints::create(QStringList(), true));
    });
}

void MessageCenterGUI::buildQueue(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    m_queueTable = makeTable({"Page", "Transmitter", "Protocol", "Address", "Text", "State", "Tries", "Retries", "Reason"}, container);
    // The text and the reason share the width left and are cut short: the state stays in view
    m_queueTable->horizontalHeader()->setStretchLastSection(false);
    m_queueTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_queueTable->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Stretch);
    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *requeue = new QPushButton("Requeue", container);
    requeue->setToolTip("A dead page goes back to pending with its attempts reset");
    QPushButton *cancel = new QPushButton("Cancel", container);
    cancel->setToolTip("Withdraw a page nobody has claimed");
    QPushButton *clear = new QPushButton("Clear sent", container);
    buttons->addWidget(requeue);
    buttons->addWidget(cancel);
    buttons->addWidget(clear);
    buttons->addStretch();
    layout->addWidget(m_queueTable);
    layout->addLayout(buttons);

    connect(requeue, &QPushButton::clicked, this, [this]() { pageAction((int) MessageCenter::MsgPageAction::Requeue); });
    connect(cancel, &QPushButton::clicked, this, [this]() { pageAction((int) MessageCenter::MsgPageAction::Cancel); });
    connect(clear, &QPushButton::clicked, this, [this]() { pageAction((int) MessageCenter::MsgPageAction::ClearSent); });
}

void MessageCenterGUI::buildCompose(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);

    // The transmitters say what they can send; the page is checked by the one that takes it (its mode, numbering)
    m_composer = new PagingComposer(container);
    m_composer->setNetworkMode(true);
    m_composer->setSendText("Post the page");
    m_composer->setTransmitters(QStringList());
    layout->addWidget(m_composer);
    // Its lines together at the top of the tab, not spread over its height
    layout->addStretch(1);

    connect(m_composer, &PagingComposer::selectionChanged, this, &MessageCenterGUI::transmitterChanged);
    connect(m_composer, &PagingComposer::sendRequested, this, &MessageCenterGUI::post);
}

void MessageCenterGUI::buildRoutes(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    // How the routes work: in the tool tip of the table (and the readme, from the help button of the window)
    m_routesTable = makeTable({"#", "Route", "Filter", "Sinks", "Stop", "On"}, container);
    // The filter, the longest text, takes the width left
    m_routesTable->horizontalHeader()->setStretchLastSection(false);
    m_routesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_routesTable->setToolTip("Every message is checked against all the routes, in their number order (#), not only the first.\n"
        "Each route whose filter selects it sends it to the route's sinks: the log, the queue or the sinks of I/O.\n"
        "A route with Stop ends the check for the messages it selects.\n"
        "The log is a sink too: a message no route sends to it is not logged\n"
        "(route 1, All messages to the log, does it by default). A sink takes only what a route sends to it.\n"
        "Double-click a route to edit it");
    QHBoxLayout *buttons = new QHBoxLayout();
    QToolButton *add = iconButton(container, ":/plus.png", "Add a route");
    QToolButton *edit = iconButton(container, ":/edit.png", "Edit the route selected");
    QToolButton *remove = iconButton(container, ":/bin.png", "Remove the route selected");
    QToolButton *up = iconButton(container, ":/arrow_up.png", "Move the route up: it is checked earlier");
    QToolButton *down = iconButton(container, ":/arrow_down.png", "Move the route down: it is checked later");
    QToolButton *exportRouting = iconButton(container, ":/save.png", "Save the routes, the sinks and the sources to a JSON file");
    QToolButton *importRouting = iconButton(container, ":/load.png", "Replace the routes, the sinks and the sources with those of a JSON file");
    buttons->addWidget(add);
    buttons->addWidget(edit);
    buttons->addWidget(remove);
    buttons->addWidget(up);
    buttons->addWidget(down);
    buttons->addStretch();
    buttons->addWidget(importRouting);
    buttons->addWidget(exportRouting);
    layout->addWidget(m_routesTable);
    layout->addLayout(buttons);

    connect(add, &QToolButton::clicked, this, &MessageCenterGUI::addRoute);
    connect(edit, &QToolButton::clicked, this, &MessageCenterGUI::editSelectedRoute);
    connect(remove, &QToolButton::clicked, this, &MessageCenterGUI::removeRoute);
    connect(up, &QToolButton::clicked, this, [this]() { moveRoute(-1); });
    connect(down, &QToolButton::clicked, this, [this]() { moveRoute(1); });
    connect(exportRouting, &QToolButton::clicked, this, &MessageCenterGUI::exportRouting);
    connect(importRouting, &QToolButton::clicked, this, &MessageCenterGUI::importRouting);
    connect(m_routesTable, &QTableWidget::cellDoubleClicked, this, &MessageCenterGUI::editSelectedRoute);
}

void MessageCenterGUI::moveRoute(int step)
{
    int row = m_routesTable->currentRow();
    int to = row + step;

    if ((row < 0) || (row >= (int) m_settings.m_routes.size()) || (to < 0) || (to >= (int) m_settings.m_routes.size())) {
        return;
    }

    std::swap(m_settings.m_routes[row], m_settings.m_routes[to]);
    refreshRoutes();
    m_routesTable->selectRow(to);
    applySettings(QStringList("routes"));
}

// The routing as one JSON object: routes, sinks and sources, as the WebAPI has them
void MessageCenterGUI::exportRouting()
{
    QString path = QFileDialog::getSaveFileName(this, "Export the routing", "messagecenter.json", "JSON (*.json)");

    if (path.isEmpty()) {
        return;
    }

    QJsonObject json;
    json["routes"] = QJsonDocument::fromJson(MessageCenterSettings::routesToJson(m_settings.m_routes)).array();
    json["sinks"] = QJsonDocument::fromJson(MessageCenterSettings::filesToJson(m_settings.m_sinks)).array();
    json["sources"] = QJsonDocument::fromJson(MessageCenterSettings::filesToJson(m_settings.m_sources)).array();
    QFile file(path);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || (file.write(QJsonDocument(json).toJson()) < 0)) {
        QMessageBox::warning(this, "Export the routing", QString("Cannot write %1").arg(path));
    }
}

void MessageCenterGUI::importRouting()
{
    QString path = QFileDialog::getOpenFileName(this, "Import the routing", QString(), "JSON (*.json);;All files (*)");

    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    QJsonParseError parseError;
    QJsonDocument document = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll(), &parseError) : QJsonDocument();

    if (!document.isObject())
    {
        QMessageBox::warning(this, "Import the routing", QString("%1 is not a routing file").arg(path));
        return;
    }

    QJsonObject json = document.object();
    MessageCenterSettings settings = m_settings;
    auto text = [&json](const char *key) { return QJsonDocument(json[key].toArray()).toJson(QJsonDocument::Compact); };
    settings.m_routes = MessageCenterSettings::routesFromJson(text("routes"));
    settings.m_sinks = MessageCenterSettings::filesFromJson(text("sinks"));
    settings.m_sources = MessageCenterSettings::filesFromJson(text("sources"));
    QString error = MessageCenter::checkRouting(settings);

    if (!error.isEmpty())
    {
        QMessageBox::warning(this, "Import the routing", error);
        return;
    }

    QString question = QString("Replace %1 routes, %2 sinks and %3 sources with %4 routes, %5 sinks and %6 sources?")
        .arg(m_settings.m_routes.size()).arg(m_settings.m_sinks.size()).arg(m_settings.m_sources.size())
        .arg(settings.m_routes.size()).arg(settings.m_sinks.size()).arg(settings.m_sources.size());

    if (QMessageBox::question(this, "Import the routing", question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    m_settings.m_routes = settings.m_routes;
    m_settings.m_sinks = settings.m_sinks;
    m_settings.m_sources = settings.m_sources;
    refreshRoutes();
    refreshFiles();
    applySettings({"routes", "sinks", "sources"});
}

void MessageCenterGUI::buildFiles(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    m_sinksTable = makeTable({"Sink", "Type", "Target", "Format", "Taken", "Failed", "Routes"}, container);
    m_sinksTable->setToolTip("The sinks the routes send messages to: files, UDP, and actions on the message (command,\n"
        "speech, highlight in the log, position on the Map). Double-click to edit");
    m_sourcesTable = makeTable({"Source", "Type", "Target"}, container);
    m_sourcesTable->setToolTip("The sources messages are read from, as if they came from the channels: files (read as they\n"
        "grow) and UDP. Double-click to edit");
    // The errors of the sinks and the sources: shown from the first one
    m_errors = new QPlainTextEdit(container);
    m_errors->setReadOnly(true);
    m_errors->setMaximumBlockCount(200);
    m_errors->setMaximumHeight(m_errors->fontMetrics().lineSpacing() * 3 + 2 * m_errors->frameWidth() + 8);
    m_errors->setToolTip("Errors of the sinks and the sources");
    m_errors->setVisible(false);
    QHBoxLayout *sinkButtons = new QHBoxLayout();
    QToolButton *addSink = iconButton(container, ":/plus.png", "Add a sink");
    QToolButton *removeSink = iconButton(container, ":/bin.png", "Remove the sink selected; the routes no longer send to it");
    sinkButtons->addWidget(addSink);
    sinkButtons->addWidget(removeSink);
    sinkButtons->addStretch();
    QHBoxLayout *sourceButtons = new QHBoxLayout();
    QToolButton *addSource = iconButton(container, ":/plus.png", "Add a source");
    QToolButton *removeSource = iconButton(container, ":/bin.png", "Remove the source selected");
    sourceButtons->addWidget(addSource);
    sourceButtons->addWidget(removeSource);
    sourceButtons->addStretch();
    layout->addWidget(m_sinksTable);
    layout->addLayout(sinkButtons);
    layout->addWidget(m_sourcesTable);
    layout->addLayout(sourceButtons);
    layout->addWidget(m_errors);

    connect(m_sinksTable, &QTableWidget::cellDoubleClicked, this, &MessageCenterGUI::editSelectedSink);
    connect(m_sourcesTable, &QTableWidget::cellDoubleClicked, this, &MessageCenterGUI::editSelectedSource);
    connect(addSink, &QToolButton::clicked, this, &MessageCenterGUI::addSink);
    connect(removeSink, &QToolButton::clicked, this, &MessageCenterGUI::removeSink);
    connect(addSource, &QToolButton::clicked, this, &MessageCenterGUI::addSource);
    connect(removeSource, &QToolButton::clicked, this, &MessageCenterGUI::removeSource);
}

void MessageCenterGUI::buildOptions(QWidget *container)
{
    QFormLayout *layout = new QFormLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);

    m_logSize = new QSpinBox(container);
    m_logSize->setRange(MessageCenterEngine::m_minLogSize, MessageCenterEngine::m_maxLogSize);
    m_logSize->setSingleStep(1000);
    m_logSize->setToolTip(QString("Messages kept in the log (in memory), %1 to %2; the oldest go first. The table shows the "
        "newest %3 the filters select").arg(MessageCenterEngine::m_minLogSize).arg(MessageCenterEngine::m_maxLogSize).arg(m_maxLogRows));
    m_claimTimeout = new QSpinBox(container);
    m_claimTimeout->setRange(1, 3600);
    m_claimTimeout->setSuffix(" s");
    m_claimTimeout->setToolTip("A page given to a transmitter that does not claim it in this time goes back to the queue");
    m_silentTime = new QSpinBox(container);
    m_silentTime->setRange(6, 3600);
    m_silentTime->setSuffix(" s");
    m_silentTime->setToolTip("A channel that does not announce itself for this time is offline");
    m_historyEnabled = new QCheckBox("Keep", container);
    m_historyEnabled->setToolTip(QString("Keep the log in the history file,\nso that it is there again when the message center "
        "starts:\n%1\nOne message center keeps the history").arg(MessageCenter::historyFilePath()));
    m_historyMaxMB = new QSpinBox(container);
    m_historyMaxMB->setRange(1, 10000);
    m_historyMaxMB->setSuffix(" MB");
    m_historyMaxMB->setToolTip("Size limit of the history file:\n"
        "over it the file is written again with the newest messages of the log, up to half the limit");
    QWidget *history = new QWidget(container);
    QHBoxLayout *historyLayout = new QHBoxLayout(history);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->addWidget(m_historyEnabled);
    historyLayout->addWidget(m_historyMaxMB, 1);

    layout->addRow("Log size", m_logSize);
    layout->addRow("History", history);
    layout->addRow("Claim time", m_claimTimeout);
    layout->addRow("Offline after", m_silentTime);

    connect(m_logSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_settings.m_logSize = value;
        applySettings(QStringList("logSize"));
    });
    connect(m_historyEnabled, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.m_historyEnabled = checked;
        m_historyMaxMB->setEnabled(checked);
        applySettings(QStringList("historyEnabled"));
    });
    connect(m_historyMaxMB, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_settings.m_historyMaxMB = value;
        applySettings(QStringList("historyMaxMB"));
    });
    connect(m_claimTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_settings.m_claimTimeoutSec = value;
        applySettings(QStringList("claimTimeoutSec"));
    });
    connect(m_silentTime, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        m_settings.m_silentSec = value;
        applySettings(QStringList("silentSec"));
    });
}

void MessageCenterGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;
    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList("rollupState"));
    sizeToContents();
}

void MessageCenterGUI::onMenuDialogCalled(const QPoint &p)
{
    if (m_contextMenuType == ContextMenuType::ContextMenuChannelSettings)
    {
        BasicFeatureSettingsDialog dialog(this);
        dialog.setTitle(m_settings.m_title);
        dialog.setUseReverseAPI(m_settings.m_useReverseAPI);
        dialog.setReverseAPIAddress(m_settings.m_reverseAPIAddress);
        dialog.setReverseAPIPort(m_settings.m_reverseAPIPort);
        dialog.setReverseAPIFeatureSetIndex(m_settings.m_reverseAPIFeatureSetIndex);
        dialog.setReverseAPIFeatureIndex(m_settings.m_reverseAPIFeatureIndex);
        dialog.setDefaultTitle(m_displayedName);
        dialog.move(p);
        new DialogPositioner(&dialog, false);
        dialog.exec();

        m_settings.m_title = dialog.getTitle();
        m_settings.m_useReverseAPI = dialog.useReverseAPI();
        m_settings.m_reverseAPIAddress = dialog.getReverseAPIAddress();
        m_settings.m_reverseAPIPort = dialog.getReverseAPIPort();
        m_settings.m_reverseAPIFeatureSetIndex = dialog.getReverseAPIFeatureSetIndex();
        m_settings.m_reverseAPIFeatureIndex = dialog.getReverseAPIFeatureIndex();
        setTitle(m_settings.m_title);
        setTitleColor(m_settings.m_rgbColor);
        applySettings({"title", "rgbColor", "useReverseAPI", "reverseAPIAddress", "reverseAPIPort",
            "reverseAPIFeatureSetIndex", "reverseAPIFeatureIndex"});
    }

    resetContextMenuType();
}

void MessageCenterGUI::applySettings(const QStringList& settingsKeys, bool force)
{
    if (m_doApplySettings) {
        m_center->getInputMessageQueue()->push(MessageCenter::MsgConfigureMessageCenter::create(m_settings, settingsKeys, force));
    }
}

void MessageCenterGUI::displaySettings()
{
    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_settings.m_title);
    setTitle(m_settings.m_title);
    m_doApplySettings = false;
    m_logSize->setValue(m_settings.m_logSize);
    m_historyEnabled->setChecked(m_settings.m_historyEnabled);
    m_historyMaxMB->setValue(m_settings.m_historyMaxMB);
    m_historyMaxMB->setEnabled(m_settings.m_historyEnabled);
    m_claimTimeout->setValue(m_settings.m_claimTimeoutSec);
    m_silentTime->setValue(m_settings.m_silentSec);
    refreshRoutes();
    refreshFiles();
    m_doApplySettings = true;

    // The columns the user chose; the first time the usual ones
    if (!m_logTable->setColumns(m_settings.m_logColumns)) {
        m_logTable->sizeColumns();
    }
    m_logTable->setAudioDevice(m_settings.m_voicePlaybackDeviceName);
    getRollupContents()->restoreState(m_rollupState);
    getRollupContents()->arrangeRollups();
}

void MessageCenterGUI::handleInputMessages()
{
    Message *message;

    while ((message = getInputMessageQueue()->pop()))
    {
        if (!handleMessage(*message)) {
            qDebug("%s: unhandled message: %s", Q_FUNC_INFO, message->getIdentifier());
        }

        delete message;
    }
}

bool MessageCenterGUI::handleMessage(const Message& message)
{
    // Settings changed from the WebAPI or the MCP server (routes, sinks, sources...): shown at once
    if (MessageCenter::MsgConfigureMessageCenter::match(message))
    {
        const MessageCenter::MsgConfigureMessageCenter& cfg = (const MessageCenter::MsgConfigureMessageCenter&) message;

        if (cfg.getForce()) {
            m_settings = cfg.getSettings();
        } else {
            m_settings.applySettings(cfg.getSettingsKeys(), cfg.getSettings());
        }

        m_settings.setRollupState(&m_rollupState);
        displaySettings();
        return true;
    }
    if (MessageCenter::MsgUpdated::match(message))
    {
        refresh();
        return true;
    }
    if (MessageCenter::MsgSpeak::match(message))
    {
        speak(((const MessageCenter::MsgSpeak&) message).getText());
        return true;
    }
    if (MessageCenter::MsgHighlight::match(message))
    {
        const MessageCenter::MsgHighlight& msg = (const MessageCenter::MsgHighlight&) message;

        if (m_highlights.size() >= m_maxHighlights) {
            m_highlights.clear();
        }

        m_highlights.insert(msg.getId(), msg.getColor());
        QColor color = QColor::fromRgba(msg.getColor());
        color.setAlpha(110);
        m_logTable->setRowBackground(msg.getId(), color);
        return true;
    }

    return false;
}

void MessageCenterGUI::refresh()
{
    refreshLog();
    refreshEndpoints();
    refreshQueue();
    refreshComposeTransmitters();
    refreshFiles();         // The counts of the sinks

    for (const QString& error : m_center->takeErrors()) {
        m_errors->appendPlainText(QString("%1 %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(error));
        m_errors->setVisible(true);
    }
}

// The newest messages of the log that the filter selects; made again when the log changed
void MessageCenterGUI::refreshLog()
{
    const MessageCenterEngine& engine = m_center->engine();

    if (engine.logged() == m_shownLogged) {
        return;
    }

    // The records stored or changed since the table was shown: their rows only. Making all the rows again at each
    // change of a large log froze the GUI
    QStringList changed;

    if ((m_shownLogged >= 0) && engine.changedSince(m_shownLogged, changed))
    {
        const MessageCenterFilter filter = m_logFilter->filter();
        QSet<QString> done;

        m_shownLogged = engine.logged();

        for (const QString& id : changed)
        {
            if (done.contains(id)) {
                continue;
            }

            done.insert(id);
            const MessageRecord *record = engine.find(id);

            if (record && filter.matches(*record))
            {
                m_logTable->addRecord(*record);
                auto highlight = m_highlights.constFind(id);

                if (highlight != m_highlights.constEnd())
                {
                    QColor color = QColor::fromRgba(highlight.value());
                    color.setAlpha(110);
                    m_logTable->setRowBackground(id, color);
                }
            }
            else
            {
                // A record the filter no longer selects (decoded again), or gone from the log
                const int row = m_logTable->findRow(id);

                if (row >= 0) {
                    m_logTable->removeRow(row);
                }
            }
        }

        return;
    }

    m_shownLogged = engine.logged();
    std::vector<MessageRecord> records = engine.select(m_logFilter->filter());
    size_t first = (records.size() > (size_t) m_maxLogRows) ? records.size() - m_maxLogRows : 0;
    QString selected = m_logTable->selectedId();

    m_logTable->setUpdatesEnabled(false);
    m_logTable->setRowCount(0);

    for (size_t i = first; i < records.size(); i++)
    {
        m_logTable->addRecord(records[i]);
        auto highlight = m_highlights.constFind(records[i].m_id);

        if (highlight != m_highlights.constEnd())
        {
            QColor color = QColor::fromRgba(highlight.value());
            color.setAlpha(110);
            m_logTable->setRowBackground(records[i].m_id, color);
        }
    }

    m_logTable->selectId(selected);
    m_logTable->setUpdatesEnabled(true);
    m_logTable->scrollToBottom();
}

void MessageCenterGUI::refreshEndpoints()
{
    static const char *statuses[] = {"offline", "idle", "busy"};
    const QMap<QString, MessageEndpoint>& endpoints = m_center->engine().endpoints();
    const QString selected = selectedId(m_endpointsTable);
    m_endpointsTable->setRowCount(endpoints.size());
    int row = 0;

    for (auto it = endpoints.constBegin(); it != endpoints.constEnd(); ++it)
    {
        const MessageEndpoint& e = it.value();
        QStringList speeds, polarities;

        for (int s : e.m_speeds) {
            speeds << QString::number(s);
        }
        for (int p : e.m_polarities) {
            polarities << ((p == (int) MessageRecord::PolarityInverted) ? "inverted" : "normal");
        }

        setRow(m_endpointsTable, row, {
            e.m_channel, e.m_role == MessageEndpoint::Transmitter ? "transmitter" : "receiver", statuses[(int) e.m_status],
            e.m_protocols.join(", "), speeds.join(", "), polarities.join(", "),
            e.m_role == MessageEndpoint::Transmitter ? QString("%1/%2").arg(e.m_queueDepth).arg(e.m_capacity) : QString(),
            QDateTime::fromMSecsSinceEpoch(e.m_lastSeen).toString("HH:mm:ss")
        });
        m_endpointsTable->item(row, 0)->setData(idRole, it.key());

        if (it.key() == selected) {
            m_endpointsTable->selectRow(row);
        }

        row++;
    }
}

void MessageCenterGUI::refreshQueue()
{
    std::vector<MessageCenterPage> pages = m_center->engine().pages();
    QString selected = selectedId(m_queueTable);
    m_queueTable->setRowCount((int) pages.size());

    for (int i = 0; i < (int) pages.size(); i++)
    {
        const MessageCenterPage& p = pages[i];
        setRow(m_queueTable, i, {
            p.m_record.m_id.left(8), p.transmitter(), p.m_record.m_protocol, p.m_record.m_address, p.m_record.m_text,
            MessageCenterPage::stateName(p.m_state), QString::number(p.m_attempts), QString::number(p.m_retransmits), p.m_reason
        });
        m_queueTable->item(i, 0)->setData(idRole, p.m_record.m_id);
        m_queueTable->item(i, 0)->setToolTip(p.m_record.m_id);

        if (p.m_record.m_id == selected) {
            m_queueTable->selectRow(i);
        }
    }
}

void MessageCenterGUI::refreshComposeTransmitters()
{
    QStringList aliases;

    for (const MessageEndpoint& e : m_center->engine().endpoints())
    {
        if ((e.m_role == MessageEndpoint::Transmitter) && (e.m_status != MessageEndpoint::Offline)) {
            aliases.append(e.m_channel);
        }
    }

    // The composer keeps its choice when the transmitters did not change
    m_composer->setTransmitters(aliases);
}

void MessageCenterGUI::refreshRoutes()
{
    const std::vector<MessageCenterEngine::Route>& routes = m_settings.m_routes;
    int selected = m_routesTable->currentRow();
    m_routesTable->setRowCount((int) routes.size());

    for (int i = 0; i < (int) routes.size(); i++)
    {
        QString sinks = routes[i].m_sinks.isEmpty() ? QString("-") : routes[i].m_sinks.join(", ");

        if (routes[i].decodes())
        {
            QStringList as;

            if (!routes[i].m_decodeCharset.empty()) {
                as.append(RadioPagingPage::charsetText(routes[i].m_decodeCharset));
            }
            if (!routes[i].m_decodeType.isEmpty()) {
                as.append(routes[i].m_decodeType);
            }

            sinks += QString(", decoded as %1").arg(as.join(" "));
        }

        setRow(m_routesTable, i, {QString::number(i + 1), routes[i].m_name, filterSummary(routes[i].m_filter),
            sinks, routes[i].m_stop ? "stop" : "", routes[i].m_enabled ? "yes" : "no"});
    }

    if ((selected >= 0) && (selected < (int) routes.size())) {
        m_routesTable->selectRow(selected);
    }
}

namespace {

// What a sink or a source works on, for its row
QString fileTarget(const MessageCenterFileSetting& f)
{
    const QString type = f.typeOrDefault();

    if (type == "udp") {
        return QString("%1:%2").arg(f.m_address.isEmpty() ? QString("127.0.0.1") : f.m_address).arg(f.m_port);
    }
    if ((type == "command") || (type == "speech")) {
        return f.m_text;
    }
    if (type == "highlight") {
        return QColor::fromRgba(f.m_color).name(QColor::HexArgb);
    }
    if (type == "map") {
        return "the position in the message";
    }

    return f.m_path;
}

QString fileFormat(const MessageCenterFileSetting& f)
{
    const QString type = f.typeOrDefault();

    if (type == "file") {
        return f.m_format == 0 ? "JSON lines" : "CSV";
    }
    if (type == "udp") {
        return f.m_format == 0 ? "Pager" : "JSON";
    }
    if (type == "csv") {
        return f.m_format == 1 ? "new rows only" : "all rows";
    }

    return QString();
}

} // namespace

// The built in sinks (log, queue) and sources (channels, api) come first: they are always there
void MessageCenterGUI::refreshFiles()
{
    const QMap<QString, MessageCenterEngine::SinkStat>& stats = m_center->engine().sinkStats();
    auto routesTo = [this](const QString& sink) {
        QStringList routes;

        for (int r = 0; r < (int) m_settings.m_routes.size(); r++)
        {
            if (m_settings.m_routes[r].m_sinks.contains(sink)) {
                routes.append(QString("%1 %2").arg(r + 1).arg(m_settings.m_routes[r].m_name));
            }
        }

        return routes;
    };
    auto setSinkRow = [&](int row, const QStringList& cells, const QStringList& routes, bool builtIn) {
        setRow(m_sinksTable, row, cells + QStringList(routes.isEmpty() ? QString("none: takes nothing") : routes.join(", ")));
        m_sinksTable->item(row, 6)->setForeground(routes.isEmpty() ? QBrush(QColor(255, 120, 80)) : QBrush());

        for (int c = 0; c < m_sinksTable->columnCount(); c++) {
            m_sinksTable->item(row, c)->setForeground(builtIn && (c != 6) ? QBrush(QColor(150, 150, 150)) : m_sinksTable->item(row, c)->foreground());
        }
    };

    m_sinksTable->setRowCount(m_builtInSinks + m_settings.m_sinks.size());
    MessageCenterEngine::SinkStat logStat = stats.value("log");
    MessageCenterEngine::SinkStat queueStat = stats.value("queue");
    setSinkRow(0, {"log", "built in", "the log of the Message Center", "", QString::number(logStat.m_accepted),
        QString::number(logStat.m_failed)}, routesTo("log"), true);
    setSinkRow(1, {"queue", "built in", "the pages to transmit (messages for a transmitter)", "", QString::number(queueStat.m_accepted),
        QString::number(queueStat.m_failed)}, routesTo("queue"), true);

    for (int i = 0; i < m_settings.m_sinks.size(); i++)
    {
        const MessageCenterFileSetting& f = m_settings.m_sinks[i];
        MessageCenterEngine::SinkStat stat = stats.value(f.m_name);
        int row = m_builtInSinks + i;
        setSinkRow(row, {f.m_name, f.typeOrDefault(), fileTarget(f), fileFormat(f),
            QString::number(stat.m_accepted), QString::number(stat.m_failed)}, routesTo(f.m_name), false);

        if (f.typeOrDefault() == "highlight") {
            m_sinksTable->item(row, 2)->setBackground(QColor::fromRgba(f.m_color));
        }
    }

    m_sourcesTable->setRowCount(m_builtInSources + m_settings.m_sources.size());
    setRow(m_sourcesTable, 0, {"channels", "built in", "the Radio Paging demodulators and modulators"});
    setRow(m_sourcesTable, 1, {"api", "built in", "the ingest action of the WebAPI and of the MCP server"});

    for (int r = 0; r < m_builtInSources; r++)
    {
        for (int c = 0; c < m_sourcesTable->columnCount(); c++) {
            m_sourcesTable->item(r, c)->setForeground(QBrush(QColor(150, 150, 150)));
        }
    }

    for (int i = 0; i < m_settings.m_sources.size(); i++)
    {
        const MessageCenterFileSetting& f = m_settings.m_sources[i];
        setRow(m_sourcesTable, m_builtInSources + i, {f.m_name, f.typeOrDefault(),
            fileFormat(f).isEmpty() ? fileTarget(f) : QString("%1 (%2)").arg(fileTarget(f), fileFormat(f))});
    }
}

QString MessageCenterGUI::selectedId(QTableWidget *table) const
{
    int row = table->currentRow();
    return ((row >= 0) && table->item(row, 0)) ? table->item(row, 0)->data(idRole).toString() : QString();
}

void MessageCenterGUI::transmitterChanged()
{
    QString alias = m_composer->transmitter();
    QStringList protocols;

    for (const MessageEndpoint& e : m_center->engine().endpoints())
    {
        if ((e.m_role == MessageEndpoint::Transmitter) && (e.m_channel == alias)) {
            protocols = e.m_protocols;
        }
    }

    // A transmitter that did not say what it sends: any
    m_composer->setProtocols(protocols.isEmpty() ? RadioPagingPage::protocols() : protocols);
}

void MessageCenterGUI::post()
{
    if (m_composer->transmitter().isEmpty())
    {
        QMessageBox::information(this, "Page", "No transmitter has announced itself.");
        return;
    }

    RadioPagingPage page = m_composer->page();
    QString error = page.check();

    if (!error.isEmpty())
    {
        QMessageBox::information(this, "Page", error);
        return;
    }

    m_center->getInputMessageQueue()->push(MessageCenter::MsgPostPage::create(page.toRecord()));
}

void MessageCenterGUI::pageAction(int action)
{
    m_center->getInputMessageQueue()->push(MessageCenter::MsgPageAction::create(selectedId(m_queueTable),
        (MessageCenter::MsgPageAction::Action) action));
}

// A route: a name, a filter, the sinks it sends to
void MessageCenterGUI::editRoute(int index, const MessageCenterEngine::Route *preset)
{
    bool isNew = (index < 0);
    MessageCenterEngine::Route route;

    if (!isNew) {
        route = m_settings.m_routes[index];
    } else if (preset) {
        route = *preset;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? "New route" : "Route");
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLineEdit *name = new QLineEdit(route.m_name, &dialog);
    name->setPlaceholderText("name");
    MessageCenterFilterWidget *filter = new MessageCenterFilterWidget(&dialog);
    QStringList sources;

    for (const MessageCenterFileSetting& f : m_settings.m_sources) {
        sources.append(f.m_name);
    }

    filter->showSources(sources);
    filter->setFilter(route.m_filter);
    QFormLayout *nameForm = new QFormLayout();
    nameForm->addRow("Name", name);
    layout->addLayout(nameForm);
    layout->addWidget(filter);
    QList<QCheckBox*> sinkChecks;

    QStringList sinks = {"log", "queue"};

    for (const MessageCenterFileSetting& f : m_settings.m_sinks) {
        sinks.append(f.m_name);
    }

    // The sinks on a line that wraps, after what they are for
    QWidget *sinkLine = new QWidget(&dialog);
    FlowLayout *sinkFlow = new FlowLayout(sinkLine, 0, 10, 3);
    QLabel *sendTo = new QLabel("Send to", sinkLine);
    sendTo->setToolTip("The sinks the messages the route selects go to: the log, the transmit queue and the sinks of I/O");
    sinkFlow->addWidget(sendTo);

    for (const QString& sink : sinks)
    {
        QCheckBox *check = new QCheckBox(sink, sinkLine);
        check->setProperty("sink", sink);
        check->setChecked(route.m_sinks.contains(sink));
        sinkFlow->addWidget(check);
        sinkChecks.append(check);
    }

    layout->addWidget(sinkLine);

    QCheckBox *stop = new QCheckBox("Stop: the routes after this one are not checked for the messages it selects", &dialog);
    stop->setChecked(route.m_stop);
    QCheckBox *enabled = new QCheckBox("Enabled", &dialog);
    enabled->setChecked(route.m_enabled);

    // Decode as: the codepages of every protocol, once each; a message of a protocol without it is sent as it is
    QFormLayout *decodeForm = new QFormLayout();
    QComboBox *decodeCharset = new QComboBox(&dialog);
    decodeCharset->setToolTip("The messages the route selects are decoded again with this codepage before they go to its sinks.\n"
        "A message of a protocol that does not have it is sent as it was received");
    decodeCharset->addItem("As received");
    QList<RadioPagingCharset::Key> keys;
    keys.append(RadioPagingCharset::Key());

    for (const char *protocol : {"pocsag", "flex", "gsc"})
    {
        for (const RadioPagingCharset::Codepage *codepage : RadioPagingCharset::codepages(protocol))
        {
            const RadioPagingCharset::Key key = codepage->key();

            if (!keys.contains(key))
            {
                keys.append(key);
                decodeCharset->addItem(RadioPagingPage::charsetText(key));
            }
        }
    }

    decodeCharset->setCurrentIndex(std::max(0, (int) keys.indexOf(route.m_decodeCharset)));
    QComboBox *decodeType = new QComboBox(&dialog);
    decodeType->setToolTip("The type the messages are decoded as (POCSAG and GSC; FLEX alphanumeric only)");
    decodeType->addItem("As received", QString());
    decodeType->addItem("Alphanumeric", QString("alphanumeric"));
    decodeType->addItem("Numeric", QString("numeric"));
    decodeType->setCurrentIndex(std::max(0, decodeType->findData(route.m_decodeType)));
    decodeForm->addRow("Decode as codepage", decodeCharset);
    decodeForm->addRow("Decode as type", decodeType);

    // A new route: where it goes, before route 1 by default
    QComboBox *position = nullptr;

    if (isNew)
    {
        position = new QComboBox(&dialog);
        const std::vector<MessageCenterEngine::Route>& routes = m_settings.m_routes;

        for (int i = 0; i < (int) routes.size(); i++) {
            position->addItem(QString("Before %1 %2").arg(i + 1).arg(routes[i].m_name), i);
        }

        position->addItem(routes.empty() ? QString("First") : QString("After %1 %2").arg(routes.size()).arg(routes.back().m_name),
            (int) routes.size());
        position->setCurrentIndex(0);
        decodeForm->addRow("Insert", position);
    }

    QPushButton *ok = new QPushButton("OK", &dialog);
    QPushButton *cancel = new QPushButton("Cancel", &dialog);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addWidget(stop);
    layout->addWidget(enabled);
    layout->addLayout(decodeForm);
    layout->addLayout(buttons);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    route.m_name = name->text().trimmed().isEmpty() ? QString("route %1").arg(m_settings.m_routes.size() + 1) : name->text().trimmed();
    route.m_filter = filter->filter();
    route.m_sinks.clear();

    for (QCheckBox *check : sinkChecks)
    {
        if (check->isChecked()) {
            route.m_sinks.append(check->property("sink").toString());
        }
    }

    route.m_stop = stop->isChecked();
    route.m_enabled = enabled->isChecked();
    route.m_decodeCharset = keys.value(decodeCharset->currentIndex());
    route.m_decodeType = decodeType->currentData().toString();

    // A route with an expression that is not valid would select nothing: said now
    QString error = route.m_filter.errors();

    if (!error.isEmpty()) {
        QMessageBox::information(this, "Route", QString("A regular expression is not valid, the route selects nothing:\n%1").arg(error));
    }

    int at = index;

    if (isNew)
    {
        at = std::min(std::max(0, position->currentData().toInt()), (int) m_settings.m_routes.size());
        m_settings.m_routes.insert(m_settings.m_routes.begin() + at, route);
    }
    else
    {
        m_settings.m_routes[index] = route;
    }

    refreshRoutes();
    m_routesTable->selectRow(at);
    applySettings(QStringList("routes"));
}

void MessageCenterGUI::addRoute()
{
    editRoute(-1);
}

void MessageCenterGUI::createRouteFromRecord(const QString& recordJson, const QVariantMap& charset, const QString& type)
{
    MessageRecord record;

    if (!MessageRecord::fromJson(QJsonDocument::fromJson(recordJson.toUtf8()).object(), record)) {
        return;
    }

    MessageCenterEngine::Route route;
    route.m_decodeCharset = RadioPagingPage::charsetKey(charset);
    route.m_decodeType = type;
    route.m_filter.m_protocols = QStringList(record.m_protocol.toLower());
    route.m_filter.m_polarity = (record.m_polarity == MessageRecord::PolarityUnknown) ? -1 : (int) record.m_polarity;
    route.m_filter.m_msgType = record.m_msgType;
    route.m_filter.m_address = QRegularExpression::escape(record.m_address);
    route.m_sinks = QStringList("log");
    route.m_stop = true;
    route.m_name = QString("%1 %2").arg(record.m_protocol.toUpper(), record.m_address);

    if (!route.m_decodeCharset.empty())
    {
        const RadioPagingCharset::Codepage *codepage = RadioPagingCharset::find(record.m_protocol.toStdString(), route.m_decodeCharset);
        route.m_name += " " + (codepage ? QString::fromUtf8(codepage->m_label) : RadioPagingPage::charsetText(route.m_decodeCharset));
    }
    if (!type.isEmpty()) {
        route.m_name += " " + type;
    }

    show();
    raise();
    activateWindow();
    editRoute(-1, &route);
}

void MessageCenterGUI::editSelectedRoute()
{
    int row = m_routesTable->currentRow();

    if ((row >= 0) && (row < (int) m_settings.m_routes.size())) {
        editRoute(row);
    }
}

void MessageCenterGUI::removeRoute()
{
    int row = m_routesTable->currentRow();

    if ((row >= 0) && (row < (int) m_settings.m_routes.size()))
    {
        m_settings.m_routes.erase(m_settings.m_routes.begin() + row);
        refreshRoutes();
        applySettings(QStringList("routes"));
    }
}

// A sink or a source: its type and what it works on
void MessageCenterGUI::editFile(bool sink, int index)
{
    QList<MessageCenterFileSetting>& list = sink ? m_settings.m_sinks : m_settings.m_sources;
    const bool isNew = (index < 0) || (index >= list.size());
    MessageCenterFileSetting setting = isNew ? MessageCenterFileSetting() : list[index];
    const QString oldName = setting.m_name;

    QDialog dialog(this);
    dialog.setWindowTitle(QString("%1 %2").arg(isNew ? "New" : "Edit").arg(sink ? "sink" : "source"));
    QFormLayout *layout = new QFormLayout(&dialog);
    QLineEdit *name = new QLineEdit(setting.m_name, &dialog);
    QComboBox *type = new QComboBox(&dialog);
    type->addItems(sink ? MessageCenterFileSetting::sinkTypes() : MessageCenterFileSetting::sourceTypes());
    type->setCurrentIndex(std::max(0, type->findText(setting.typeOrDefault())));
    type->setToolTip(sink
        ? "file: written as JSON lines or CSV. udp: a datagram for each message.\n"
          "command: run for each message. speech: said for each message.\n"
          "highlight: the row of the message in the log. map: the position in the message, on the Map"
        : "file: a JSON lines file, read as it grows. csv: a CSV log of a message center or of the Pager\n"
          "demodulator, read as it grows. udp: datagrams in JSON or in the format of the Pager demodulator");
    QLineEdit *path = new QLineEdit(setting.m_path, &dialog);
    QPushButton *browse = new QPushButton("...", &dialog);
    QWidget *pathRow = new QWidget(&dialog);
    QHBoxLayout *pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->addWidget(path);
    pathLayout->addWidget(browse);
    QComboBox *format = new QComboBox(&dialog);
    QLineEdit *address = new QLineEdit(setting.m_address, &dialog);
    address->setPlaceholderText(sink ? "IP address to send to" : "127.0.0.1");
    address->setToolTip(sink ? "IP address the datagrams are sent to"
        : "IP address to listen on: 127.0.0.1 (the default) for this computer only, 0.0.0.0 for any");
    QSpinBox *port = new QSpinBox(&dialog);
    port->setRange(1, 65535);
    port->setValue(setting.m_port > 0 ? setting.m_port : 9999);
    QLineEdit *text = new QLineEdit(setting.m_text, &dialog);
    text->setToolTip("${address}, ${message}, ${protocol}, ${type}, ${channel}, ${mode}, ${id}, ${frequency}, ${time} and\n"
        "${0}, ${1}... (the captures of the regular expression of the route) are replaced.\n"
        "A command is split into the program and its arguments first: the message goes into the arguments only");
    QPushButton *color = new QPushButton(&dialog);
    quint32 colorValue = setting.m_color;
    auto showColor = [color, &colorValue]() {
        color->setText(QColor::fromRgba(colorValue).name());
        color->setStyleSheet(QString("QPushButton { background-color: %1; }").arg(QColor::fromRgba(colorValue).name()));
    };
    showColor();

    layout->addRow("Name", name);
    layout->addRow("Type", type);
    layout->addRow("File", pathRow);
    layout->addRow("Format", format);
    layout->addRow("Address", address);
    layout->addRow("Port", port);
    layout->addRow("Text", text);
    layout->addRow("Colour", color);

    // The fields of the type. A row is shown or hidden with its label (QFormLayout::setRowVisible is Qt 6 only)
    auto setRowVisible = [layout](QWidget *field, bool visible) {
        field->setVisible(visible);

        if (QWidget *label = layout->labelForField(field)) {
            label->setVisible(visible);
        }
    };
    auto showFields = [&]() {
        const QString t = type->currentText();
        const bool file = (t == "file") || (t == "csv");
        const bool udp = (t == "udp");
        const bool hasText = (t == "command") || (t == "speech");
        setRowVisible(pathRow, file);
        setRowVisible(format, (sink && (t == "file")) || udp || (!sink && (t == "csv")));
        setRowVisible(address, udp);
        setRowVisible(port, udp);
        setRowVisible(text, hasText);
        setRowVisible(color, t == "highlight");
        int current = format->currentIndex();
        format->blockSignals(true);
        format->clear();

        if (!sink && (t == "csv")) {
            format->addItems({"All rows", "New rows only"});
            format->setToolTip("All rows: the rows in the file are read, then those added. New rows only: the rows already\n"
                "there are skipped (a log the Pager demodulator writes to)");
        } else if (udp) {
            format->addItems({"Pager", "JSON"});
            format->setToolTip(sink ? "Pager: the fields of the Pager demodulator, each ended by a NUL. JSON: the record as one line"
                : "The datagrams are read in either format");
        } else {
            format->addItems({"JSON lines", "CSV"});
            format->setToolTip("JSON lines: one record a line, nothing lost. CSV: the columns of the record");
        }

        format->setCurrentIndex(std::max(0, std::min(1, current)));
        format->blockSignals(false);
        text->setPlaceholderText(t == "command" ? "program argument ${message} ..." : "${message}");
    };

    format->addItems({"JSON lines", "CSV"});
    format->setCurrentIndex(std::max(0, std::min(1, setting.m_format)));
    showFields();
    format->setCurrentIndex(std::max(0, std::min(1, setting.m_format)));

    QPushButton *ok = new QPushButton("OK", &dialog);
    QPushButton *cancel = new QPushButton("Cancel", &dialog);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);

    // A sink takes only what routes send to it: the routes are chosen here too. With no route, one that sends
    // every message to it can be made
    QList<QCheckBox*> routeChecks;
    QCheckBox *newRoute = nullptr;

    if (sink)
    {
        QWidget *routesBox = new QWidget(&dialog);
        QVBoxLayout *routesLayout = new QVBoxLayout(routesBox);
        routesLayout->setContentsMargins(0, 0, 0, 0);

        for (int r = 0; r < (int) m_settings.m_routes.size(); r++)
        {
            const MessageCenterEngine::Route& route = m_settings.m_routes[r];
            QCheckBox *check = new QCheckBox(QString("%1 (%2)").arg(route.m_name).arg(filterSummary(route.m_filter)), routesBox);
            check->setChecked(!isNew && route.m_sinks.contains(oldName));
            check->setProperty("route", r);
            routesLayout->addWidget(check);
            routeChecks.append(check);
        }

        newRoute = new QCheckBox("New route: every message to this sink", routesBox);
        newRoute->setToolTip("Adds a route without a filter that sends every message to this sink.\n"
            "Edit it in Routes to select the messages");
        newRoute->setChecked(m_settings.m_routes.empty() && isNew);
        routesLayout->addWidget(newRoute);
        routesBox->setToolTip("The routes that send messages to this sink. A sink no route sends to takes nothing");
        layout->addRow("Routes", routesBox);
    }

    layout->addRow(buttons);

    connect(type, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&]() { showFields(); });
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(color, &QPushButton::clicked, &dialog, [&]() {
        QColor chosen = QColorDialog::getColor(QColor::fromRgba(colorValue), &dialog, "Highlight");

        if (chosen.isValid())
        {
            colorValue = chosen.rgba();
            showColor();
        }
    });
    connect(browse, &QPushButton::clicked, &dialog, [&]() {
        QString file = sink ? QFileDialog::getSaveFileName(&dialog, "File", path->text())
            : QFileDialog::getOpenFileName(&dialog, "File", path->text());

        if (!file.isEmpty()) {
            path->setText(file);
        }
    });

    for (;;)
    {
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        const QString t = type->currentText();
        setting.m_name = name->text().trimmed();
        setting.m_type = (t == "file") ? QString() : t;
        setting.m_path = ((t == "file") || (t == "csv")) ? path->text().trimmed() : QString();
        setting.m_format = (((t == "file") && sink) || (t == "udp") || (!sink && (t == "csv"))) ? format->currentIndex() : 0;
        setting.m_address = (t == "udp") ? address->text().trimmed() : QString();
        setting.m_port = (t == "udp") ? port->value() : 0;
        setting.m_text = ((t == "command") || (t == "speech")) ? text->text() : QString();
        setting.m_color = colorValue;

        QString error = setting.check(sink);

        for (int i = 0; error.isEmpty() && (i < list.size()); i++)
        {
            if ((i != index) && (list[i].m_name == setting.m_name)) {
                error = QString("There is a %1 named %2 already").arg(sink ? "sink" : "source").arg(setting.m_name);
            }
        }

        if (error.isEmpty()) {
            break;
        }

        QMessageBox::information(this, sink ? "Sink" : "Source", error);
    }

    if (isNew) {
        list.append(setting);
    } else {
        list[index] = setting;
    }

    QStringList keys(sink ? "sinks" : "sources");

    // The routes that send to the sink, as chosen; a sink renamed keeps its routes
    if (sink)
    {
        bool routesChanged = false;

        for (QCheckBox *check : routeChecks)
        {
            MessageCenterEngine::Route& route = m_settings.m_routes[check->property("route").toInt()];
            const QStringList before = route.m_sinks;
            route.m_sinks.removeAll(oldName);
            route.m_sinks.removeAll(setting.m_name);

            if (check->isChecked()) {
                route.m_sinks.append(setting.m_name);
            }

            routesChanged = routesChanged || (before != route.m_sinks);
        }

        if (newRoute && newRoute->isChecked())
        {
            MessageCenterEngine::Route route;
            route.m_name = QString("to %1").arg(setting.m_name);
            route.m_sinks = QStringList(setting.m_name);
            m_settings.m_routes.push_back(route);
            routesChanged = true;
        }

        if (routesChanged)
        {
            keys.append("routes");
            refreshRoutes();
        }
    }

    refreshFiles();
    applySettings(keys);
}

void MessageCenterGUI::addSink()
{
    editFile(true, -1);
}

void MessageCenterGUI::addSource()
{
    editFile(false, -1);
}

void MessageCenterGUI::editSelectedSink()
{
    int row = m_sinksTable->currentRow() - m_builtInSinks;

    if ((row >= 0) && (row < m_settings.m_sinks.size())) {
        editFile(true, row);
    }
}

void MessageCenterGUI::editSelectedSource()
{
    int row = m_sourcesTable->currentRow() - m_builtInSources;

    if ((row >= 0) && (row < m_settings.m_sources.size())) {
        editFile(false, row);
    }
}

// The speech engine takes long to start on some systems: it is made when it is first needed
void MessageCenterGUI::speak(const QString& text)
{
#ifdef QT_TEXTTOSPEECH_FOUND
    if (!m_speech) {
        m_speech = new QTextToSpeech(this);
    }

    m_speech->say(text);
#else
    m_errors->appendPlainText(QString("%1 Text to speech is not in this build: %2")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(text));
    m_errors->setVisible(true);
#endif
}

void MessageCenterGUI::importFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Import messages", QString(), "Logs (*.jsonl *.json *.csv);;All files (*)");

    if (!path.isEmpty()) {
        m_center->getInputMessageQueue()->push(MessageCenter::MsgImportFile::create(path));
    }
}

void MessageCenterGUI::exportLog()
{
    QString path = QFileDialog::getSaveFileName(this, "Export messages", "messages.csv", "CSV (*.csv)");

    if (path.isEmpty()) {
        return;
    }

    const std::vector<MessageRecord> records = m_center->engine().select(m_logFilter->filter());
    QFile file(path);
    bool written = file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && (file.write(MessageRecord::csvHeader().join(',').toUtf8() + "\n") >= 0);

    for (size_t i = 0; written && (i < records.size()); i++) {
        written = file.write(records[i].toCSV().toUtf8() + "\n") >= 0;
    }

    if (!written) {
        QMessageBox::warning(this, "Export messages", QString("Cannot write %1").arg(path));
    }
}

void MessageCenterGUI::removeSink()
{
    int row = m_sinksTable->currentRow() - m_builtInSinks;

    if ((row >= 0) && (row < m_settings.m_sinks.size()))
    {
        QString name = m_settings.m_sinks[row].m_name;
        m_settings.m_sinks.removeAt(row);

        // The routes do not send to a sink that is gone
        for (MessageCenterEngine::Route& route : m_settings.m_routes) {
            route.m_sinks.removeAll(name);
        }

        refreshFiles();
        refreshRoutes();
        applySettings({"sinks", "routes"});
    }
}

void MessageCenterGUI::removeSource()
{
    int row = m_sourcesTable->currentRow() - m_builtInSources;

    if ((row >= 0) && (row < m_settings.m_sources.size()))
    {
        m_settings.m_sources.removeAt(row);
        refreshFiles();
        applySettings(QStringList("sources"));
    }
}
