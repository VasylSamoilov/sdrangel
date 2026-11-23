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

    // with a ceiling above 1.0 the expander restores up to it (NMT: the largest deviation, 4.7
    // over 3.0 kHz): 1.5 compressed and expanded comes back as 1.5, not 1.0
    const double ceiling = 4700.0 / 3000.0;
    Compandor tx2;
    Compandor rx2;
    rx2.setExpanderMax(ceiling);
    std::vector<float> loud = sine(1.5, 8000);
    tx2.compress(loud.data(), static_cast<int>(loud.size()));
    rx2.expand(loud.data(), static_cast<int>(loud.size()));
    CHECK(std::fabs(peakOfTail(loud) - 1.5) < 0.1);
    // and no more than the ceiling: 2.0 in stays at 2.0 * ceiling at most
    Compandor rx3;
    rx3.setExpanderMax(ceiling);
    std::vector<float> over = sine(2.0, 8000);
    rx3.expand(over.data(), static_cast<int>(over.size()));
    CHECK(peakOfTail(over) <= 2.0 * ceiling + 1e-6);
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

// Lookahead (osmocom-analog's NMT, 4 ms at 8 kHz): the speech is delayed by the lookahead, and the
// onset of a loud syllable after a quiet pause no longer goes out with the gain of the pause.
static void testLookahead()
{
    // the delay: a click comes out 32 samples later
    {
        Compandor compandor;
        compandor.setCompressorLookahead(4.0);
        std::vector<float> s(100, 0.0f);
        s[10] = 1.0f;
        compandor.compress(s.data(), static_cast<int>(s.size()));
        for (size_t i = 0; i < s.size(); i++) {
            CHECK((i == 42) == (s[i] != 0.0f));
        }
    }

    // the first 4 ms of the onset after 0.5 s of quiet: with the lookahead far lower
    auto onsetPeak = [](double lookaheadMs) {
        Compandor compandor;
        compandor.setCompressorLookahead(lookaheadMs);
        std::vector<float> quiet = sine(0.01, 4000);
        std::vector<float> loud = sine(1.0, 800);
        compandor.compress(quiet.data(), static_cast<int>(quiet.size()));
        compandor.compress(loud.data(), static_cast<int>(loud.size()));
        const size_t delay = static_cast<size_t>(lookaheadMs * 8.0 + 0.5);
        double peak = 0.0;
        for (size_t i = delay; i < delay + 32; i++) {
            peak = std::fmax(peak, std::fabs(loud[i]));
        }
        return peak;
    };
    const double without = onsetPeak(0.0);
    const double with = onsetPeak(4.0);
    CHECK(without > 2.0);                   // the gain of the pause: several dB over the level
    CHECK(with < 0.6 * without);
    // reset empties the lookahead
    Compandor compandor;
    compandor.setCompressorLookahead(4.0);
    std::vector<float> s = sine(1.0, 100);
    compandor.compress(s.data(), static_cast<int>(s.size()));
    compandor.reset();
    std::vector<float> z(32, 0.0f);
    compandor.compress(z.data(), static_cast<int>(z.size()));
    for (float v : z) {
        CHECK(v == 0.0f);
    }
}

static std::vector<float> tone(double hz, double amplitude, int count)
{
    std::vector<float> s(static_cast<size_t>(count));

    for (int i = 0; i < count; i++) {
        s[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * i / 8000.0));
    }
    return s;
}

// Knee (osmocom-analog's NMT: -2 dB re the reference, emphasis 300 Hz, lookahead 4 ms): loud
// speech, after the pre-emphasis, is held at the knee; below the knee nothing changes.
static void testKnee()
{
    const double knee = std::pow(10.0, -2.0 / 20.0);
    const double a = std::exp(-2.0 * kPi * 300.0 / 8000.0);
    const double norm = 1.0 / std::sqrt(1.0 + a * a - 2.0 * a * std::cos(2.0 * kPi * 1000.0 / 8000.0));
    auto emphasized = [&](const std::vector<float>& s) {
        double last = 0.0, peak = 0.0;
        for (size_t i = 0; i < s.size(); i++)
        {
            const double y = (s[i] - a * last) * norm;
            last = s[i];
            if (i >= s.size() / 2) {
                peak = std::fmax(peak, std::fabs(y));
            }
        }
        return peak;
    };

    // loud 2.5 kHz: without the knee far above it after the pre-emphasis, with it held there
    for (bool on : {false, true})
    {
        Compandor compandor;
        compandor.setCompressorLookahead(4.0);
        if (on) {
            compandor.setCompressorKnee(knee, 300.0);
        }
        std::vector<float> s = tone(2500.0, 2.0, 8000);
        compandor.compress(s.data(), static_cast<int>(s.size()));
        if (on) {
            CHECK(std::fabs(emphasized(s) / knee - 1.0) < 0.1);
        } else {
            CHECK(emphasized(s) > 2.0 * knee);
        }
    }

    // quiet speech: the same with and without the knee
    Compandor plain;
    Compandor kneed;
    plain.setCompressorLookahead(4.0);
    kneed.setCompressorLookahead(4.0);
    kneed.setCompressorKnee(knee, 300.0);
    std::vector<float> p = tone(1000.0, 0.1, 8000);
    std::vector<float> k = p;
    plain.compress(p.data(), static_cast<int>(p.size()));
    kneed.compress(k.data(), static_cast<int>(k.size()));
    for (size_t i = 0; i < p.size(); i++) {
        CHECK(p[i] == k[i]);
    }
}

int main()
{
    testCompress();
    testRoundTrip();
    testTiming();
    testLookahead();
    testKnee();

    if (g_failures == 0) {
        std::printf("all compandor tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
