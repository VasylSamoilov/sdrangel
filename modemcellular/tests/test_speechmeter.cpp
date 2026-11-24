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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "speechmeter.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kGate = 0.10;       // the noise gate of the NMT receiver, -20 dB
static const double kLimit = 1.567;     // 4.7 kHz of 3 kHz, +3.9 dB

static std::vector<float> tone(double hz, double amplitude, double seconds, double clip = 1e9)
{
    std::vector<float> v(static_cast<size_t>(seconds * SpeechMeter::kSampleRate));

    for (size_t i = 0; i < v.size(); i++)
    {
        const double x = amplitude * std::sin(2.0 * M_PI * hz * static_cast<double>(i) / SpeechMeter::kSampleRate);
        v[i] = static_cast<float>(std::max(-clip, std::min(clip, x)));
    }
    return v;
}

static int loudestBand(const SpeechStats& s)
{
    const std::array<double, SpeechStats::kBands> db = s.bandsDb();

    return static_cast<int>(std::max_element(db.begin(), db.end()) - db.begin());
}

// A tone at the reference: its samples spread below 0 dB (a sine spends most time near its peak),
// nothing beyond, nothing at the limit, no pause, its energy in the 800-1200 Hz band.
static void testReferenceTone()
{
    SpeechMeter m(kGate, kLimit);
    const std::vector<float> t = tone(1000.0, 1.0, 1.0);

    m.process(t.data(), static_cast<int>(t.size()));
    const SpeechStats& s = m.call();
    CHECK(s.speech == 8000 && s.pause == 0);
    CHECK(s.atLimit == 0);
    CHECK(std::fabs(s.peak - 1.0) < 0.01);
    CHECK(s.p99Db() > -2.0 && s.p99Db() <= 1.26);           // the upper edge of the bin of 0 dB
    CHECK(std::fabs(s.levelDb() + 3.01) < 0.1);            // rms of a sine
    // the top bins hold most of the samples: a sine is near its peak most of the time
    const int top = static_cast<int>((0.0 - SpeechStats::kLowDb) / ((SpeechStats::kHighDb - SpeechStats::kLowDb) / SpeechStats::kBins));
    CHECK(s.bins[top - 1] > s.bins[top - 5]);
    CHECK(s.bins[SpeechStats::kBins - 1] == 0);
    CHECK(loudestBand(s) == 2);
    const std::array<double, SpeechStats::kBands> db = s.bandsDb();
    CHECK(db[2] > -1.0);
    CHECK(db[0] < -15.0 && db[5] < -15.0);
    std::printf("1 kHz at 0 dB: level %.2f dB, p99 %.2f dB, bands %.1f %.1f %.1f %.1f %.1f %.1f dB\n",
        s.levelDb(), s.p99Db(), db[0], db[1], db[2], db[3], db[4], db[5]);
}

// A tone at the centre of each band lands in that band.
static void testBands()
{
    for (int band = 0; band < SpeechStats::kBands; band++)
    {
        SpeechMeter m(kGate, kLimit);
        const double f = std::sqrt(SpeechStats::kBandEdges[band] * SpeechStats::kBandEdges[band + 1]);
        const std::vector<float> t = tone(f, 0.5, 1.0);

        m.process(t.data(), static_cast<int>(t.size()));
        CHECK(loudestBand(m.call()) == band);
        CHECK(m.call().bandsDb()[band] > -1.5);
    }
}

// A tone clipped at the limit: a large share of its samples at the limit and in the top bin.
static void testClipped()
{
    SpeechMeter m(kGate, kLimit);
    const std::vector<float> t = tone(700.0, 3.0, 1.0, kLimit);

    m.process(t.data(), static_cast<int>(t.size()));
    const SpeechStats& s = m.call();
    CHECK(s.limitShare() > 0.5);
    CHECK(s.bins[SpeechStats::kBins - 1] >= s.atLimit);        // the top bin reaches down to +1.25 dB
    CHECK(s.p99Db() >= SpeechStats::kHighDb - 0.01);
    std::printf("tone clipped at the limit: %.0f %% of the samples at the limit\n", 100.0 * s.limitShare());
}

// Below the gate is pause, it adds no level and no spectrum; a quiet tone above the gate is speech.
static void testGate()
{
    SpeechMeter m(kGate, kLimit);
    const std::vector<float> quiet = tone(1000.0, 0.1, 1.0);       // rms 0.07 < 0.1
    const std::vector<float> soft = tone(1000.0, 0.2, 1.0);        // rms 0.14

    m.process(quiet.data(), static_cast<int>(quiet.size()));
    CHECK(m.call().pause == 8000 && m.call().speech == 0 && m.call().energy == 0.0);
    m.process(soft.data(), static_cast<int>(soft.size()));
    CHECK(m.call().speech == 8000);
    CHECK(std::fabs(m.call().pauseShare() - 0.5) < 1e-9);
}

// The recent stats are the last 2 s; the call keeps everything until reset.
static void testWindow()
{
    SpeechMeter m(kGate, kLimit);
    const std::vector<float> t = tone(1000.0, 1.0, 3.0);
    const std::vector<float> silence(static_cast<size_t>(3 * SpeechMeter::kSampleRate), 0.0f);

    m.process(t.data(), static_cast<int>(t.size()));
    CHECK(m.recent().speech >= 14400 && m.recent().speech <= 16000);
    CHECK(m.recent().pause == 0);
    m.process(silence.data(), static_cast<int>(silence.size()));
    CHECK(m.recent().speech == 0);
    CHECK(m.recent().pauseShare() == 1.0);
    CHECK(m.call().speech == 24000 && m.call().pause == 24000);
    // input in odd pieces gives the same
    SpeechMeter pieces(kGate, kLimit);
    for (size_t i = 0; i < t.size(); i += 37) {
        pieces.process(t.data() + i, static_cast<int>(std::min<size_t>(37, t.size() - i)));
    }
    CHECK(pieces.call().speech == 24000);
    m.reset();
    CHECK(m.call().empty() && m.recent().empty());
}

int main()
{
    testReferenceTone();
    testBands();
    testClipped();
    testGate();
    testWindow();

    if (g_failures == 0) {
        std::printf("all speech meter tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
