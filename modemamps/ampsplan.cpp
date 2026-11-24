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

#include "ampsplan.h"

namespace modemamps
{

const char *ampsTypeName(AmpsChannelType type)
{
    switch (type)
    {
    case AmpsChannelType::ControlPaging: return "CC/PC";
    case AmpsChannelType::ControlPagingVoice: return "CC/PC/VC";
    case AmpsChannelType::Voice: return "VC";
    }
    return "";
}

bool ampsChannelFrequency(int channel, AmpsChannelFrequency& out)
{
    // 832 channels, 990 is not used (TIA/EIA-136-110)
    if (channel < 1 || channel > 1023 || (channel > 799 && channel < 991)) {
        return false;
    }
    const int n = (channel >= 990) ? channel - 1023 : channel;

    out.downlink = 870.030e6 + (n - 1) * kAmpsChannelSpacing;
    out.uplink = out.downlink - kAmpsDuplex;
    return true;
}

bool ampsControlChannel(int channel)
{
    return channel >= 313 && channel <= 354;
}

std::string ampsBand(int channel)
{
    if (channel >= 991 && channel <= 1023) {
        return "A''";
    }
    if (channel >= 1 && channel <= 333) {
        return "A";
    }
    if (channel >= 334 && channel <= 666) {
        return "B";
    }
    if (channel >= 667 && channel <= 716) {
        return "A'";
    }
    if (channel >= 717 && channel <= 799) {
        return "B'";
    }
    return "";
}

std::vector<AmpsPlanIssue> ampsCheckCarrier(int channel, AmpsChannelType type)
{
    std::vector<AmpsPlanIssue> issues;
    AmpsChannelFrequency f;

    if (!ampsChannelFrequency(channel, f))
    {
        issues.push_back({true, "Channel number " + std::to_string(channel) + " invalid"});
        return issues;
    }
    if (ampsControlChannel(channel) && type == AmpsChannelType::Voice)
    {
        issues.push_back({false, "Channel number " + std::to_string(channel)
            + " belongs to a control channel, but the channel type VC requires it to be on a voice channel number."
            " Some phones may reject this (osmocom: all its phones do not)"});
    }
    if (!ampsControlChannel(channel) && type != AmpsChannelType::Voice)
    {
        issues.push_back({true, "Channel number " + std::to_string(channel) + " belongs to a voice channel, but the channel type "
            + ampsTypeName(type) + " requires it to be on a control channel number (313 to 354)"});
    }
    return issues;
}

} // namespace modemamps
