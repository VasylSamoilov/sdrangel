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

#include <cmath>

#include "phone.h"
#include "nmtplan.h"

static bool near(double a, double mhz)
{
    return std::fabs(a - mhz * 1e6) < 1.0;
}

static bool frequency(System system, const char* country, int channel, double downlink, double uplink, double factor)
{
    const NmtCountry* c = nmtCountry(system, country);
    NmtChannelFrequency f;

    return c && nmtChannelFrequency(*c, channel, f) && near(f.downlink, downlink) && near(f.uplink, uplink)
        && std::fabs(f.deviationFactor - factor) < 1e-9;
}

static bool exists(System system, const char* country, int channel)
{
    const NmtCountry* c = nmtCountry(system, country);
    NmtChannelFrequency f;

    return c && nmtChannelFrequency(*c, channel, f);
}

static void testFrequencies()
{
    // Scandinavian NMT-450: the plain, extended and interleaved channels
    CHECK(frequency(System::Nmt450, "DK", 1, 463.000, 453.000, 1.0));
    CHECK(frequency(System::Nmt450, "DK", 180, 467.475, 457.475, 1.0));
    CHECK(frequency(System::Nmt450, "DK", 181, 462.500, 452.500, 1.0));
    CHECK(frequency(System::Nmt450, "DK", 200, 462.975, 452.975, 1.0));
    CHECK(frequency(System::Nmt450, "DK", 201, 463.0125, 453.0125, 1.0));
    CHECK(frequency(System::Nmt450, "DK", 381, 462.5125, 452.5125, 1.0));
    CHECK(frequency(System::Nmt450, "DK", 399, 462.9625, 452.9625, 1.0));
    CHECK(frequency(System::Nmt450, "DK", 400, 462.9875, 452.9875, 1.0));
    CHECK(!exists(System::Nmt450, "DK", 0));
    CHECK(!exists(System::Nmt450, "DK", 401));

    // NMT-900
    CHECK(frequency(System::Nmt900, "DK", 1, 935.0125, 890.0125, 1.0));
    CHECK(frequency(System::Nmt900, "DK", 1025, 935.025, 890.025, 1.0));
    CHECK(!exists(System::Nmt900, "DK", 1001));

    // Hungary: two blocks, falling with the channel number, 20 kHz spacing
    CHECK(frequency(System::Nmt450, "HU", 132, 467.370, 457.370, 0.8));
    CHECK(frequency(System::Nmt450, "HU", 239, 465.230, 455.230, 0.8));
    CHECK(frequency(System::Nmt450, "HU", 1, 469.990, 459.990, 0.8));
    CHECK(frequency(System::Nmt450, "HU", 72, 468.570, 458.570, 0.8));
    CHECK(!exists(System::Nmt450, "HU", 73));
    CHECK(!exists(System::Nmt450, "HU", 131));

    // France NMT-900 sits at 440 MHz with the phone above the base station
    CHECK(frequency(System::Nmt900, "F1", 400, 440.025, 450.025, 1.0));

    // the name does not care for case, the system does
    CHECK(nmtCountry(System::Nmt450, "dk") == nmtCountry(System::Nmt450, "DK"));
    CHECK(nmtCountry(System::Nmt450, "D") == nullptr);
    CHECK(nmtCountry(System::Nmt450, "DKX") == nullptr);
    CHECK(nmtCountry(System::Nmt450, "F1") == nullptr);
    CHECK(nmtCountry(System::Nmt900, "CH") != nullptr);

    for (const NmtCountry& c : nmtCountries())
    {
        CHECK(!c.blocks.empty());
        CHECK(c.firstArea >= 1 && c.firstArea <= c.lastArea && c.lastArea <= 15);
        CHECK(c.y >= 0 && c.y <= 9);
    }
}

static void testTrafficAreas()
{
    CHECK(nmtTrafficArea(System::Nmt450, "DK,1") == 0x51);
    CHECK(nmtTrafficArea(System::Nmt450, "DK") == 0x51);
    CHECK(nmtTrafficArea(System::Nmt450, "dk,9") == 0x59);
    CHECK(nmtTrafficArea(System::Nmt450, "DK,10") == -1);
    CHECK(nmtTrafficArea(System::Nmt450, "DK,0") == -1);
    CHECK(nmtTrafficArea(System::Nmt450, "DK,") == -1);
    CHECK(nmtTrafficArea(System::Nmt450, "DK,1x") == -1);
    CHECK(nmtTrafficArea(System::Nmt450, "XX,1") == -1);
    CHECK(nmtTrafficArea(System::Nmt450, "L") == 0x1f);
    CHECK(nmtTrafficArea(System::Nmt450, "HU,15") == 0x6f);
    CHECK(nmtTrafficArea(System::Nmt450, "A2,3") == 0x93);
    CHECK(nmtTrafficArea(System::Nmt450, "NL,15") == -1);
    CHECK(nmtTrafficArea(System::Nmt900, "NL,15") == 0x6f);
    CHECK(nmtTrafficArea(System::Nmt900, "DK,2") == 0x12);
    CHECK(nmtTrafficArea(System::Nmt900, "F0,1") == 0x01);
}

