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

#include "nmtplan.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace modemnmt
{

namespace
{

using Blocks = std::vector<NmtFrequencyBlock>;

const Blocks k450Scandinavia = {
    {1, 180, 463.000, 0.025, 1.0, 10.0, true},
    {181, 200, 462.500, 0.025, 1.0, 10.0, true},
    {201, 380, 463.0125, 0.025, 1.0, 10.0, true},
    {381, 399, 462.5125, 0.025, 1.0, 10.0, true}
};
const Blocks k900Scandinavia = {
    {1, 1000, 935.0125, 0.025, 1.0, 45.0, false},
    {1025, 2023, 935.025, 0.025, 1.0, 45.0, false}
};
const Blocks k450NlLB = {{1, 222, 461.310, 0.020, 0.8, 10.0, false}};
const Blocks k900Nl = {{1, 1000, 935.0125, 0.025, 1.0, 45.0, false}};
const Blocks k450Mal = {{1, 180, 462.000, 0.025, 1.0, 10.0, false}};
const Blocks k450TRi = {{1, 224, 489.000, 0.020, 0.8, 10.0, false}};
const Blocks k450E = {{1, 180, 464.325, 0.025, 1.0, 10.0, false}};
const Blocks k450A = {{1, 180, 465.730, -0.020, 0.8, 10.0, false}};
const Blocks k450CzSk = {{1, 222, 465.730, -0.020, 0.8, 10.0, false}};
const Blocks k450Hu = {
    {132, 239, 467.370, -0.020, 0.8, 10.0, false},
    {1, 72, 469.990, -0.020, 0.8, 10.0, false}
};
const Blocks k900F = {
    {400, 578, 440.025, 0.025, 1.0, -10.0, false},
    {1423, 1601, 440.0125, 0.025, 1.0, -10.0, false}
};

const System k450 = System::Nmt450;
const System k900 = System::Nmt900;

const std::vector<NmtCountry> kCountries = {
    {k450, true, 5, 1, 9, "DK", "Denmark", "Tele Danmark Mobile", k450Scandinavia},
    {k450, true, 6, 1, 9, "SE", "Sweden", "Telia Mobitel", k450Scandinavia},
    {k450, true, 7, 1, 9, "NO", "Norway", "Telenor Mobil", k450Scandinavia},
    {k450, true, 8, 1, 9, "FI", "Finland", "Telecom Finland", k450Scandinavia},
    {k450, true, 8, 1, 9, "IS", "Iceland", "Post & Telecom", k450Scandinavia},
    {k450, true, 5, 1, 9, "FO", "Faroe Island", "Faroese Telecom", k450Scandinavia},
    {k450, true, 7, 1, 9, "EE", "Estonia", "Eesti Mobiiltelefon", k450Scandinavia},
    {k450, true, 5, 1, 9, "LV", "Latvia", "Latvian Mobile Telephone", k450Scandinavia},
    {k450, true, 8, 1, 9, "LT", "Lithuania", "COMLIET", k450Scandinavia},
    {k450, true, 6, 1, 9, "BY", "Belarus", "Belcel", k450Scandinavia},
    {k450, true, 5, 1, 9, "MO", "OSS/Moscow", "Moscow Cellular Comm.", k450Scandinavia},
    {k450, true, 6, 1, 9, "STP", "OSS/St Petersburg", "Delta Telecom", k450Scandinavia},
    {k450, true, 6, 1, 9, "STP", "OSS/Leningrads Dist.", "Delta Telecom", k450Scandinavia},
    {k450, true, 7, 1, 9, "CAR", "OSS/Carelian Rep.", "Telecom Finland", k450Scandinavia},
    {k450, true, 5, 1, 9, "MUR", "OSS/Murmansk", "Telecom Finland", k450Scandinavia},
    {k450, true, 5, 1, 9, "LED", "OSS/Leningrads Dist.", "Telecom Finland", k450Scandinavia},
    {k450, true, 5, 1, 9, "KAL", "Kaliningrad", "Telecom Finland", k450Scandinavia},
    {k450, true, 7, 1, 9, "PL", "Poland", "CENTERTEL", k450Scandinavia},
    {k450, true, 6, 1, 9, "BG", "Bulgaria", "MOBIFON", k450Scandinavia},
    {k450, true, 5, 1, 9, "RO", "Romania", "Telefonica Romania", k450Scandinavia},
    {k450, true, 8, 1, 9, "UA", "Ukraine", "Ukrainian Mobile Communications", k450Scandinavia},
    {k450, true, 1, 1, 9, "RU1", "", "", k450Scandinavia},
    {k450, true, 2, 1, 9, "RU2", "", "", k450Scandinavia},
    {k450, true, 3, 1, 9, "RU3", "", "", k450Scandinavia},
    {k450, true, 4, 1, 9, "RU4", "", "", k450Scandinavia},
    {k450, true, 1, 1, 9, "NL", "Netherlands", "Royal Dutch Post & Telecom", k450NlLB},
    {k450, true, 1, 15, 15, "L", "Luxemburg", "Enterprise des P&T Luxembourg", k450NlLB},
    {k450, true, 2, 1, 9, "B", "Belgium", "Belgacom Mobile", k450NlLB},
    {k450, true, 7, 1, 9, "CZ", "Czech Republic", "Eurotel Prague", k450CzSk},
    {k450, true, 6, 1, 9, "SK", "Slovakia", "Eurotel Bratislava", k450CzSk},
    {k450, true, 6, 1, 15, "HU", "Hungary", "WESTEL 0660", k450Hu},
    {k900, true, 1, 1, 9, "DK", "Denmark", "Tele Danmark Mobile", k900Scandinavia},
    {k900, true, 2, 1, 9, "SE", "Sweden", "Telia Mobitel", k900Scandinavia},
    {k900, true, 3, 1, 9, "NO", "Norway", "Telenor Mobil", k900Scandinavia},
    {k900, true, 4, 1, 9, "FI", "Finland", "Telecom Finland", k900Scandinavia},
    {k900, true, 0, 1, 9, "F0", "France (Group 0)", "France Telecom", k900F},
    {k900, true, 1, 1, 9, "F1", "France (Group 1)", "France Telecom", k900F},
    {k900, true, 2, 1, 9, "F2", "France (Group 2)", "France Telecom", k900F},
    {k900, true, 3, 1, 9, "F3", "France (Group 3)", "France Telecom", k900F},
    {k900, true, 4, 1, 9, "F4", "France (Group 4)", "France Telecom", k900F},
    {k900, true, 5, 1, 9, "F5", "France (Group 5)", "France Telecom", k900F},
    {k900, true, 6, 1, 9, "F6", "France (Group 6)", "France Telecom", k900F},
    {k900, true, 7, 1, 9, "F7", "France (Group 7)", "France Telecom", k900F},
    {k450, false, 8, 8, 8, "MAL", "Malaysia", "Jabatan Telekom Malaysia", k450Mal},
    {k450, false, 4, 1, 9, "T", "Thailand", "Telephone Organization of Thailand", k450TRi},
    {k450, false, 8, 1, 9, "E", "Spain", "Telefonica Servicios Moviles", k450E},
    {k450, false, 8, 1, 1, "RI", "Indonesia", "PT Mobisel", k450TRi},
    {k450, false, 0, 1, 3, "A", "Austria", "PTV", k450A},
    {k450, false, 9, 1, 3, "A2", "Austria 2", "PTV", k450A},
    {k900, false, 5, 1, 9, "CH", "Switzerland", "PTT", k900Scandinavia},
    {k900, false, 6, 1, 15, "NL", "Netherlands", "Royal Dutch Post & Telecom", k900Nl},
    // no Turkish allocation known: the standard NMT-900 plan
    {k900, false, 1, 1, 9, "TR", "Turkey", "Turkcell", k900Scandinavia}
};

bool sameName(const std::string& a, const char* b)
{
    size_t i = 0;

    for (; i < a.size() && b[i]; i++)
    {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && !b[i];
}

} // namespace

const std::vector<NmtCountry>& nmtCountries()
{
    return kCountries;
}

const NmtCountry* nmtCountry(System system, const std::string& shortName)
{
    for (const NmtCountry& country : kCountries)
    {
        if (country.system == system && sameName(shortName, country.shortName)) {
            return &country;
        }
    }
    return nullptr;
}

bool nmtChannelFrequency(const NmtCountry& country, int channel, NmtChannelFrequency& out)
{
    for (const NmtFrequencyBlock& block : country.blocks)
    {
        if (channel < block.firstChannel || channel > block.lastChannel) {
            continue;
        }
        const double downlink = block.firstFrequency + (channel - block.firstChannel) * block.spacing;

        out.downlink = downlink * 1e6;
        out.uplink = (downlink - block.duplex) * 1e6;
        out.deviationFactor = block.deviationFactor;
        out.spacing = std::fabs(block.spacing) * 1e6;
        out.scandinavia = block.scandinavia;
        return true;
    }
    return false;
}

int nmtTrafficArea(System system, const std::string& countryAndArea)
{
    const size_t comma = countryAndArea.find(',');
    const NmtCountry* country = nmtCountry(system, countryAndArea.substr(0, comma));

    if (!country) {
        return -1;
    }

    int area = country->firstArea;

    if (comma != std::string::npos)
    {
        const std::string digits = countryAndArea.substr(comma + 1);
        char* end = nullptr;
        const long value = std::strtol(digits.c_str(), &end, 10);

        if (digits.empty() || *end) {
            return -1;
        }
        area = static_cast<int>(value);
    }
    // a country may be listed more than once (the same short name with other areas)
    for (const NmtCountry& c : kCountries)
    {
        if (c.system == system && sameName(country->shortName, c.shortName) && area >= c.firstArea && area <= c.lastArea) {
            return (c.y << 4) | area;
        }
    }
    return -1;
}

std::vector<NmtPlanIssue> nmtCheckCarrier(const NmtCountry& country, int channel, bool combinedWithClock)
{
    std::vector<NmtPlanIssue> issues;
    NmtChannelFrequency f;

    if (!nmtChannelFrequency(country, channel, f))
    {
        issues.push_back({true, "channel " + std::to_string(channel) + " does not exist in the plan of " + country.shortName});
        return issues;
    }
    if (!country.tested) {
        issues.push_back({false, std::string("the plan of ") + country.shortName + " has not been tested with a phone"});
    }
    if (f.scandinavia && channel >= 201) {
        issues.push_back({false, "interleaved channel above 200: probably not supported by the phones"});
    }
    if (f.scandinavia && channel >= 181 && channel <= 200) {
        issues.push_back({false, "extended channel 181..200: never confirmed with a phone"});
    }
    if (combinedWithClock) {
        issues.push_back({false, "the clock is not sent on a combined calling and traffic channel"});
    }
    return issues;
}

} // namespace modemnmt
