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
#include <random>
#include <vector>

#include "ampsfsk.h"
#include "ampsvoice.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kRate = 48000.0;
static const int kWindow = 3200;            // 1/15 s

struct Events
{
    std::vector<std::pair<int, int>> sat;   // (window, scc)
    std::vector<std::pair<int, bool>> tone; // (window, on)
    int window = 0;
};

// windows of the SAT of scc at level (of its deviation; scc -1 none), with the ST at toneLevel (of
// the FSK deviation), and noise of 300 Hz rms
static void feed(SatDetector& d, Events& e, int windows, int scc, double level, double toneLevel, std::mt19937& gen)
{
    static double t = 0.0;
    std::normal_distribution<double> noise(0.0, 300.0);
    std::vector<float> block(kWindow);
    SatTone sat(kRate, scc);

    for (int w = 0; w < windows; w++)
    {
        for (int i = 0; i < kWindow; i++)
        {
            block[i] = static_cast<float>(noise(gen) + toneLevel * kFskDeviation * std::sin(2.0 * M_PI * kSignallingToneHz * t));
            t += 1.0 / kRate;
        }
        std::vector<float> tone(kWindow, 0.0f);
        sat.add(tone.data(), kWindow);
        for (int i = 0; i < kWindow; i++) {
            block[i] += static_cast<float>(level) * tone[i];
        }
        d.process(block.data(), kWindow);
        e.window++;
    }
}

static SatDetector detector(Events& e)
{
    SatDetector d(kRate);
    d.setSatSink([&e](int scc, double) { e.sat.push_back({e.window, scc}); });
    d.setToneSink([&e](bool on, double) { e.tone.push_back({e.window, on}); });
    return d;
}

// Detected after 5 windows, a change of the colour code after 5, lost after 5
static void testSat()
{
    std::mt19937 gen(1);
    Events e;
    SatDetector d = detector(e);

    feed(d, e, 10, 1, 1.0, 0.0, gen);
    CHECK(e.sat.size() == 1 && e.sat[0].second == 1 && e.sat[0].first == 4);    // in the 5th window
    CHECK(d.sat() == 1);
    feed(d, e, 10, 2, 1.0, 0.0, gen);
    CHECK(e.sat.size() == 2 && e.sat[1].second == 2 && e.sat[1].first == 14);
    feed(d, e, 10, -1, 0.0, 0.0, gen);
    CHECK(e.sat.size() == 3 && e.sat[2].second == -1 && e.sat[2].first == 24);
    CHECK(d.sat() == -1);
}

// Hysteresis: at 0.4 of the deviation a SAT is not found, but one found is kept
static void testLevels()
{
    std::mt19937 gen(2);
    Events e;
    SatDetector d = detector(e);

    feed(d, e, 10, 0, 0.4, 0.0, gen);
    CHECK(e.sat.empty());
    feed(d, e, 10, 0, 1.0, 0.0, gen);
    CHECK(e.sat.size() == 1 && e.sat[0].second == 0);
    feed(d, e, 10, 0, 0.4, 0.0, gen);
    CHECK(e.sat.size() == 1 && d.sat() == 0);
    feed(d, e, 10, 0, 0.2, 0.0, gen);
    CHECK(e.sat.size() == 2 && e.sat[1].second == -1);
}

// The signalling tone: on after 6 windows, off after 4; at a tenth of the deviation not at all
static void testSignallingTone()
{
    std::mt19937 gen(3);
    Events e;
    SatDetector d = detector(e);

    feed(d, e, 3, 1, 1.0, 0.1, gen);
    feed(d, e, 10, 1, 1.0, 1.0, gen);
    CHECK(e.tone.size() == 1 && e.tone[0].second && e.tone[0].first == 8);
    CHECK(d.tone());
    feed(d, e, 10, 1, 1.0, 0.0, gen);
    CHECK(e.tone.size() == 2 && !e.tone[1].second && e.tone[1].first == 16);
    CHECK(d.sat() == 1);                    // the SAT stays through the tone
}

int main()
{
    testSat();
    testLevels();
    testSignallingTone();

    if (g_failures == 0) {
        std::printf("all voice channel tone tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
