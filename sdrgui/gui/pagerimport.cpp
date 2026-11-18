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

#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "channel/channelapi.h"
#include "channel/channelgui.h"
#include "device/deviceapi.h"
#include "device/deviceset.h"
#include "maincore.h"
#include "util/simpleserializer.h"

#include "pagerimport.h"

namespace {

// The settings of the Pager demodulator (pagerdemodsettings.cpp): the tags it writes
enum PagerTag {
    TagInputFrequencyOffset = 1,
    TagRfBandwidth = 2,
    TagFmDeviation = 3,
    TagFilterAddress = 5,
    TagDecode = 6,
    TagUdpEnabled = 7,
    TagUdpAddress = 8,
    TagUdpPort = 9,
    TagTitle = 13,
    TagReverse = 22,
    TagSevenBit = 23,
    TagUnicode = 24,
    TagLogFilename = 25,
    TagLogEnabled = 26,
    TagNotifications = 31,
    TagFilterDuplicates = 32,
    TagModulation = 35
};

// Its message columns (PagerDemodSettings::MESSAGE_COL_*) a notification matches
enum PagerColumn {
    ColAddress = 2,
    ColMessage = 3,
    ColAlpha = 5,
    ColNumeric = 6
};

const char * const pagerURI = "sdrangel.channel.pagerdemod";

} // namespace

QString PagerImport::Instance::label() const
{
    QString text = QString("%1 %2").arg(id()).arg(m_title);

    if (m_frequency > 0) {
        text += QString(" (%1 Hz)").arg(m_frequency);
    }

    return text;
}

QList<PagerImport::Instance> PagerImport::instances()
{
    QList<Instance> list;
    std::vector<DeviceSet*>& deviceSets = MainCore::instance()->getDeviceSets();

    for (int d = 0; d < (int) deviceSets.size(); d++)
    {
        DeviceSet *deviceSet = deviceSets[d];

        for (int c = 0; c < deviceSet->getNumberOfChannels(); c++)
        {
            ChannelAPI *channel = deviceSet->getChannelAt(c);

            if (!channel || (channel->getURI() != pagerURI)) {
                continue;
            }

            Instance instance;
            instance.m_deviceSetIndex = d;
            instance.m_channelIndex = c;
            channel->getTitle(instance.m_title);
            instance.m_settings = channel->serialize();

            if (deviceSet->m_deviceAPI)
            {
                QList<quint64> centres = deviceSet->m_deviceAPI->getCenterFrequency();

                if (!centres.isEmpty()) {
                    instance.m_frequency = (qint64) centres.first() + channel->getCenterFrequency();
                }
            }

            // The window keeps the notifications the user set: its settings when it is there
            for (QWidget *widget : QApplication::allWidgets())
            {
                ChannelGUI *gui = qobject_cast<ChannelGUI*>(widget);

                if (gui && (gui->getDeviceSetIndex() == d) && (gui->getIndex() == c))
                {
                    instance.m_settings = gui->serialize();
                    break;
                }
            }

            list.append(instance);
        }
    }

    return list;
}

bool PagerImport::parse(const QByteArray& settings, Config& config)
{
    SimpleDeserializer d(settings);

    if (!d.isValid() || (d.getVersion() != 1)) {
        return false;
    }

    config = Config();
    quint32 port;
    QByteArray blob;
    d.readS32(TagInputFrequencyOffset, &config.m_inputFrequencyOffset, 0);
    d.readFloat(TagRfBandwidth, &config.m_rfBandwidth, 20000.0f);
    d.readFloat(TagFmDeviation, &config.m_fmDeviation, 4500.0f);
    d.readString(TagFilterAddress, &config.m_filterAddress, "");
    d.readS32(TagDecode, &config.m_decode, 0);
    d.readBool(TagUdpEnabled, &config.m_udpEnabled, false);
    d.readString(TagUdpAddress, &config.m_udpAddress, "127.0.0.1");
    d.readU32(TagUdpPort, &port, 9999);
    config.m_udpPort = ((port > 1023) && (port <= 65535)) ? (quint16) port : 9999;
    d.readString(TagTitle, &config.m_title, "Pager Demodulator");
    d.readString(TagLogFilename, &config.m_logFilename, "pager_log.csv");
    d.readBool(TagLogEnabled, &config.m_logEnabled, false);
    d.readBool(TagFilterDuplicates, &config.m_filterDuplicates, false);
    d.readS32(TagModulation, &config.m_modulation, Config::Auto);
    config.m_modulation = std::max((int) Config::Auto, std::min((int) Config::Flex, config.m_modulation));

    // The characters the user mapped (7-bit code to Unicode code point, two lists), and the reverse reading order
    QList<qint32> lists[2];

    for (int i = 0; i < 2; i++)
    {
        if (d.readBlob(i == 0 ? TagSevenBit : TagUnicode, &blob) && !blob.isEmpty())
        {
            QDataStream stream(blob);
            stream >> lists[i];
        }
    }

    const int mapped = std::min(lists[0].size(), lists[1].size());

    for (int i = 0; i < mapped; i++) {
        config.m_characters.append(qMakePair(lists[0][i], lists[1][i]));
    }

    d.readBool(TagReverse, &config.m_reverse, false);

    // The notifications: a list of the settings each one saves
    if (d.readBlob(TagNotifications, &blob))
    {
        QList<QByteArray> items;
        QDataStream stream(blob);
        stream >> items;

        for (const QByteArray& item : items)
        {
            SimpleDeserializer n(item);

            if (!n.isValid() || (n.getVersion() != 1)) {
                continue;
            }

            Notification notification;
            qint32 column, color;
            n.readS32(1, &column, ColAddress);
            n.readString(2, &notification.m_regExp);
            n.readString(3, &notification.m_speech);
            n.readString(4, &notification.m_command);
            n.readBool(5, &notification.m_highlight, false);
            n.readS32(6, &color, (qint32) 0xffff0000);
            n.readBool(7, &notification.m_plotOnMap, false);
            notification.m_highlightColor = (quint32) color;
            notification.m_column = (column == ColMessage) ? Notification::Message : (column == ColAlpha) ? Notification::Alpha
                : (column == ColNumeric) ? Notification::Numeric : Notification::Address;
            config.m_notifications.append(notification);
        }
    }

    return true;
}

