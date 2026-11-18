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

// Self test of the reading of the settings of the Pager demodulator: they are saved by its own settings code and read
// back without it.

#include <cstdio>
#include <string>

#include <QColor>

#include "pagerdemodsettings.h"
#include "gui/pagerimport.h"

namespace {

int passes = 0;
int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());

    if (ok) {
        passes++;
    } else {
        failures++;
    }
}

} // namespace

int main()
{
    PagerDemodSettings pager;
    pager.m_inputFrequencyOffset = 25000;
    pager.m_rfBandwidth = 15000.0f;
    pager.m_fmDeviation = 4000.0f;
    pager.m_modulation = PagerDemodSettings::POCSAG;
    pager.m_decode = PagerDemodSettings::Inverted;
    pager.m_filterAddress = "12345..";
    pager.m_udpEnabled = true;
    pager.m_udpAddress = "10.0.0.7";
    pager.m_udpPort = 5555;
    pager.m_logEnabled = true;
    pager.m_logFilename = "/tmp/pager.csv";
    pager.m_filterDuplicates = true;
    pager.m_title = "Pager A";
    pager.m_sevenbit = {65};
    pager.m_unicode = {1040};
    pager.m_reverse = true;

    for (int column : {PagerDemodSettings::MESSAGE_COL_ADDRESS, PagerDemodSettings::MESSAGE_COL_ALPHA})
    {
        QSharedPointer<PagerDemodSettings::NotificationSettings> n = QSharedPointer<PagerDemodSettings::NotificationSettings>::create();
        n->m_matchColumn = column;
        n->m_regExp = (column == PagerDemodSettings::MESSAGE_COL_ADDRESS) ? "1234567" : "FIRE (\\w+)";
        n->m_speech = "${message}";
        n->m_command = (column == PagerDemodSettings::MESSAGE_COL_ADDRESS) ? "" : "notify ${1}";
        n->m_highlight = true;
        n->m_highlightColor = QColor(Qt::green).rgba();
        n->m_plotOnMap = (column == PagerDemodSettings::MESSAGE_COL_ALPHA);
        pager.m_notificationSettings.append(n);
    }

    PagerImport::Config c;
    check(PagerImport::parse(pager.serialize(), c), "the settings of the Pager demodulator are read");
    check((c.m_inputFrequencyOffset == 25000) && (c.m_rfBandwidth == 15000.0f) && (c.m_fmDeviation == 4000.0f), "frequency, bandwidth, deviation");
    check(c.m_modulation == PagerImport::Config::Pocsag, "modulation");
    check(c.m_decode == 1, "decode");
    check(c.m_filterAddress == "12345..", "address filter");
    check(c.m_udpEnabled && (c.m_udpAddress == "10.0.0.7") && (c.m_udpPort == 5555), "UDP");
    check(c.m_logEnabled && (c.m_logFilename == "/tmp/pager.csv"), "log");
    check(c.m_filterDuplicates && (c.m_title == "Pager A"), "duplicates and title");
    check((c.m_characters.size() == 1) && (c.m_characters[0] == qMakePair(65, 1040)) && c.m_reverse && !c.hebrewCharacters(),
        "characters mapped by the user and right-to-left");
    check(c.m_notifications.size() == 2, "two notifications");

    if (c.m_notifications.size() == 2)
    {
        const PagerImport::Notification& a = c.m_notifications[0];
        const PagerImport::Notification& b = c.m_notifications[1];
        check((a.m_column == PagerImport::Notification::Address) && (a.m_regExp == "1234567") && (a.m_speech == "${message}")
            && a.m_command.isEmpty() && a.m_highlight && (a.m_highlightColor == QColor(Qt::green).rgba()) && !a.m_plotOnMap,
            "notification on the address");
        check((b.m_column == PagerImport::Notification::Alpha) && (b.m_regExp == "FIRE (\\w+)") && (b.m_command == "notify ${1}")
            && b.m_plotOnMap, "notification on the alpha message");
    }

    PagerDemodSettings defaults;
    PagerImport::Config d;
    check(PagerImport::parse(defaults.serialize(), d) && (d.m_modulation == PagerImport::Config::Auto) && d.m_notifications.isEmpty()
        && d.m_characters.isEmpty() && !d.m_reverse && !d.m_udpEnabled, "the default settings");

    PagerDemodSettings hebrew;
    for (int i = 0; i < 27; i++)
    {
        hebrew.m_sevenbit.append(0x60 + i);
        hebrew.m_unicode.append(0x05D0 + i);
    }
    check(PagerImport::parse(hebrew.serialize(), d) && d.hebrewCharacters(), "the Hebrew preset is seen");
    check(!PagerImport::parse(QByteArray("not settings"), d), "what is not settings is refused");

    std::printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
