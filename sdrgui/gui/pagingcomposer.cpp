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

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

#include "radiopagingflexscheduler.h"
#include "radiopaginggsc.h"
#include "radiopagingtxstream.h"
#include "pagingcomposer.h"

namespace {

// The types, as the user reads them
QString typeLabel(const QString& type)
{
    if (type == "short_message") {
        return "Short message";
    }

    // special_numeric: Special numeric
    QString label = type;
    label.replace('_', ' ');
    return label.left(1).toUpper() + label.mid(1);
}

const int pocsagSpeeds[3] = {512, 1200, 2400};

} // namespace

PagingComposer::PagingComposer(QWidget *parent) :
    QWidget(parent)
{
    QGridLayout *layout = new QGridLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(3);

    m_transmitterLabel = new QLabel(tr("Transmitter"), this);
    m_transmitter = new QComboBox(this);
    m_transmitter->setToolTip(tr("The transmitter that will send the page: it claims the page when it is ready"));
    m_protocolLabel = new QLabel(tr("Protocol"), this);
    m_protocol = new QComboBox(this);
    m_protocol->setToolTip(tr("Protocol of this message"));

    m_addressLabel = new QLabel(tr("Address"), this);
    m_address = new QLineEdit(this);
    m_address->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{0,10}"), m_address));
    m_function = new QComboBox(this);
    m_capCode = new QLabel(this);
    m_capCode->setWordWrap(true);
    m_capCode->setToolTip(tr("GSC cap code: index I, group G1 G0, address A2 A1 A0 and the suffix F. The preamble is (I + G0) mod 10, "
        "word 1 comes from G1 G0, word 2 from A2 A1 A0. The suffix gives the function: 1-4 and 5-8 are A-D, 9 and 0 are A and B. "
        "Without a suffix the function is the one selected"));
    m_priority = new QCheckBox(tr("Priority"), this);
    m_priority->setToolTip(tr("FLEX priority address"));
    m_groupLabel = new QLabel(tr("Group"), this);
    m_group = new QLineEdit(this);
    m_group->setPlaceholderText(tr("capcodes separated by commas"));
    m_group->setToolTip(tr("FLEX group message (STD-43A 5.2): the capcodes of 2 to 64 pagers, separated by commas. The address above is "
        "not used. Each pager gets a short instruction with a temporary address and a frame, then the message goes to that address in "
        "that frame. Numeric, alphanumeric, tone and short messages; no copies"));

    m_type = new QComboBox(this);
    m_polarity = new QComboBox(this);
    m_polarity->addItems({tr("Default"), tr("Normal"), tr("Inverted")});
    m_polarity->setToolTip(tr("Polarity of this message; Default is the polarity set for the channel"));
    m_overrideLabel = new QLabel(tr("Speed"), this);
    m_speed = new QComboBox(this);
    m_speed->addItem(tr("Default"));

    for (int speed : pocsagSpeeds) {
        m_speed->addItem(QString::number(speed));
    }

    m_speed->setToolTip(tr("POCSAG speed of this message; Default is the speed set for the channel"));

    m_textLabel = new QLabel(tr("Text"), this);
    m_text = new QLineEdit(this);
    m_browse = new QToolButton(this);
    m_browse->setText("...");
    m_browse->setToolTip(tr("Choose the sound file"));
    m_send = new QPushButton(tr("Send"), this);
    m_send->setToolTip(tr("Queue the message for transmission"));

    m_voiceSource = new QComboBox(this);
    m_voiceSource->addItems({tr("File"), tr("Microphone")});
    m_voiceSource->setToolTip(tr("Voice of the page: a sound file, or the microphone. The microphone is taken 2 s after the page "
        "(the alert time) until End or the voice length"));
    m_micInput = new QToolButton(this);
    m_micInput->setText(tr("Audio input..."));
    m_micInput->setToolTip(tr("Select the audio input of the microphone"));

    m_shortLabel = new QLabel(tr("Short"), this);
    m_shortType = new QComboBox(this);
    m_shortType->addItems({tr("Numeric"), tr("Source"), tr("Numbered")});
    m_shortType->setToolTip(tr("FLEX short message (STD-43A Table 3.9.2-1): digits of the vector, a source code, or a source code and a "
        "message number"));
    m_source = new QSpinBox(this);
    m_source->setRange(0, 7);
    m_source->setPrefix(tr("source "));
    m_source->setToolTip(tr("Source code 0 to 7"));

    m_binaryLabel = new QLabel(tr("Header"), this);
    m_blocking = new QSpinBox(this);
    m_blocking->setRange(1, 16);
    m_blocking->setPrefix("B ");
    m_blocking->setToolTip(tr("FLEX binary: blocking length B, bits of a character (1 is the default of the standard); the bits of the "
        "text (4 a hex digit) are a multiple of it"));
    m_mailDrop = new QCheckBox("M", this);
    m_mailDrop->setToolTip(tr("FLEX binary: mail drop flag M, the message can be handled separately from ordinary messages"));
    m_rightToLeft = new QCheckBox("D", this);
    m_rightToLeft->setToolTip(tr("FLEX binary: display direction D, right to left; not allowed with a blocking length of 1"));
    m_headerMessage = new QCheckBox("H", this);
    m_headerMessage->setToolTip(tr("FLEX binary: header message flag H, a transparent message of the same number follows"));
    m_statusInfo = new QCheckBox("I", this);
    m_statusInfo->setToolTip(tr("FLEX binary: status information field enabler I, the first 8 data bits name the encoding method "
        "(reserved)"));
    m_secureType = new QComboBox(this);
    m_secureType->addItems({tr("Alphanumeric"), tr("Separate"), tr("Binary")});
    m_secureType->setToolTip(tr("FLEX secure message type t1 t0: 00 alphanumeric (7-bit characters), 01 data defined separately "
        "(hex digits, not terminated), 10 binary (hex digits)"));

    m_scheduleLabel = new QLabel(tr("Copies"), this);
    m_retransmit = new QSpinBox(this);
    m_retransmit->setRange(0, RadioPagingFlexEncoder::m_maxRetransmit);
    m_retransmit->setToolTip(tr("Network mode: copies sent after the message. POCSAG 0 to 10; FLEX 0 to 15, numbered types "
        "(alphanumeric, binary, numbered short message) with message numbering, sent with the same number and R = 0"));
    m_retransmitInterval = new QDoubleSpinBox(this);
    m_retransmitInterval->setDecimals(1);
    m_retransmitInterval->setRange(-1.0, RadioPagingPage::m_maxScheduleSeconds);
    m_retransmitInterval->setValue(-1.0);
    m_retransmitInterval->setSpecialValueText(tr("Default"));
    m_retransmitInterval->setPrefix(tr("every "));
    m_retransmitInterval->setSuffix(" s");
    m_retransmitInterval->setToolTip(tr("Seconds between the copies. Default: 10 s for POCSAG, 128 frames (240 s) for FLEX. FLEX is "
        "rounded up to whole frames of 1.875 s"));
    m_sendDelay = new QDoubleSpinBox(this);
    m_sendDelay->setDecimals(1);
    m_sendDelay->setRange(0.0, RadioPagingPage::m_maxScheduleSeconds);
    m_sendDelay->setSpecialValueText(tr("No delay"));
    m_sendDelay->setPrefix(tr("delay "));
    m_sendDelay->setSuffix(" s");
    m_sendDelay->setToolTip(tr("Network mode: seconds from the queueing to the first transmission. FLEX is rounded up to whole frames "
        "of 1.875 s"));

    QHBoxLayout *shortRow = new QHBoxLayout();
    shortRow->addWidget(m_shortType);
    shortRow->addWidget(m_source);
    QHBoxLayout *binaryRow = new QHBoxLayout();
    binaryRow->setSpacing(3);
    binaryRow->addWidget(m_blocking);
    binaryRow->addWidget(m_mailDrop);
    binaryRow->addWidget(m_rightToLeft);
    binaryRow->addWidget(m_headerMessage);
    binaryRow->addWidget(m_statusInfo);
    binaryRow->addWidget(m_secureType);
    QHBoxLayout *addressRow = new QHBoxLayout();
    addressRow->setSpacing(3);
    addressRow->addWidget(m_address, 1);
    addressRow->addWidget(m_function);
    addressRow->addWidget(m_priority);
    QHBoxLayout *typeRow = new QHBoxLayout();
    typeRow->setSpacing(3);
    typeRow->addWidget(m_type);
    typeRow->addWidget(m_overrideLabel);
    typeRow->addWidget(m_speed);
    typeRow->addWidget(m_polarity);
    QHBoxLayout *textRow = new QHBoxLayout();
    textRow->setSpacing(3);
    textRow->addWidget(m_voiceSource);
    textRow->addWidget(m_text, 1);
    textRow->addWidget(m_browse);
    textRow->addWidget(m_micInput);
    QHBoxLayout *scheduleRow = new QHBoxLayout();
    scheduleRow->setSpacing(3);
    scheduleRow->addWidget(m_retransmit);
    scheduleRow->addWidget(m_retransmitInterval);
    scheduleRow->addWidget(m_sendDelay);

    int row = 0;
    layout->addWidget(m_transmitterLabel, row, 0);
    layout->addWidget(m_transmitter, row++, 1, 1, 2);
    layout->addWidget(m_protocolLabel, row, 0);
    layout->addWidget(m_protocol, row++, 1, 1, 2);
    layout->addWidget(m_addressLabel, row, 0);
    layout->addLayout(addressRow, row++, 1, 1, 2);
    layout->addWidget(m_capCode, row++, 1, 1, 2);
    layout->addWidget(m_groupLabel, row, 0);
    layout->addWidget(m_group, row++, 1, 1, 2);
    layout->addWidget(new QLabel(tr("Type"), this), row, 0);
    layout->addLayout(typeRow, row++, 1, 1, 2);
    layout->addWidget(m_shortLabel, row, 0);
    layout->addLayout(shortRow, row++, 1, 1, 2);
    layout->addWidget(m_binaryLabel, row, 0);
    layout->addLayout(binaryRow, row++, 1, 1, 2);
    layout->addWidget(m_textLabel, row, 0);
    layout->addLayout(textRow, row, 1);
    layout->addWidget(m_send, row++, 2);
    layout->addWidget(m_scheduleLabel, row, 0);
    layout->addLayout(scheduleRow, row++, 1, 1, 2);
    layout->setColumnStretch(1, 1);

    m_transmitterLabel->setVisible(false);
    m_transmitter->setVisible(false);
    setProtocols(RadioPagingPage::protocols());

    connect(m_protocol, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        fillFunctions();
        fillTypes();
        display();
        emit selectionChanged();
    });
    connect(m_transmitter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PagingComposer::selectionChanged);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { display(); emit selectionChanged(); });
    connect(m_voiceSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { display(); emit selectionChanged(); });
    connect(m_shortType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PagingComposer::display);
    connect(m_secureType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PagingComposer::display);
    connect(m_retransmit, QOverload<int>::of(&QSpinBox::valueChanged), this, &PagingComposer::display);
    connect(m_browse, &QToolButton::clicked, this, &PagingComposer::browse);
    connect(m_micInput, &QToolButton::clicked, this, &PagingComposer::microphoneInputRequested);
    connect(m_send, &QPushButton::clicked, this, &PagingComposer::sendRequested);
    connect(m_text, &QLineEdit::returnPressed, this, &PagingComposer::sendRequested);
    connect(m_address, &QLineEdit::returnPressed, this, &PagingComposer::sendRequested);
    connect(m_address, &QLineEdit::textChanged, this, &PagingComposer::display);
    connect(m_group, &QLineEdit::textChanged, this, &PagingComposer::display);
    connect(m_group, &QLineEdit::returnPressed, this, &PagingComposer::sendRequested);
}

