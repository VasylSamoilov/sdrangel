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
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
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

#include "messagecentergui.h"

namespace {

const int idRole = Qt::UserRole;
const int voiceRole = Qt::UserRole + 1;

QTableWidget *makeTable(const QStringList& headers, QWidget *parent)
{
    QTableWidget *table = new QTableWidget(parent);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setMinimumHeight(140);
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
    if (!f.m_aliases.isEmpty()) {
        parts << "alias " + f.m_aliases.join(",");
    }
    if (!f.m_address.isEmpty()) {
        parts << "address " + f.m_address;
    }
    if (!f.m_msgType.isEmpty()) {
        parts << f.m_msgType;
    }
    if (!f.m_text.isEmpty()) {
        parts << "text '" + f.m_text + "'";
    }
    if (f.m_hideDuplicates) {
        parts << "no duplicates";
    }
    if (f.m_voiceOnly) {
        parts << "voice files";
    }

    return parts.isEmpty() ? QString("everything") : parts.join(", ");
}

} // namespace

// ---------------------------------------------------------------------------
// Filter widget
// ---------------------------------------------------------------------------

MessageCenterFilterWidget::MessageCenterFilterWidget(QWidget *parent) :
    QWidget(parent)
{
    QGridLayout *layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_protocol = new QComboBox(this);
    m_protocol->addItems({"Any protocol", "pocsag", "flex", "gsc"});
    m_direction = new QComboBox(this);
    m_direction->addItems({"RX and TX", "RX", "TX"});
    m_polarity = new QComboBox(this);
    m_polarity->addItems({"Any polarity", "Normal", "Inverted"});
    m_type = new QComboBox(this);
    m_type->addItems({"Any type", "alphanumeric", "numeric", "tone_only", "voice"});
    m_alias = new QLineEdit(this);
    m_alias->setPlaceholderText("channel alias");
    m_address = new QLineEdit(this);
    m_address->setPlaceholderText("address (* ?)");
    m_text = new QLineEdit(this);
    m_text->setPlaceholderText("text contains");
    m_hideDuplicates = new QCheckBox("Hide duplicates", this);
    m_voiceOnly = new QCheckBox("Voice files", this);

    layout->addWidget(m_protocol, 0, 0);
    layout->addWidget(m_direction, 0, 1);
    layout->addWidget(m_polarity, 0, 2);
    layout->addWidget(m_type, 0, 3);
    layout->addWidget(m_alias, 1, 0);
    layout->addWidget(m_address, 1, 1);
    layout->addWidget(m_text, 1, 2, 1, 2);
    layout->addWidget(m_hideDuplicates, 2, 0, 1, 2);
    layout->addWidget(m_voiceOnly, 2, 2, 1, 2);

    for (QComboBox *combo : {m_protocol, m_direction, m_polarity, m_type}) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MessageCenterFilterWidget::changed);
    }
    for (QLineEdit *edit : {m_alias, m_address, m_text}) {
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
    if (!m_alias->text().trimmed().isEmpty()) {
        f.m_aliases = QStringList(m_alias->text().trimmed());
    }

    f.m_address = m_address->text().trimmed();
    f.m_text = m_text->text();
    f.m_hideDuplicates = m_hideDuplicates->isChecked();
    f.m_voiceOnly = m_voiceOnly->isChecked();
    return f;
}

