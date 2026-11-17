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

#include "SWGFeatureSettings.h"
#include "SWGCellularSettings.h"

#include "cellular.h"
#include "cellularwebapiadapter.h"

int CellularWebAPIAdapter::webapiSettingsGet(
    SWGSDRangel::SWGFeatureSettings& response,
    QString& errorMessage)
{
    (void) errorMessage;
    response.setCellularSettings(new SWGSDRangel::SWGCellularSettings());
    response.getCellularSettings()->init();
    Cellular::webapiFormatFeatureSettings(response, m_settings);
    return 200;
}

int CellularWebAPIAdapter::webapiSettingsPutPatch(
    bool force,
    const QStringList& featureSettingsKeys,
    SWGSDRangel::SWGFeatureSettings& response,
    QString& errorMessage)
{
    (void) force;
    (void) errorMessage;
    Cellular::webapiUpdateFeatureSettings(m_settings, featureSettingsKeys, response);
    Cellular::webapiFormatFeatureSettings(response, m_settings);
    return 200;
}
