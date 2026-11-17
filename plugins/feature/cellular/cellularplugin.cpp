///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov                                             //
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

#include <QtPlugin>

#include "plugin/pluginapi.h"

#ifndef SERVER_MODE
#include "cellulargui.h"
#endif

#include "cellular.h"
#include "cellularplugin.h"

const PluginDescriptor CellularPlugin::m_pluginDescriptor = {
    Cellular::m_featureId,
    QStringLiteral("Cellular"),
    QStringLiteral("8.0.0"),
    QStringLiteral("(c) Vasyl Samoilov"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

CellularPlugin::CellularPlugin(QObject* parent) :
    QObject(parent),
    m_pluginAPI(nullptr)
{
}

const PluginDescriptor& CellularPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void CellularPlugin::initPlugin(PluginAPI* pluginAPI)
{
    m_pluginAPI = pluginAPI;
    m_pluginAPI->registerFeature(Cellular::m_featureIdURI, Cellular::m_featureId, this);
}

#ifdef SERVER_MODE
FeatureGUI* CellularPlugin::createFeatureGUI(FeatureUISet *featureUISet, Feature *feature) const
{
    (void) featureUISet;
    (void) feature;
    return nullptr;
}
#else
FeatureGUI* CellularPlugin::createFeatureGUI(FeatureUISet *featureUISet, Feature *feature) const
{
    return CellularGUI::create(m_pluginAPI, featureUISet, feature);
}
#endif

Feature* CellularPlugin::createFeature(WebAPIAdapterInterface* webAPIAdapterInterface) const
{
    return new Cellular(webAPIAdapterInterface);
}