void PagingComposer::setProtocols(const QStringList& protocols)
{
    QString current = protocol();
    QSignalBlocker blocker(m_protocol);

    m_protocol->clear();

    for (const QString& p : protocols) {
        m_protocol->addItem(p.toUpper(), p);
    }

    int index = m_protocol->findData(current);
    m_protocol->setCurrentIndex((index >= 0) ? index : 0);
    m_protocolLabel->setVisible(protocols.size() > 1);
    m_protocol->setVisible(protocols.size() > 1);
    fillFunctions();
    fillTypes();
    display();
}

QString PagingComposer::protocol() const
{
    return m_protocol->currentData().toString();
}

void PagingComposer::setTransmitters(const QStringList& aliases)
{
    QString current = m_transmitter->currentData().toString();
    QStringList shown;

    for (int i = 0; i < m_transmitter->count(); i++) {
        shown.append(m_transmitter->itemData(i).toString());
    }

    m_transmitterLabel->setVisible(true);
    m_transmitter->setVisible(true);

    // The selector keeps its choice when the transmitters did not change
    if (shown == aliases) {
        return;
    }

    {
        QSignalBlocker blocker(m_transmitter);
        m_transmitter->clear();

        for (const QString& alias : aliases) {
            m_transmitter->addItem(alias, alias);
        }

        int index = m_transmitter->findData(current);
        m_transmitter->setCurrentIndex((index >= 0) ? index : 0);
    }

    emit selectionChanged();
}

