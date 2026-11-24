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

#include <cmath>
#include <cstdio>

#include "ampsplan.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static bool near(double a, double b)
{
    return std::fabs(a - b) < 1.0;
}

// The frequencies osmocom printed for the user's network (amps.log of 2026-11-24)
static void testFrequencies()
{
    AmpsChannelFrequency f;

    CHECK(ampsChannelFrequency(353, f) && near(f.downlink, 880.590e6) && near(f.uplink, 835.590e6));
    CHECK(ampsChannelFrequency(356, f) && near(f.downlink, 880.680e6));
    CHECK(ampsChannelFrequency(362, f) && near(f.downlink, 880.860e6) && near(f.uplink, 835.860e6));
    CHECK(ampsChannelFrequency(1, f) && near(f.downlink, 870.030e6));
    CHECK(ampsChannelFrequency(799, f) && near(f.downlink, 893.970e6));
    // 991..1023 below channel 1
    CHECK(ampsChannelFrequency(1023, f) && near(f.downlink, 870.000e6));
    CHECK(ampsChannelFrequency(991, f) && near(f.downlink, 869.040e6));
    CHECK(!ampsChannelFrequency(0, f) && !ampsChannelFrequency(800, f) && !ampsChannelFrequency(990, f));
    CHECK(!ampsChannelFrequency(1024, f));
}

static void testBands()
{
    CHECK(ampsBand(1) == "A" && ampsBand(333) == "A" && ampsBand(334) == "B" && ampsBand(666) == "B");
    CHECK(ampsBand(667) == "A'" && ampsBand(716) == "A'" && ampsBand(717) == "B'" && ampsBand(799) == "B'");
    CHECK(ampsBand(991) == "A''" && ampsBand(800).empty());
    CHECK(ampsControlChannel(313) && ampsControlChannel(354) && !ampsControlChannel(312) && !ampsControlChannel(355));
}

// As osmocom's amps_create: an invalid number and a control channel on a voice channel number are
// errors, a voice channel on a control channel number a notice
static void testChecks()
{
    CHECK(ampsCheckCarrier(353, AmpsChannelType::ControlPaging).empty());
    CHECK(ampsCheckCarrier(356, AmpsChannelType::Voice).empty());
    CHECK(ampsCheckCarrier(353, AmpsChannelType::ControlPagingVoice).empty());

    auto i = ampsCheckCarrier(900, AmpsChannelType::Voice);
    CHECK(i.size() == 1 && i[0].error);
    i = ampsCheckCarrier(400, AmpsChannelType::ControlPaging);
    CHECK(i.size() == 1 && i[0].error);
    i = ampsCheckCarrier(340, AmpsChannelType::Voice);
    CHECK(i.size() == 1 && !i[0].error);
}

int main()
{
    testFrequencies();
    testBands();
    testChecks();

    if (g_failures == 0) {
        std::printf("all plan tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
