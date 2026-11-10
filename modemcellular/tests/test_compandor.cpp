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
#include <cstdio>
#include <vector>

#include "compandor.h"

using modemcellular::Compandor;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kPi = 3.14159265358979323846;

static std::vector<float> sine(double amplitude, int count)
{
    std::vector<float> s(static_cast<size_t>(count));

    for (int i = 0; i < count; i++) {
        s[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * kPi * 1000.0 * i / 8000.0));
    }
    return s;
}

static double peakOfTail(const std::vector<float>& s)
{
    double peak = 0.0;

    for (size_t i = s.size() / 2; i < s.size(); i++) {
        peak = std::fmax(peak, std::fabs(s[i]));
    }
    return peak;
}

// 2:1: a level of 1.0 passes, 0.1 (-20 dB) comes out near 0.32 (-10 dB), 4.0 near 2.0.
static void testCompress()
{
    struct Case { double in; double out; };
    const Case cases[] = {{1.0, 1.0}, {0.1, std::sqrt(0.1)}, {4.0, 2.0}};

    for (const Case& c : cases)
    {
        Compandor compandor;
        std::vector<float> s = sine(c.in, 8000);

        compandor.compress(s.data(), static_cast<int>(s.size()));
        CHECK(std::fabs(peakOfTail(s) - c.out) < 0.05 * c.out + 0.01);
    }

    // very quiet sound is lifted by 15 dB at most (envelope held at -30 dB)
    Compandor compandor;
    std::vector<float> s = sine(0.001, 8000);
    compandor.compress(s.data(), static_cast<int>(s.size()));
    CHECK(peakOfTail(s) < 0.001 * 5.7 && peakOfTail(s) > 0.001 * 5.4);
}

// The expander restores what the compressor did for levels up to 1.0, and does not lift.
static void testRoundTrip()
{
    for (double level : {1.0, 0.3, 0.1})
    {
        Compandor tx;
        Compandor rx;
        std::vector<float> s = sine(level, 8000);

        tx.compress(s.data(), static_cast<int>(s.size()));
        rx.expand(s.data(), static_cast<int>(s.size()));
        CHECK(std::fabs(peakOfTail(s) - level) < 0.1 * level);
    }

    Compandor rx;
    std::vector<float> s = sine(2.0, 8000);
    rx.expand(s.data(), static_cast<int>(s.size()));
    CHECK(peakOfTail(s) <= 2.0 + 1e-6);
}

// Attack and recovery: after a step from 0.01 to 1.0 the gain follows within tens of ms.
static void testTiming()
{
    Compandor compandor;
    std::vector<float> quiet = sine(0.01, 4000);
    std::vector<float> loud = sine(1.0, 800);

    compandor.compress(quiet.data(), static_cast<int>(quiet.size()));
    compandor.compress(loud.data(), static_cast<int>(loud.size()));
    // 100 ms after the step the loud part passes almost unchanged
    double peak = 0.0;
    for (size_t i = 400; i < loud.size(); i++) {
        peak = std::fmax(peak, std::fabs(loud[i]));
    }
    CHECK(peak < 1.15 && peak > 0.9);

    // a sample that is not finite is taken as silence and does not poison the state
    float bad[3] = {NAN, INFINITY, 0.5f};
    compandor.compress(bad, 3);
    CHECK(bad[0] == 0.0f && bad[1] == 0.0f && std::isfinite(bad[2]));
}

int main()
{
    testCompress();
    testRoundTrip();
    testTiming();

    if (g_failures == 0) {
        std::printf("all compandor tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