QString PagingComposer::transmitter() const
{
    return m_transmitter->isVisible() ? m_transmitter->currentData().toString() : QString();
}

void PagingComposer::setNetworkMode(bool network)
{
    m_network = network;
    display();
}

void PagingComposer::setFlexNumbering(bool numbering)
{
    m_flexNumbering = numbering;
    display();
}

void PagingComposer::setMicrophone(bool allowed)
{
    m_microphone = allowed;
    display();
}

bool PagingComposer::microphoneSelected() const
{
    return m_microphone && (typeName() == "voice") && (m_voiceSource->currentIndex() == 1);
}

void PagingComposer::setSendText(const QString& text)
{
    m_sendText = text;
    display();
}

void PagingComposer::setSendEnabled(bool enabled)
{
    m_send->setEnabled(enabled);
}

QString PagingComposer::typeName() const
{
    return m_type->currentData().toString();
}

// The function of a message: the function A to D of POCSAG, the address number 1 to 4 (function A to D) of GSC
void PagingComposer::fillFunctions()
{
    QSignalBlocker blocker(m_function);
    int index = std::max(0, m_function->currentIndex());
    bool gsc = (protocol() == "gsc");

    m_function->clear();

    for (int i = 0; i < 4; i++) {
        m_function->addItem(gsc ? QString("%1 (%2)").arg(i + 1).arg(QChar(RadioPagingGsc::functionLetter(i + 1))) : QString(QChar('A' + i)));
    }

    m_function->setCurrentIndex(index);
    m_function->setToolTip(gsc ? tr("GSC address number 1 to 4, function A to D") : tr("POCSAG function A to D"));
}

