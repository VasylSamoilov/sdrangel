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

#ifndef MODEMAMPS_AMPSPLAN_H_
#define MODEMAMPS_AMPSPLAN_H_

#include <string>
#include <vector>

#include "export.h"

// The channels of AMPS (TIA/EIA-553-A 2.1.1, 3.1.1; TIA/EIA-136-110 for 991..1023): the base station
// transmits on 870.030 MHz + (N - 1) x 30 kHz, the phone 45 MHz lower. Channels 313 to 354 are the
// dedicated control channels (system A 313..333, system B 334..354). As osmocom-analog's amps
// (amps.c amps_channel2freq, amps_channel2type, amps_channel2band, the checks of amps_create).
namespace modemamps
{

enum class AmpsChannelType
{
    ControlPaging,          // CC/PC: control and paging channel
    ControlPagingVoice,     // CC/PC/VC: also a voice channel
    Voice                   // VC
};

MODEMAMPS_API const char *ampsTypeName(AmpsChannelType type);

struct AmpsChannelFrequency
{
    double downlink = 0.0;      // base station transmits
    double uplink = 0.0;        // phone transmits
};

constexpr double kAmpsChannelSpacing = 30000.0;
constexpr double kAmpsDuplex = 45.0e6;

// false for a number AMPS has not (0, 800..990, above 1023)
MODEMAMPS_API bool ampsChannelFrequency(int channel, AmpsChannelFrequency& out);
// one of the dedicated control channels 313..354
MODEMAMPS_API bool ampsControlChannel(int channel);
// the band of the channel: A, B, A', B', A'' (empty for no channel)
MODEMAMPS_API std::string ampsBand(int channel);

struct AmpsPlanIssue
{
    bool error;
    std::string text;
};

// The checks of a carrier as osmocom makes them when a channel is created.
MODEMAMPS_API std::vector<AmpsPlanIssue> ampsCheckCarrier(int channel, AmpsChannelType type);

} // namespace modemamps

#endif // MODEMAMPS_AMPSPLAN_H_