void MessageCenterFilterWidget::setFilter(const MessageCenterFilter& f)
{
    m_protocol->setCurrentIndex(f.m_protocols.isEmpty() ? 0 : std::max(0, m_protocol->findText(f.m_protocols.first())));
    m_direction->setCurrentIndex(f.m_direction < 0 ? 0 : (f.m_direction == (int) MessageRecord::DirectionRX ? 1 : 2));
    m_polarity->setCurrentIndex(f.m_polarity < 0 ? 0 : (f.m_polarity == (int) MessageRecord::PolarityNormal ? 1 : 2));
    m_type->setCurrentIndex(f.m_msgType.isEmpty() ? 0 : std::max(0, m_type->findText(f.m_msgType)));
    m_alias->setText(f.m_aliases.isEmpty() ? QString() : f.m_aliases.first());
    m_address->setText(f.m_address);
    m_text->setText(f.m_text);
    m_hideDuplicates->setChecked(f.m_hideDuplicates);
    m_voiceOnly->setChecked(f.m_voiceOnly);
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

QWidget *MessageCenterGUI::makeRollup(const QString& title)
{
    QWidget *widget = new QWidget(getRollupContents());
    widget->setWindowTitle(title);
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

    RollupContents *rollupContents = getRollupContents();
    buildLog(makeRollup("Log"));
    buildEndpoints(makeRollup("Channels"));
    buildQueue(makeRollup("Queue"));
    buildCompose(makeRollup("Page"));
    buildRoutes(makeRollup("Routes"));
    buildFiles(makeRollup("Files"));
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
    m_logTable = makeTable({"Time", "Dir", "Channel", "Protocol", "Address", "Type", "Text", "Voice"}, container);
    m_logTable->setMinimumHeight(220);
    m_playButton = new QPushButton("Play the voice", container);
    m_playButton->setToolTip("Play the recorded voice of the selected message (double-click a row does the same)");
    layout->addWidget(m_logFilter);
    layout->addWidget(m_logTable);
    layout->addWidget(m_playButton);

    connect(m_logFilter, &MessageCenterFilterWidget::changed, this, [this]() { m_shownLogged = -1; refreshLog(); });
    connect(m_playButton, &QPushButton::clicked, this, &MessageCenterGUI::playSelected);
    connect(m_logTable, &QTableWidget::cellDoubleClicked, this, [this]() { playSelected(); });
}

void MessageCenterGUI::buildEndpoints(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    m_endpointsTable = makeTable({"Channel", "Role", "Status", "Protocols", "Queue", "Seen"}, container);
    m_endpointsTable->setToolTip("The channels that announced themselves");
    layout->addWidget(m_endpointsTable);
}

void MessageCenterGUI::buildQueue(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    m_queueTable = makeTable({"Page", "Transmitter", "Protocol", "Address", "Text", "State", "Tries", "Reason"}, container);
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
    QFormLayout *layout = new QFormLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);

    m_transmitter = new QComboBox(container);
    m_transmitter->setToolTip("The transmitter that will send the page: it claims the page when it is ready");
    m_protocol = new QComboBox(container);
    m_address = new QLineEdit(container);
    m_address->setPlaceholderText("address / capcode");
    m_function = new QComboBox(container);
    m_function->addItems({"1 (A)", "2 (B)", "3 (C)", "4 (D)"});
    m_function->setToolTip("POCSAG function A to D, GSC address number 1 to 4");
    m_type = new QComboBox(container);
    m_type->addItems({"alphanumeric", "numeric", "tone_only", "voice"});
    m_text = new QLineEdit(container);
    m_voiceFile = new QLineEdit(container);
    m_voiceFile->setPlaceholderText("WAV file");
    QPushButton *browse = new QPushButton("...", container);
    QHBoxLayout *voiceRow = new QHBoxLayout();
    voiceRow->addWidget(m_voiceFile);
    voiceRow->addWidget(browse);
    m_speed = new QComboBox(container);
    m_speed->addItems({"Default speed", "512", "1200", "2400"});
    m_polarity = new QComboBox(container);
    m_polarity->addItems({"Default polarity", "Normal", "Inverted"});
    QPushButton *post = new QPushButton("Post the page", container);

    layout->addRow("Transmitter", m_transmitter);
    layout->addRow("Protocol", m_protocol);
    layout->addRow("Address", m_address);
    layout->addRow("Function", m_function);
    layout->addRow("Type", m_type);
    layout->addRow("Text", m_text);
    layout->addRow("Voice file", voiceRow);
    layout->addRow("Speed", m_speed);
    layout->addRow("Polarity", m_polarity);
    layout->addRow(post);

    connect(m_transmitter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MessageCenterGUI::transmitterChanged);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MessageCenterGUI::typeChanged);
    connect(browse, &QPushButton::clicked, this, &MessageCenterGUI::browseVoice);
    connect(post, &QPushButton::clicked, this, &MessageCenterGUI::post);
    typeChanged();
}