// The types of the protocol; the selected type is kept if it is there
void PagingComposer::fillTypes()
{
    QSignalBlocker blocker(m_type);
    QString current = typeName();

    m_type->clear();

    for (const QString& type : RadioPagingPage::types(protocol())) {
        m_type->addItem(typeLabel(type), type);
    }

    int index = m_type->findData(current);
    m_type->setCurrentIndex((index >= 0) ? index : 0);
}

// The fields of the protocol and the type
void PagingComposer::display()
{
    const QString proto = protocol();
    const QString type = typeName();
    const bool pocsag = (proto == "pocsag");
    const bool flex = (proto == "flex");
    const bool gsc = (proto == "gsc");
    const bool voice = gsc && (type == "voice");
    const bool microphone = microphoneSelected();
    const bool shortMessage = flex && (type == "short_message");
    const bool binary = flex && (type == "binary");
    const bool secure = flex && (type == "secure");
    const bool hexText = binary || (secure && (m_secureType->currentIndex() != 0));

    m_addressLabel->setText(flex ? tr("Capcode") : tr("Address"));
    m_address->setPlaceholderText(pocsag ? tr("0 to 2097151") : flex ? tr("capcode") : tr("5 to 7 digits"));
    m_address->setToolTip(gsc ? tr("GSC cap code: 6 digits (index, group, 3 address digits), 5 digits (index 0) or 7 digits "
        "(the 6 digits and the suffix, that gives the function)")
        : flex ? tr("FLEX capcode: short 1 to 1933312, long 2101249 to 4297068542") : tr("POCSAG address 0 to 2097151"));
    m_function->setVisible(!flex);

    // GSC: the digits of the cap code; a suffix digit sets the function
    const QString capCode = m_address->text().trimmed();
    const bool suffix = gsc && (capCode.size() == 7);
    m_capCode->setVisible(gsc && !capCode.isEmpty());
    m_capCode->setText(gsc ? QString::fromStdString(RadioPagingGsc::capCodeText(capCode.toStdString())) : QString());
    m_function->setEnabled(!suffix);

    if (suffix && (m_function->count() == 4)) {
        m_function->setCurrentIndex(RadioPagingGsc::suffixAddressNumber(capCode.at(6).toLatin1()) - 1);
    }
    m_priority->setVisible(flex);
    m_groupLabel->setVisible(flex);
    m_group->setVisible(flex);
    m_overrideLabel->setVisible(pocsag);
    m_speed->setVisible(pocsag);

    // A voice page: the voice comes from a file, or from the microphone after the alert time
    m_voiceSource->setVisible(voice && m_microphone);
    m_micInput->setVisible(microphone);
    m_browse->setVisible(voice && !microphone);
    m_text->setVisible(!microphone);
    m_textLabel->setVisible(!microphone);
    m_text->setEnabled(type != "tone");
    m_textLabel->setText(voice ? tr("File") : tr("Text"));

    // FLEX short message (the digits of numeric ones), HEX/binary (B and the flags of header word 2), secure
    m_shortLabel->setVisible(shortMessage);
    m_shortType->setVisible(shortMessage);
    m_source->setVisible(shortMessage);
    m_source->setEnabled(shortMessage && (m_shortType->currentIndex() != 0));

    if (shortMessage) {
        m_text->setEnabled(m_shortType->currentIndex() == 0);
    }

    const bool alpha = flex && (type == "alphanumeric");
    m_binaryLabel->setVisible(binary || secure || alpha);
    m_binaryLabel->setText(secure ? tr("Type") : tr("Header"));

    for (QWidget *w : QList<QWidget*>({m_blocking, m_rightToLeft, m_headerMessage, m_statusInfo})) {
        w->setVisible(binary);
    }

    m_mailDrop->setVisible(binary || alpha);
    m_mailDrop->setToolTip(binary ? tr("FLEX binary: mail drop flag M, the message can be handled separately from ordinary messages")
        : tr("FLEX alphanumeric: mail drop flag M, the message can be handled separately from ordinary messages"));

    m_secureType->setVisible(secure);

    // Copies and send delay: network mode (POCSAG and FLEX); FLEX copies of the numbered types on a network
    // with message numbering (STD-43A 4.3.7)
    const bool scheduled = m_network && !gsc;
    const bool flexNumbered = (type == "alphanumeric") || binary || (type == "numbered_numeric") || (type == "numbered_special_numeric")
        || (shortMessage && (m_shortType->currentIndex() == 2));
    const bool copies = (!flex || (m_flexNumbering && flexNumbered)) && m_group->text().trimmed().isEmpty();
    m_scheduleLabel->setVisible(scheduled);
    m_retransmit->setVisible(scheduled);
    m_retransmitInterval->setVisible(scheduled);
    m_sendDelay->setVisible(scheduled);
    m_retransmit->setMaximum(flex ? RadioPagingFlexEncoder::m_maxRetransmit : RadioPagingPocsagTxStream::m_maxRepeat);
    m_retransmit->setEnabled(copies);
    m_retransmitInterval->setEnabled(copies && (m_retransmit->value() > 0));

    m_text->setPlaceholderText(voice ? tr("WAV file") : hexText ? tr("Hex digits") : shortMessage ? tr("Digits") : QString());
    m_text->setToolTip(voice
        ? tr("Path of a WAV file (8 to 32 bit PCM or float, up to 76.8 kHz). Sent after an alert of 2 s, cut at the voice length. "
             "Enter to send")
        : hexText
        ? tr("Hex digits: digit i holds the data bits 4i to 4i+3, its least significant bit sent first. Binary: the bits are a "
             "multiple of B. Enter to send")
        : tr("Message text. Numeric: 0-9, space, U, -, ], [. Alphanumeric: 7-bit characters. Enter to send"));

    if (!m_sendText.isEmpty()) {
        m_send->setText(m_sendText);
    } else {
        m_send->setText(microphone ? tr("Send voice") : tr("Send"));
    }
}