static bool hasIssue(const std::vector<NmtPlanIssue>& issues, bool error)
{
    for (const NmtPlanIssue& i : issues)
    {
        if (i.error == error) {
            return true;
        }
    }
    return false;
}

static void testChecks()
{
    const NmtCountry& dk = *nmtCountry(System::Nmt450, "DK");

    CHECK(nmtCheckCarrier(dk, 1, false).empty());
    CHECK(nmtCheckCarrier(dk, 180, false).empty());
    CHECK(hasIssue(nmtCheckCarrier(dk, 181, false), false));
    CHECK(!hasIssue(nmtCheckCarrier(dk, 181, false), true));
    CHECK(hasIssue(nmtCheckCarrier(dk, 201, false), false));
    CHECK(hasIssue(nmtCheckCarrier(dk, 1, true), false));

    CHECK(hasIssue(nmtCheckCarrier(dk, 400, false), false));
    CHECK(!hasIssue(nmtCheckCarrier(dk, 400, false), true));

    const std::vector<NmtPlanIssue> none = nmtCheckCarrier(dk, 401, false);
    CHECK(none.size() == 1 && none[0].error);

    // a plan that was never tried with a phone is a warning
    const NmtCountry& es = *nmtCountry(System::Nmt450, "E");
    CHECK(hasIssue(nmtCheckCarrier(es, 1, false), false));
    CHECK(!hasIssue(nmtCheckCarrier(es, 1, false), true));

    // the extended and interleaved warnings are of the Scandinavian plan only
    const NmtCountry& nl = *nmtCountry(System::Nmt450, "NL");
    CHECK(nmtCheckCarrier(nl, 201, false).empty());
}

// the group of the block a channel is in
static bool group(System system, const char* country, int channel, NmtChannelGroup expected)
{
    const NmtCountry* c = nmtCountry(system, country);

    if (!c) {
        return false;
    }
    for (const NmtFrequencyBlock& b : c->blocks)
    {
        if (channel >= b.firstChannel && channel <= b.lastChannel) {
            return b.group == expected;
        }
    }
    return false;
}

static void testGroups()
{
    using G = NmtChannelGroup;

    CHECK(group(System::Nmt450, "DK", 1, G::Ordinary));
    CHECK(group(System::Nmt450, "DK", 180, G::Ordinary));
    CHECK(group(System::Nmt450, "DK", 181, G::Extension));
    CHECK(group(System::Nmt450, "DK", 200, G::Extension));
    CHECK(group(System::Nmt450, "DK", 201, G::Interleaved));
    CHECK(group(System::Nmt450, "DK", 380, G::Interleaved));
    CHECK(group(System::Nmt450, "DK", 381, G::InterleavedExtension));
    CHECK(group(System::Nmt450, "DK", 400, G::InterleavedExtension));
    CHECK(group(System::Nmt450, "HU", 1, G::Ordinary));
    CHECK(group(System::Nmt450, "HU", 239, G::Ordinary));
    CHECK(group(System::Nmt900, "DK", 1000, G::Ordinary));
    CHECK(group(System::Nmt900, "DK", 1025, G::Interleaved));
    CHECK(group(System::Nmt900, "F1", 578, G::Ordinary));
    CHECK(group(System::Nmt900, "F1", 1423, G::Interleaved));

    // NMT-900: interleaved are the numbers from 1024 up, and only those
    for (const NmtCountry& c : nmtCountries())
    {
        for (const NmtFrequencyBlock& b : c.blocks)
        {
            if (c.system == System::Nmt900) {
                CHECK((b.group == G::Interleaved) == (b.firstChannel >= 1024));
            }
        }
    }

    CHECK(std::string(nmtGroupName(G::Ordinary)) == "Ordinary");
    CHECK(std::string(nmtGroupName(G::Extension)) == "Extension");
    CHECK(std::string(nmtGroupName(G::Interleaved)) == "Interleaved");
    CHECK(std::string(nmtGroupName(G::InterleavedExtension)) == "Interleaved extension");
}

int main()
{
    testFrequencies();
    testTrafficAreas();
    testChecks();
    testGroups();

    if (g_failures == 0) {
        std::printf("all plan tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
