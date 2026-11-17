///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB                                   //
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
#include "cellulartxgui.h"
#endif
#include "cellulartx.h"
#include "cellulartxplugin.h"

const PluginDescriptor CellularTxPlugin::m_pluginDescriptor = {
    CellularTx::m_channelId,
    QStringLiteral("Cellular Tx"),
    QStringLiteral("8.0.0"),
    QStringLiteral("(c) Vasyl Samoilov"),
    QStringLiteral("https://github.com/f4exb/sdrangel"),
    true,
    QStringLiteral("https://github.com/f4exb/sdrangel")
};

CellularTxPlugin::CellularTxPlugin(QObject* parent) :
    QObject(parent),
    m_pluginAPI(nullptr)
{
}

const PluginDescriptor& CellularTxPlugin::getPluginDescriptor() const
{
    return m_pluginDescriptor;
}

void CellularTxPlugin::initPlugin(PluginAPI* pluginAPI)
{
    m_pluginAPI = pluginAPI;
    m_pluginAPI->registerTxChannel(CellularTx::m_channelIdURI, CellularTx::m_channelId, this);
}

void CellularTxPlugin::createTxChannel(DeviceAPI *deviceAPI, BasebandSampleSource **bs, ChannelAPI **cs) const
{
    if (bs || cs)
    {
        CellularTx *instance = new CellularTx(deviceAPI);

        if (bs) {
            *bs = instance;
        }
        if (cs) {
            *cs = instance;
        }
    }
}

#ifdef SERVER_MODE
ChannelGUI* CellularTxPlugin::createTxChannelGUI(DeviceUISet *deviceUISet, BasebandSampleSource *txChannel) const
{
    (void) deviceUISet;
    (void) txChannel;
    return nullptr;
}
#else
ChannelGUI* CellularTxPlugin::createTxChannelGUI(DeviceUISet *deviceUISet, BasebandSampleSource *txChannel) const
{
    return CellularTxGUI::create(m_pluginAPI, deviceUISet, txChannel);
}
#endif
