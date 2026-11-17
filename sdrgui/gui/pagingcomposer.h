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

#ifndef SDRGUI_GUI_PAGINGCOMPOSER_H_
#define SDRGUI_GUI_PAGINGCOMPOSER_H_

#include <QString>
#include <QStringList>
#include <QWidget>

#include "export.h"
#include "radiopagingpage.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

// The fields of a message to page (POCSAG, FLEX or GSC), as the paging modulator and the message center
// send it. The user of the composer says what can be composed (setProtocols, setNetworkMode,
// setFlexNumbering, setMicrophone, setTransmitters); the composer shows the fields of the protocol and the
// type chosen and gives the RadioPagingPage composed, which the user checks and sends.
class SDRGUI_API PagingComposer : public QWidget
{
    Q_OBJECT
public:
    explicit PagingComposer(QWidget *parent = nullptr);

    //!< The protocols that can be composed (pocsag, flex, gsc): the selector is there when there are several.
    //!< The protocol and the type chosen are kept if they are still there
    void setProtocols(const QStringList& protocols);
    QString protocol() const;
    //!< The transmitters that can send the page (message center): the selector is there when this is called
    void setTransmitters(const QStringList& aliases);
    QString transmitter() const;
    //!< Copies and send delay are for the network mode (POCSAG and FLEX)
    void setNetworkMode(bool network);
    //!< FLEX copies are for the numbered types of a network with message numbering (STD-43A 4.3.7)
    void setFlexNumbering(bool numbering);
    //!< A GSC voice page can come from the microphone, which the user takes
    void setMicrophone(bool allowed);
    bool microphoneSelected() const;
    //!< The text of the button, empty for its own (Send, Send voice)
    void setSendText(const QString& text);
    void setSendEnabled(bool enabled);
    //!< The page composed: no id
    RadioPagingPage page() const;
    //!< The page in a line of text, for the log of the user
    QString summary() const;

signals:
    void sendRequested();
    void microphoneInputRequested();
    //!< The protocol, the type, the transmitter or the voice source changed
    void selectionChanged();

private:
    QComboBox *m_transmitter;
    QLabel *m_transmitterLabel;
    QComboBox *m_protocol;
    QLabel *m_protocolLabel;
    QLabel *m_addressLabel;
    QLineEdit *m_address;
    QLabel *m_capCode;                  //!< GSC: what the digits of the cap code are
    QLabel *m_groupLabel;
    QLineEdit *m_group;
    QComboBox *m_function;
    QCheckBox *m_priority;
    QComboBox *m_type;
    QLabel *m_textLabel;
    QLineEdit *m_text;
    QToolButton *m_browse;
    QComboBox *m_voiceSource;
    QToolButton *m_micInput;
    QPushButton *m_send;
    QLabel *m_overrideLabel;
    QComboBox *m_speed;
    QComboBox *m_polarity;
    QLabel *m_shortLabel;
    QComboBox *m_shortType;
    QSpinBox *m_source;
    QLabel *m_binaryLabel;
    QSpinBox *m_blocking;
    QCheckBox *m_mailDrop;
    QCheckBox *m_rightToLeft;
    QCheckBox *m_headerMessage;
    QCheckBox *m_statusInfo;
    QComboBox *m_secureType;
    QLabel *m_scheduleLabel;
    QSpinBox *m_retransmit;
    QDoubleSpinBox *m_retransmitInterval;
    QDoubleSpinBox *m_sendDelay;

    bool m_network = false;
    bool m_flexNumbering = true;
    bool m_microphone = false;
    QString m_sendText;

    QString typeName() const;
    void fillTypes();
    void fillFunctions();
    void display();
    void browse();
};

#endif // SDRGUI_GUI_PAGINGCOMPOSER_H_
