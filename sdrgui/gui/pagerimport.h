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

#ifndef SDRGUI_GUI_PAGERIMPORT_H_
#define SDRGUI_GUI_PAGERIMPORT_H_

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

#include "export.h"

// The settings of the instances of the Pager demodulator, read from what they save (the plugin is not linked): for the
// Radio Paging demodulator and the Message Center to take them over
class SDRGUI_API PagerImport
{
public:
    //! A notification of the Pager demodulator
    struct Notification
    {
        enum Column { Address, Message, Alpha, Numeric };
        int m_column = Address;
        QString m_regExp;
        QString m_speech;
        QString m_command;
        bool m_highlight = false;
        quint32 m_highlightColor = 0xffff0000;
        bool m_plotOnMap = false;
    };

    //! The settings of an instance. Exported on its own: the export of the class does not cover its nested classes
    struct SDRGUI_API Config
    {
        enum Modulation { Auto, Pocsag, Flex };
        qint32 m_inputFrequencyOffset = 0;
        float m_rfBandwidth = 20000.0f;
        float m_fmDeviation = 4500.0f;
        int m_modulation = Auto;
        int m_decode = 0;                   //!< Standard, inverted, numeric, alphanumeric, heuristic
        QString m_filterAddress;            //!< Regular expression on the whole address
        bool m_udpEnabled = false;
        QString m_udpAddress = "127.0.0.1";
        quint16 m_udpPort = 9999;
        bool m_logEnabled = false;
        QString m_logFilename;
        bool m_filterDuplicates = false;
        QList<QPair<qint32, qint32>> m_characters;  //!< Characters mapped by the user: 7-bit code, Unicode code point
        bool m_reverse = false;             //!< The characters in the reverse order (right-to-left)
        QString m_title;
        QList<Notification> m_notifications;

        //! The characters are the Hebrew preset of the Pager demodulator: 0x60 to 0x7A, alef to tav
        bool hebrewCharacters() const;
    };

    //! What an import does to one setting or item: the action, what it is, what there is now and what it would be
    struct SDRGUI_API Change
    {
        enum Action { Add, Update, Delete, Keep, Skip };    //!< Skip: what the Pager demodulator has is not taken over
        Action m_action = Keep;
        QString m_item;
        QString m_current;
        QString m_imported;                 //!< Skip: the value of the Pager demodulator and why it is not taken
        static Change make(Action action, const QString& item, const QString& current, const QString& imported) {
            Change c;
            c.m_action = action;
            c.m_item = item;
            c.m_current = current;
            c.m_imported = imported;
            return c;
        }
        //! Update if they differ, else keep
        static Change compare(const QString& item, const QString& current, const QString& imported) {
            return make((current == imported) ? Keep : Update, item, current, imported);
        }
        static QString actionName(Action action);
    };

    //! An instance of the Pager demodulator: where it is and what it saves
    struct Instance
    {
        int m_deviceSetIndex = -1;
        int m_channelIndex = -1;
        QString m_title;
        qint64 m_frequency = 0;             //!< Absolute, Hz, 0 if unknown
        QByteArray m_settings;
        QString id() const { return QString("R%1:%2").arg(m_deviceSetIndex).arg(m_channelIndex); }
        QString label() const;
    };

    //! The Pager demodulators there are. The settings are those of the window when it is there (the channel does not keep
    //! the notifications the window changed)
    static QList<Instance> instances();
    //! The settings an instance saves; false if they can not be read
    static bool parse(const QByteArray& settings, Config& config);
    static QString columnName(int column);
    static QString modulationName(int modulation);

    //! A dialog: the instance to take, and what the import would do (preview(): each setting or item with its action,
    //! its value now and its value imported). True and the instance chosen when the user accepts
    static bool choose(QWidget *parent, const QString& title, const QString& intro,
        const std::function<QList<Change>(const Config& config, const Instance& instance)>& preview, Instance& chosen, Config& config);
};

#endif // SDRGUI_GUI_PAGERIMPORT_H_