void PagingComposer::browse()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Sound file"), m_text->text().trimmed(), tr("WAV files (*.wav);;All files (*)"));

    if (!file.isEmpty()) {
        m_text->setText(file);
    }
}

RadioPagingPage PagingComposer::page() const
{
    RadioPagingPage p;
    const QString type = typeName();
    const bool tone = (type == "tone");
    const bool voice = (type == "voice");

    p.m_transmitter = transmitter();
    p.m_protocol = protocol();
    p.m_type = type;
    p.m_address = m_address->text().trimmed();
    p.m_function = std::max(0, m_function->currentIndex());
    p.m_priority = m_priority->isVisible() && m_priority->isChecked();
    p.m_groupMembers = m_group->isVisible() ? m_group->text().trimmed() : QString();
    p.m_text = (tone || voice || !m_text->isEnabled()) ? QString() : m_text->text();
    p.m_voiceFile = (voice && !microphoneSelected()) ? m_text->text().trimmed() : QString();
    p.m_speed = (m_speed->isVisible() && (m_speed->currentIndex() > 0)) ? pocsagSpeeds[m_speed->currentIndex() - 1] : 0;
    p.m_polarity = m_polarity->currentIndex() - 1;
    p.m_shortType = m_shortType->currentIndex();
    p.m_source = m_source->isEnabled() ? m_source->value() : 0;
    p.m_blocking = m_blocking->value();
    p.m_mailDrop = m_mailDrop->isChecked();
    p.m_rightToLeft = m_rightToLeft->isChecked();
    p.m_headerMessage = m_headerMessage->isChecked();
    p.m_statusInfo = m_statusInfo->isChecked();
    p.m_secureType = m_secureType->currentIndex();

    if (m_retransmit->isVisible())
    {
        p.m_retransmit = m_retransmit->isEnabled() ? m_retransmit->value() : 0;
        p.m_retransmitInterval = m_retransmitInterval->value();
        p.m_sendDelay = m_sendDelay->value();
    }

    return p;
}

QString PagingComposer::summary() const
{
    RadioPagingPage p = page();
    QString text = microphoneSelected() ? tr("microphone") : (p.m_type == "voice") ? p.m_voiceFile : p.m_text;
    QString address = p.m_address;

    if (p.m_protocol == "pocsag") {
        address += QString(" %1").arg(QChar('A' + p.m_function));
    } else if (p.m_protocol == "gsc") {
        int function;
        p.gscAddress(address, function);
        address += QString("/%1 (%2)").arg(function + 1).arg(QChar(RadioPagingGsc::functionLetter(function + 1)));
    } else if (p.m_priority) {
        address += tr(" priority");
    }

    QString line = QString("%1 %2: %3").arg(address, typeLabel(p.m_type), text);

    if ((p.m_retransmit > 0) || (p.m_sendDelay > 0.0))
    {
        line += tr(" (%1 copies, interval %2, delay %3 s)").arg(p.m_retransmit)
            .arg((p.m_retransmitInterval < 0.0) ? tr("default") : QString("%1 s").arg(p.m_retransmitInterval)).arg(p.m_sendDelay);
    }

    return line;
}