void MessageCenterGUI::buildRoutes(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    QLabel *help = new QLabel("The records go through the routes in order. A route sends the records its filter selects to its sinks "
        "(log, queue or a file); stop: no route after it. A record no route selects is logged.", container);
    help->setWordWrap(true);
    m_routesTable = makeTable({"Route", "Filter", "Sinks", "Stop", "On"}, container);
    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *add = new QPushButton("Add...", container);
    QPushButton *edit = new QPushButton("Edit...", container);
    QPushButton *remove = new QPushButton("Remove", container);
    buttons->addWidget(add);
    buttons->addWidget(edit);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addWidget(help);
    layout->addWidget(m_routesTable);
    layout->addLayout(buttons);

    connect(add, &QPushButton::clicked, this, &MessageCenterGUI::addRoute);
    connect(edit, &QPushButton::clicked, this, &MessageCenterGUI::editSelectedRoute);
    connect(remove, &QPushButton::clicked, this, &MessageCenterGUI::removeRoute);
    connect(m_routesTable, &QTableWidget::cellDoubleClicked, this, &MessageCenterGUI::editSelectedRoute);
}

void MessageCenterGUI::buildFiles(QWidget *container)
{
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    m_sinksTable = makeTable({"Sink", "File", "Format"}, container);
    m_sourcesTable = makeTable({"Source", "File"}, container);
    m_errors = new QPlainTextEdit(container);
    m_errors->setReadOnly(true);
    m_errors->setMaximumBlockCount(200);
    m_errors->setMaximumHeight(70);
    QHBoxLayout *sinkButtons = new QHBoxLayout();
    QPushButton *addSink = new QPushButton("Add sink...", container);
    QPushButton *removeSink = new QPushButton("Remove", container);
    sinkButtons->addWidget(addSink);
    sinkButtons->addWidget(removeSink);
    sinkButtons->addStretch();
    QHBoxLayout *sourceButtons = new QHBoxLayout();
    QPushButton *addSource = new QPushButton("Add source...", container);
    QPushButton *removeSource = new QPushButton("Remove", container);
    sourceButtons->addWidget(addSource);
    sourceButtons->addWidget(removeSource);
    sourceButtons->addStretch();
    layout->addWidget(m_sinksTable);
    layout->addLayout(sinkButtons);
    layout->addWidget(m_sourcesTable);
    layout->addLayout(sourceButtons);
    layout->addWidget(m_errors);

    connect(addSink, &QPushButton::clicked, this, &MessageCenterGUI::addSink);
    connect(removeSink, &QPushButton::clicked, this, &MessageCenterGUI::removeSink);
    connect(addSource, &QPushButton::clicked, this, &MessageCenterGUI::addSource);
    connect(removeSource, &QPushButton::clicked, this, &MessageCenterGUI::removeSource);
}

void MessageCenterGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;
    getRollupContents()->saveState(m_rollupState);
    applySettings(QStringList("rollupState"));
}