QString PagerImport::columnName(int column)
{
    static const char *names[] = {"address", "message", "alpha", "numeric"};
    return ((column >= 0) && (column < 4)) ? QString(names[column]) : QString();
}

QString PagerImport::modulationName(int modulation)
{
    static const char *names[] = {"POCSAG and FLEX", "POCSAG", "FLEX"};
    return ((modulation >= 0) && (modulation < 3)) ? QString(names[modulation]) : QString();
}

bool PagerImport::Config::hebrewCharacters() const
{
    // The preset of the character encoding dialog of the Pager demodulator: 27 letters from 0x60
    if (m_characters.size() != 27) {
        return false;
    }

    for (int i = 0; i < 27; i++)
    {
        if (!m_characters.contains(qMakePair(0x60 + i, 0x05D0 + i))) {
            return false;
        }
    }

    return true;
}

QString PagerImport::Change::actionName(Action action)
{
    static const char *names[] = {"add", "update", "delete", "keep", "not imported"};
    return names[action];
}

bool PagerImport::choose(QWidget *parent, const QString& title, const QString& intro,
    const std::function<QList<Change>(const Config& config, const Instance& instance)>& preview, Instance& chosen, Config& config)
{
    QList<Instance> list = instances();
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(900, 520);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *label = new QLabel(intro, &dialog);
    label->setWordWrap(true);
    QComboBox *instance = new QComboBox(&dialog);
    QLabel *summary = new QLabel(&dialog);
    QTableWidget *changes = new QTableWidget(0, 4, &dialog);
    changes->setHorizontalHeaderLabels({"Action", "Setting", "Current", "Pager demodulator"});
    changes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    changes->setSelectionBehavior(QAbstractItemView::SelectRows);
    changes->verticalHeader()->hide();
    changes->horizontalHeader()->setStretchLastSection(true);
    changes->setWordWrap(false);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Import");
    layout->addWidget(label);
    layout->addWidget(instance);
    layout->addWidget(summary);
    layout->addWidget(changes, 1);
    layout->addWidget(buttons);

    for (const Instance& i : list) {
        instance->addItem(i.label());
    }

    auto show = [&]() {
        int index = instance->currentIndex();
        Config c;
        changes->setRowCount(0);

        if ((index < 0) || (index >= list.size()))
        {
            summary->setText("There is no Pager demodulator to import from.");
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            return;
        }
        if (!parse(list[index].m_settings, c))
        {
            summary->setText("The settings of this Pager demodulator can not be read.");
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            return;
        }

        // The actions in muted colours: add green, update amber, delete red, keep and not imported grey
        const QList<Change> rows = preview(c, list[index]);
        int count[5] = {0, 0, 0, 0, 0};
        changes->setRowCount(rows.size());

        for (int r = 0; r < rows.size(); r++)
        {
            const Change& change = rows[r];
            static const char *colours[] = {"#5fa860", "#c8a040", "#c86450", "#888888", "#888888"};
            QTableWidgetItem *action = new QTableWidgetItem(Change::actionName(change.m_action));
            action->setForeground(QColor(colours[change.m_action]));
            changes->setItem(r, 0, action);
            changes->setItem(r, 1, new QTableWidgetItem(change.m_item));
            changes->setItem(r, 2, new QTableWidgetItem(change.m_current));
            changes->setItem(r, 3, new QTableWidgetItem(change.m_imported));

            for (int column = 1; column < 4; column++)
            {
                changes->item(r, column)->setToolTip(changes->item(r, column)->text());

                if ((change.m_action == Change::Keep) || (change.m_action == Change::Skip)) {
                    changes->item(r, column)->setForeground(QColor("#888888"));
                }
            }

            count[change.m_action]++;
        }

        changes->resizeColumnsToContents();
        const int changed = count[Change::Add] + count[Change::Update] + count[Change::Delete];
        summary->setText(changed == 0 ? QString("Nothing would change.")
            : QString("%1 to add, %2 to update, %3 to delete, %4 kept, %5 not imported.")
                .arg(count[Change::Add]).arg(count[Change::Update]).arg(count[Change::Delete]).arg(count[Change::Keep]).arg(count[Change::Skip]));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(changed > 0);
    };

    QObject::connect(instance, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&](int) { show(); });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    show();

    if ((dialog.exec() != QDialog::Accepted) || (instance->currentIndex() < 0)) {
        return false;
    }

    chosen = list[instance->currentIndex()];
    return parse(chosen.m_settings, config);
}
