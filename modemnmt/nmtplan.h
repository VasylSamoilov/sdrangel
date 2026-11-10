///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Andreas Eversberg <jolly@eversberg.eu>                     //
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

#ifndef MODEMNMT_NMTPLAN_H_
#define MODEMNMT_NMTPLAN_H_

#include <cstdint>
#include <string>
#include <vector>

#include "nmtnumbers.h"
#include "export.h"

namespace modemnmt
{

// The frequency plans of the NMT countries: which channel numbers exist, their frequencies, the
// country digit Y1 of the traffic area and the traffic areas in use.

// One block of channels: first_frequency + (channel - first_channel) * spacing is the frequency
// of the base station (downlink); the phone sends duplex below it.
struct NmtFrequencyBlock
{
    int firstChannel;
    int lastChannel;
    double firstFrequency;      // MHz
    double spacing;             // MHz, negative if the frequency falls with the channel number
    double deviationFactor;     // deviation relative to the Scandinavian plan (20 kHz plans: 0.8)
    double duplex;              // MHz, downlink above uplink
    bool scandinavia;           // the Scandinavian 450 plan (its extended and interleaved channels)
};

struct NmtCountry
{
    System system;
    bool tested;                // tested with a real phone
    int y;                      // country digit Y1 of the traffic area
    int firstArea;              // traffic areas in use (Y2)
    int lastArea;
    const char* shortName;
    const char* longName;
    const char* provider;
    std::vector<NmtFrequencyBlock> blocks;
};

// All countries of both systems, in the order of the list.
MODEMNMT_API const std::vector<NmtCountry>& nmtCountries();
// The country by its short name (case does not matter), null if there is none for the system.
MODEMNMT_API const NmtCountry* nmtCountry(System system, const std::string& shortName);

// A channel of a country: its frequencies in Hz. False if the country has no such channel.
struct NmtChannelFrequency
{
    double downlink = 0.0;      // base station transmits
    double uplink = 0.0;        // phone transmits
    double deviationFactor = 1.0;
    double spacing = 25000.0;   // Hz between the channels of the block
    bool scandinavia = false;
};
MODEMNMT_API bool nmtChannelFrequency(const NmtCountry& country, int channel, NmtChannelFrequency& out);

// The traffic area code Y1Y2 (Y1 in the high nibble) of "DK,1"; -1 if the country does not
// have that area. Also takes "DK" for the first area of the country.
MODEMNMT_API int nmtTrafficArea(System system, const std::string& countryAndArea);

// What the check of a carrier found: errors keep it off the air, warnings are told.
struct NmtPlanIssue
{
    bool error;
    std::string text;
};
// The checks of a carrier as the reference makes them when a channel is created.
MODEMNMT_API std::vector<NmtPlanIssue> nmtCheckCarrier(const NmtCountry& country, int channel, bool combinedWithClock);

} // namespace modemnmt

#endif // MODEMNMT_NMTPLAN_H_
