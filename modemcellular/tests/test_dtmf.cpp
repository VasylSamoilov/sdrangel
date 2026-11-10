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

// Push-button signalling tones.

#include <cmath>
#include <cstdio>
#include <vector>

#include "dtmfgenerator.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// the amplitude of one frequency in a part of the samples (Goertzel)
static double level(const std::vector<float>& x, size_t from, size_t to, double hz)
{
    const double pi = 3.14159265358979323846;
    double re = 0.0, im = 0.0;

    for (size_t i = from; i < to; i++)
    {
        const double phase = 2.0 * pi * hz * static_cast<double>(i) / 8000.0;

        re += x[i] * std::cos(phase);
        im += x[i] * std::sin(phase);
    }

    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(to - from);
}

static void testFrequencies()
{
    double lo, hi;

    CHECK(DtmfGenerator::frequencies('1', lo, hi) && lo == 697.0 && hi == 1209.0);
    CHECK(DtmfGenerator::frequencies('5', lo, hi) && lo == 770.0 && hi == 1336.0);
    CHECK(DtmfGenerator::frequencies('9', lo, hi) && lo == 852.0 && hi == 1477.0);
    CHECK(DtmfGenerator::frequencies('0', lo, hi) && lo == 941.0 && hi == 1336.0);
    CHECK(DtmfGenerator::frequencies('*', lo, hi) && lo == 941.0 && hi == 1209.0);
    CHECK(DtmfGenerator::frequencies('#', lo, hi) && lo == 941.0 && hi == 1477.0);
    CHECK(DtmfGenerator::frequencies('A', lo, hi) && lo == 697.0 && hi == 1633.0);
    CHECK(DtmfGenerator::frequencies('d', lo, hi) && lo == 941.0 && hi == 1633.0);
    CHECK(!DtmfGenerator::frequencies('x', lo, hi));
    CHECK(!DtmfGenerator::frequencies(' ', lo, hi));
}

static void testSequence()
{
    DtmfGenerator g;
    std::vector<float> x(8000, 0.0f);

    CHECK(!g.active());
    CHECK(g.queue('5'));
    CHECK(g.queue('#'));
    CHECK(!g.queue('?'));
    CHECK(g.active());
    g.mix(x.data(), 1000);                          // in pieces
    g.mix(x.data() + 1000, 7000);
    CHECK(!g.active());

    // digit one: 100 ms of 770 and 1336 Hz, 0.2 each, then 50 ms of silence; digit two follows
    CHECK(std::fabs(level(x, 100, 700, 770.0) - 0.2) < 0.02);
    CHECK(std::fabs(level(x, 100, 700, 1336.0) - 0.2) < 0.02);
    CHECK(level(x, 100, 700, 941.0) < 0.02 && level(x, 100, 700, 1209.0) < 0.02);
    CHECK(level(x, 810, 1190, 770.0) < 0.001);
    CHECK(std::fabs(level(x, 1300, 1900, 941.0) - 0.2) < 0.02);
    CHECK(std::fabs(level(x, 1300, 1900, 1477.0) - 0.2) < 0.02);
    CHECK(level(x, 2500, 8000, 941.0) < 0.001);

    // the digits add to what is there
    std::vector<float> y(1600, 0.5f);

    g.queue('1');
    g.mix(y.data(), static_cast<int>(y.size()));
    CHECK(y[0] == 0.5f);                            // starts with a ramp
    CHECK(std::fabs(level(y, 100, 700, 697.0) - 0.2) < 0.02);

    // the queue is short and reset empties it
    for (int i = 0; i < 100; i++) {
        g.queue('1');
    }
    CHECK(g.queue('2') == false);
    g.reset();
    CHECK(!g.active());
}

int main()
{
    testFrequencies();
    testSequence();

    if (g_failures) {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("push-button tone tests passed\n");
    return 0;
}