void MessageCenterGUI::onMenuDialogCalled(const QPoint &p)
{
    if (m_contextMenuType == ContextMenuType::ContextMenuChannelSettings)
    {
        BasicFeatureSettingsDialog dialog(this);
        dialog.setTitle(m_settings.m_title);
        dialog.setDefaultTitle(m_displayedName);
        dialog.move(p);
        new DialogPositioner(&dialog, false);
        dialog.exec();

        m_settings.m_title = dialog.getTitle();
        setTitle(m_settings.m_title);
        setTitleColor(m_settings.m_rgbColor);
        applySettings({"title", "rgbColor"});
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
    refreshRoutes();
    refreshFiles();
    m_doApplySettings = true;
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
    if (MessageCenter::MsgUpdated::match(message))
    {
        refresh();
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

    for (const QString& error : m_center->takeErrors()) {
        m_errors->appendPlainText(QString("%1 %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(error));
    }
}

// The newest messages of the log that the filter selects; made again when the log changed
void MessageCenterGUI::refreshLog()
{
    const MessageCenterEngine& engine = m_center->engine();

    if (engine.logged() == m_shownLogged) {
        return;
    }

    m_shownLogged = engine.logged();
    std::vector<MessageRecord> records = engine.select(m_logFilter->filter());
    size_t first = (records.size() > (size_t) m_maxLogRows) ? records.size() - m_maxLogRows : 0;
    int rows = (int) (records.size() - first);
    QString selected = selectedId(m_logTable);

    m_logTable->setUpdatesEnabled(false);
    m_logTable->setRowCount(rows);

    for (int i = 0; i < rows; i++)
    {
        const MessageRecord& r = records[first + i];
        QString file = r.m_protocolData.value("voice_file").toString();
        QString text = r.m_text;

        if (r.m_msgType == "voice" && text.isEmpty()) {
            text = r.m_protocolData.contains("voice_end") ? QString("voice, %1 s").arg(r.m_protocolData.value("voice_seconds").toDouble(), 0, 'f', 1)
                : QString("voice");
        }

        setRow(m_logTable, i, {
            QDateTime::fromMSecsSinceEpoch(r.m_timestamp).toString("yyyy-MM-dd HH:mm:ss"),
            r.m_direction == MessageRecord::DirectionTX ? "TX" : "RX",
            r.m_channelAlias, QString("%1 %2").arg(r.m_protocol).arg(r.m_mode), r.m_address, r.m_msgType, text,
            file.isEmpty() ? QString() : QFileInfo(file).fileName()
        });
        m_logTable->item(i, 0)->setData(idRole, r.m_id);
        m_logTable->item(i, 0)->setData(voiceRole, file);
        m_logTable->item(i, 6)->setToolTip(text);

        if (r.m_duplicate != MessageRecord::DuplicateNone)
        {
            for (int c = 0; c < m_logTable->columnCount(); c++) {
                m_logTable->item(i, c)->setForeground(QColor("#777777"));
            }
        }
        if (r.m_id == selected) {
            m_logTable->selectRow(i);
        }
    }

    m_logTable->setUpdatesEnabled(true);
    m_logTable->scrollToBottom();
}

void MessageCenterGUI::refreshEndpoints()
{
    static const char *statuses[] = {"offline", "idle", "busy"};
    const QMap<QString, MessageEndpoint>& endpoints = m_center->engine().endpoints();
    m_endpointsTable->setRowCount(endpoints.size());
    int row = 0;

    for (const MessageEndpoint& e : endpoints)
    {
        setRow(m_endpointsTable, row++, {
            e.m_alias, e.m_role == MessageEndpoint::Transmitter ? "transmitter" : "receiver", statuses[(int) e.m_status],
            e.m_protocols.join(", "),
            e.m_role == MessageEndpoint::Transmitter ? QString("%1/%2").arg(e.m_queueDepth).arg(e.m_capacity) : QString(),
            QDateTime::fromMSecsSinceEpoch(e.m_lastSeen).toString("HH:mm:ss")
        });
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
            MessageCenterPage::stateName(p.m_state), QString::number(p.m_attempts), p.m_reason
        });
        m_queueTable->item(i, 0)->setData(idRole, p.m_record.m_id);

        if (p.m_record.m_id == selected) {
            m_queueTable->selectRow(i);
        }
    }
}

void MessageCenterGUI::refreshComposeTransmitters()
{
    QString current = m_transmitter->currentData().toString();
    QStringList aliases;

    for (const MessageEndpoint& e : m_center->engine().endpoints())
    {
        if ((e.m_role == MessageEndpoint::Transmitter) && (e.m_status != MessageEndpoint::Offline)) {
            aliases.append(e.m_alias);
        }
    }

    // Only when the transmitters changed: the combo keeps its choice
    QStringList shown;

    for (int i = 0; i < m_transmitter->count(); i++) {
        shown.append(m_transmitter->itemData(i).toString());
    }

    if (shown == aliases) {
        return;
    }

    m_transmitter->blockSignals(true);
    m_transmitter->clear();

    for (const QString& alias : aliases) {
        m_transmitter->addItem(alias, alias);
    }

    int index = m_transmitter->findData(current);
    m_transmitter->setCurrentIndex(index >= 0 ? index : 0);
    m_transmitter->blockSignals(false);
    transmitterChanged();
}

void MessageCenterGUI::refreshRoutes()
{
    const std::vector<MessageCenterEngine::Route>& routes = m_settings.m_routes;
    m_routesTable->setRowCount((int) routes.size());

    for (int i = 0; i < (int) routes.size(); i++)
    {
        setRow(m_routesTable, i, {routes[i].m_name, filterSummary(routes[i].m_filter), routes[i].m_sinks.join(", "),
            routes[i].m_stop ? "stop" : "", routes[i].m_enabled ? "yes" : "no"});
    }
}

void MessageCenterGUI::refreshFiles()
{
    m_sinksTable->setRowCount(m_settings.m_sinks.size());

    for (int i = 0; i < m_settings.m_sinks.size(); i++) {
        setRow(m_sinksTable, i, {m_settings.m_sinks[i].m_name, m_settings.m_sinks[i].m_path, m_settings.m_sinks[i].m_format == 0 ? "JSON lines" : "CSV"});
    }

    m_sourcesTable->setRowCount(m_settings.m_sources.size());

    for (int i = 0; i < m_settings.m_sources.size(); i++) {
        setRow(m_sourcesTable, i, {m_settings.m_sources[i].m_name, m_settings.m_sources[i].m_path});
    }
}

QString MessageCenterGUI::selectedId(QTableWidget *table) const
{
    int row = table->currentRow();
    return ((row >= 0) && table->item(row, 0)) ? table->item(row, 0)->data(idRole).toString() : QString();
}

// A voice message with its file is played with the player of the system
void MessageCenterGUI::playSelected()
{
    int row = m_logTable->currentRow();
    QString file = ((row >= 0) && m_logTable->item(row, 0)) ? m_logTable->item(row, 0)->data(voiceRole).toString() : QString();

    if (file.isEmpty()) {
        return;
    }

    if (!QFileInfo::exists(file))
    {
        QMessageBox::information(this, "Voice message", QString("%1\nis not there any more (deleted with the old voice files).").arg(file));
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

void MessageCenterGUI::transmitterChanged()
{
    QString alias = m_transmitter->currentData().toString();
    QStringList protocols;

    for (const MessageEndpoint& e : m_center->engine().endpoints())
    {
        if ((e.m_role == MessageEndpoint::Transmitter) && (e.m_alias == alias)) {
            protocols = e.m_protocols;
        }
    }

    if (protocols.isEmpty()) {
        protocols = QStringList({"pocsag", "flex", "gsc"});
    }

    m_protocol->clear();
    m_protocol->addItems(protocols);
}

void MessageCenterGUI::typeChanged()
{
    bool voice = (m_type->currentText() == "voice");
    bool tone = (m_type->currentText() == "tone_only");
    m_text->setEnabled(!voice && !tone);
    m_voiceFile->setEnabled(voice);
}

void MessageCenterGUI::browseVoice()
{
    QString file = QFileDialog::getOpenFileName(this, "Sound file", m_voiceFile->text(), "WAV files (*.wav);;All files (*)");

    if (!file.isEmpty()) {
        m_voiceFile->setText(file);
    }
}

MessageRecord MessageCenterGUI::composedPage() const
{
    return MessageCenter::pageRecord(m_transmitter->currentData().toString(), m_protocol->currentText(), m_address->text().trimmed(),
        m_function->currentIndex(), 0, m_type->currentText(), m_text->isEnabled() ? m_text->text() : QString(),
        m_voiceFile->text().trimmed(), (m_speed->currentIndex() > 0) ? m_speed->currentText().toInt() : 0,
        (m_polarity->currentIndex() == 1) ? "normal" : (m_polarity->currentIndex() == 2) ? "inverted" : "");
}

void MessageCenterGUI::post()
{
    if (m_transmitter->currentData().toString().isEmpty())
    {
        QMessageBox::information(this, "Page", "No transmitter has announced itself.");
        return;
    }

    m_center->getInputMessageQueue()->push(MessageCenter::MsgPostPage::create(composedPage()));
}

void MessageCenterGUI::pageAction(int action)
{
    m_center->getInputMessageQueue()->push(MessageCenter::MsgPageAction::create(selectedId(m_queueTable),
        (MessageCenter::MsgPageAction::Action) action));
}

// A route: a name, a filter, the sinks it sends to
void MessageCenterGUI::editRoute(int index)
{
    bool isNew = (index < 0);
    MessageCenterEngine::Route route;

    if (!isNew) {
        route = m_settings.m_routes[index];
    }

    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? "New route" : "Route");
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLineEdit *name = new QLineEdit(route.m_name, &dialog);
    name->setPlaceholderText("name");
    MessageCenterFilterWidget *filter = new MessageCenterFilterWidget(&dialog);
    filter->setFilter(route.m_filter);
    layout->addWidget(name);
    layout->addWidget(filter);
    QList<QCheckBox*> sinkChecks;

    for (const QString& sink : m_center->engine().sinkNames())
    {
        QCheckBox *check = new QCheckBox(QString("Send to %1").arg(sink), &dialog);
        check->setProperty("sink", sink);
        check->setChecked(route.m_sinks.contains(sink) || (isNew && (sink == "log")));
        layout->addWidget(check);
        sinkChecks.append(check);
    }

    QCheckBox *stop = new QCheckBox("Stop: no route after this one", &dialog);
    stop->setChecked(route.m_stop);
    QCheckBox *enabled = new QCheckBox("Enabled", &dialog);
    enabled->setChecked(route.m_enabled);
    QPushButton *ok = new QPushButton("OK", &dialog);
    QPushButton *cancel = new QPushButton("Cancel", &dialog);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addWidget(stop);
    layout->addWidget(enabled);
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

    if (isNew) {
        m_settings.m_routes.push_back(route);
    } else {
        m_settings.m_routes[index] = route;
    }

    refreshRoutes();
    applySettings(QStringList("routes"));
}

void MessageCenterGUI::addRoute()
{
    editRoute(-1);
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

// A file to write (a sink) or to read (a source)
void MessageCenterGUI::editFile(bool sink)
{
    QDialog dialog(this);
    dialog.setWindowTitle(sink ? "New sink" : "New source");
    QFormLayout *layout = new QFormLayout(&dialog);
    QLineEdit *name = new QLineEdit(&dialog);
    QLineEdit *path = new QLineEdit(&dialog);
    QPushButton *browse = new QPushButton("...", &dialog);
    QHBoxLayout *pathRow = new QHBoxLayout();
    pathRow->addWidget(path);
    pathRow->addWidget(browse);
    QComboBox *format = new QComboBox(&dialog);
    format->addItems({"JSON lines", "CSV"});
    QPushButton *ok = new QPushButton("OK", &dialog);
    QPushButton *cancel = new QPushButton("Cancel", &dialog);
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addRow("Name", name);
    layout->addRow("File", pathRow);

    if (sink) {
        layout->addRow("Format", format);
    }

    layout->addRow(buttons);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(browse, &QPushButton::clicked, &dialog, [&]() {
        QString file = sink ? QFileDialog::getSaveFileName(&dialog, "File", path->text()) : QFileDialog::getOpenFileName(&dialog, "File", path->text());

        if (!file.isEmpty()) {
            path->setText(file);
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    MessageCenterFileSetting file;
    file.m_name = name->text().trimmed();
    file.m_path = path->text().trimmed();
    file.m_format = format->currentIndex();

    if (file.m_name.isEmpty() || file.m_path.isEmpty() || (file.m_name == "log") || (file.m_name == "queue"))
    {
        QMessageBox::information(this, "File", "A name (not log or queue) and a file are needed.");
        return;
    }

    if (sink) {
        m_settings.m_sinks.append(file);
    } else {
        m_settings.m_sources.append(file);
    }

    refreshFiles();
    applySettings(QStringList(sink ? "sinks" : "sources"));
}

void MessageCenterGUI::addSink()
{
    editFile(true);
}

void MessageCenterGUI::addSource()
{
    editFile(false);
}

void MessageCenterGUI::removeSink()
{
    int row = m_sinksTable->currentRow();

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
    int row = m_sourcesTable->currentRow();

    if ((row >= 0) && (row < m_settings.m_sources.size()))
    {
        m_settings.m_sources.removeAt(row);
        refreshFiles();
        applySettings(QStringList("sources"));
    }
}
