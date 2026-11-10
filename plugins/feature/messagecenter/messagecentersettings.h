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

#ifndef INCLUDE_MESSAGECENTERSETTINGS_H
#define INCLUDE_MESSAGECENTERSETTINGS_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <vector>

#include "messagecenterengine.h"

class Serializable;

// A file the message center writes records to (a sink) or reads records from (a source)
struct MessageCenterFileSetting
{
    QString m_name;
    QString m_path;
    int m_format = 0;           //!< Sink: MessageCenterFileSink::Format
};

struct MessageCenterSettings
{
    QString m_title;
    quint32 m_rgbColor;
    int m_logSize;                          //!< Messages kept in the log
    int m_claimTimeoutSec;                  //!< A page given to a transmitter that does not claim it in this time goes back to the queue
    int m_silentSec;                        //!< A channel that does not announce itself for this time is offline
    std::vector<MessageCenterEngine::Route> m_routes;
    QList<MessageCenterFileSetting> m_sinks;
    QList<MessageCenterFileSetting> m_sources;
    Serializable *m_rollupState;
    int m_workspaceIndex;
    QByteArray m_geometryBytes;
    bool m_hidden;

    MessageCenterSettings();
    void resetToDefaults();
    void setRollupState(Serializable *rollupState) { m_rollupState = rollupState; }
    QByteArray serialize() const;
    bool deserialize(const QByteArray& data);
    void applySettings(const QStringList& settingsKeys, const MessageCenterSettings& settings);

    static QByteArray routesToJson(const std::vector<MessageCenterEngine::Route>& routes);
    static std::vector<MessageCenterEngine::Route> routesFromJson(const QByteArray& json);
    static QByteArray filesToJson(const QList<MessageCenterFileSetting>& files);
    static QList<MessageCenterFileSetting> filesFromJson(const QByteArray& json);
};

#endif // INCLUDE_MESSAGECENTERSETTINGS_H
