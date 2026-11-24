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

#include "biquad.h"

using modemcellular::BiquadCascade;
using modemcellular::BiquadNotch;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kPi = 3.14159265358979323846;

// the gain of the filter for a sine, in dB, after it settled
static double gainDb(BiquadCascade filter, double hz, double rate)
{
    double in = 0.0, out = 0.0;
    const int n = static_cast<int>(rate);

    for (int i = 0; i < 2 * n; i++)
    {
        const double x = std::sin(2.0 * kPi * hz * i / rate);
        const double y = filter.process(x);
        if (i >= n)
        {
            in += x * x;
            out += y * y;
        }
    }
    return 10.0 * std::log10(out / in);
}

// The notch of the SAT on the AMPS voice channel (osmocom: 6000 Hz, Q 10, 2 sections at the
// channel rate): deep at the three SATs, the speech band untouched
static void testNotch()
{
    const double rate = 48000.0;
    const BiquadNotch notch(6000.0, rate, 2, 10.0);

    CHECK(gainDb(notch, 6000.0, rate) < -60.0);
    CHECK(gainDb(notch, 5970.0, rate) < -25.0 && gainDb(notch, 6030.0, rate) < -25.0);
    CHECK(std::fabs(gainDb(notch, 1000.0, rate)) < 0.1);
    CHECK(gainDb(notch, 3000.0, rate) > -0.5);
}

int main()
{
    testNotch();

    if (g_failures == 0) {
        std::printf("all biquad tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
