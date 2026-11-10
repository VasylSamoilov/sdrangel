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

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "util/simpleserializer.h"
#include "settings/serializable.h"
#include "messagecentersettings.h"

MessageCenterSettings::MessageCenterSettings() :
    m_rollupState(nullptr)
{
    resetToDefaults();
}

void MessageCenterSettings::resetToDefaults()
{
    m_title = "Message Center";
    m_rgbColor = QColor(0, 170, 127).rgb();
    m_logSize = MessageCenterEngine::m_defaultLogSize;
    m_claimTimeoutSec = 10;
    m_silentSec = 20;
    m_routes.clear();
    m_sinks.clear();
    m_sources.clear();
    m_workspaceIndex = 0;
    m_geometryBytes.clear();
    m_hidden = false;
}

QByteArray MessageCenterSettings::routesToJson(const std::vector<MessageCenterEngine::Route>& routes)
{
    QJsonArray array;

    for (const MessageCenterEngine::Route& route : routes)
    {
        QJsonObject o;
        o["name"] = route.m_name;
        o["enabled"] = route.m_enabled;
        o["stop"] = route.m_stop;
        o["sinks"] = QJsonArray::fromStringList(route.m_sinks);
        o["filter"] = route.m_filter.toJson();
        array.append(o);
    }

    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

std::vector<MessageCenterEngine::Route> MessageCenterSettings::routesFromJson(const QByteArray& json)
{
    std::vector<MessageCenterEngine::Route> routes;

    for (const QJsonValue& v : QJsonDocument::fromJson(json).array())
    {
        QJsonObject o = v.toObject();
        MessageCenterEngine::Route route;
        route.m_name = o["name"].toString();
        route.m_enabled = o["enabled"].toBool(true);
        route.m_stop = o["stop"].toBool(false);

        for (const QJsonValue& s : o["sinks"].toArray()) {
            route.m_sinks.append(s.toString());
        }

        route.m_filter = MessageCenterFilter::fromJson(o["filter"].toObject());
        routes.push_back(route);
    }

    return routes;
}

QByteArray MessageCenterSettings::filesToJson(const QList<MessageCenterFileSetting>& files)
{
    QJsonArray array;

    for (const MessageCenterFileSetting& file : files)
    {
        QJsonObject o;
        o["name"] = file.m_name;
        o["path"] = file.m_path;
        o["format"] = file.m_format;
        array.append(o);
    }

    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QList<MessageCenterFileSetting> MessageCenterSettings::filesFromJson(const QByteArray& json)
{
    QList<MessageCenterFileSetting> files;

    for (const QJsonValue& v : QJsonDocument::fromJson(json).array())
    {
        QJsonObject o = v.toObject();
        MessageCenterFileSetting file;
        file.m_name = o["name"].toString();
        file.m_path = o["path"].toString();
        file.m_format = o["format"].toInt();
        files.append(file);
    }

    return files;
}

QByteArray MessageCenterSettings::serialize() const
{
    SimpleSerializer s(1);

    s.writeString(1, m_title);
    s.writeU32(2, m_rgbColor);
    s.writeS32(3, m_logSize);
    s.writeS32(4, m_claimTimeoutSec);
    s.writeS32(5, m_silentSec);
    s.writeBlob(6, routesToJson(m_routes));
    s.writeBlob(7, filesToJson(m_sinks));
    s.writeBlob(8, filesToJson(m_sources));

    if (m_rollupState) {
        s.writeBlob(9, m_rollupState->serialize());
    }

    s.writeS32(10, m_workspaceIndex);
    s.writeBlob(11, m_geometryBytes);
    s.writeBool(12, m_hidden);

    return s.final();
}

bool MessageCenterSettings::deserialize(const QByteArray& data)
{
    SimpleDeserializer d(data);

    if (!d.isValid())
    {
        resetToDefaults();
        return false;
    }

    if (d.getVersion() == 1)
    {
        QByteArray bytes;

        d.readString(1, &m_title, "Message Center");
        d.readU32(2, &m_rgbColor, QColor(0, 170, 127).rgb());
        d.readS32(3, &m_logSize, MessageCenterEngine::m_defaultLogSize);
        d.readS32(4, &m_claimTimeoutSec, 10);
        d.readS32(5, &m_silentSec, 20);
        m_logSize = std::max(100, std::min(1000000, m_logSize));
        m_claimTimeoutSec = std::max(1, std::min(3600, m_claimTimeoutSec));
        m_silentSec = std::max(6, std::min(3600, m_silentSec));
        d.readBlob(6, &bytes);
        m_routes = routesFromJson(bytes);
        d.readBlob(7, &bytes);
        m_sinks = filesFromJson(bytes);
        d.readBlob(8, &bytes);
        m_sources = filesFromJson(bytes);

        if (m_rollupState)
        {
            d.readBlob(9, &bytes);
            m_rollupState->deserialize(bytes);
        }

        d.readS32(10, &m_workspaceIndex, 0);
        d.readBlob(11, &m_geometryBytes);
        d.readBool(12, &m_hidden, false);
        return true;
    }

    resetToDefaults();
    return false;
}

void MessageCenterSettings::applySettings(const QStringList& settingsKeys, const MessageCenterSettings& settings)
{
    if (settingsKeys.contains("title")) {
        m_title = settings.m_title;
    }
    if (settingsKeys.contains("rgbColor")) {
        m_rgbColor = settings.m_rgbColor;
    }
    if (settingsKeys.contains("logSize")) {
        m_logSize = settings.m_logSize;
    }
    if (settingsKeys.contains("claimTimeoutSec")) {
        m_claimTimeoutSec = settings.m_claimTimeoutSec;
    }
    if (settingsKeys.contains("silentSec")) {
        m_silentSec = settings.m_silentSec;
    }
    if (settingsKeys.contains("routes")) {
        m_routes = settings.m_routes;
    }
    if (settingsKeys.contains("sinks")) {
        m_sinks = settings.m_sinks;
    }
    if (settingsKeys.contains("sources")) {
        m_sources = settings.m_sources;
    }
    if (settingsKeys.contains("workspaceIndex")) {
        m_workspaceIndex = settings.m_workspaceIndex;
    }
    if (settingsKeys.contains("hidden")) {
        m_hidden = settings.m_hidden;
    }
}
