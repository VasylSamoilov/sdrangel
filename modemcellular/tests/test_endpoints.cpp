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

#include "calltypes.h"
#include "echoendpoint.h"
#include "musicendpoint.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static void testEchoDelaysAudioExactly()
{
    EchoEndpoint echo(0.5);     // 4000 samples
    std::vector<float> in(160);
    std::vector<float> out(160);
    std::vector<float> all;

    for (int block = 0; block < 40; block++)   // 6400 samples = 0.8 s
    {
        for (int i = 0; i < 160; i++) {
            in[i] = static_cast<float>(block * 160 + i + 1);
        }

        echo.fromMobile(in.data(), 160);
        echo.toMobile(out.data(), 160);
        all.insert(all.end(), out.begin(), out.end());
    }

    bool silent = true;
    bool delayed = true;

    for (int i = 0; i < 4000; i++) {
        silent = silent && (all[i] == 0.0f);
    }

    for (int i = 4000; i < 6400; i++) {
        delayed = delayed && (all[i] == static_cast<float>(i - 4000 + 1));
    }

    CHECK(silent);
    CHECK(delayed);
}

static void testEchoUnderrunGivesSilence()
{
    EchoEndpoint echo(0.0);
    float out[160];
    const float in[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    echo.fromMobile(in, 10);
    echo.toMobile(out, 160);
    CHECK(out[0] == 1.0f);
    CHECK(out[9] == 10.0f);
    CHECK(out[10] == 0.0f);
    CHECK(out[159] == 0.0f);
}

static void testEchoDelayDoesNotGrowWithoutBound()
{
    EchoEndpoint echo(0.1);
    std::vector<float> in(160, 0.5f);
    float out[160];

    for (int i = 0; i < 1000; i++) {    // receive 20 s and never read
        echo.fromMobile(in.data(), 160);
    }

    // At most delay + 1 s is queued, so reading 1.1 s drains it.
    for (int i = 0; i < 55; i++) {      // 55 * 160 = 8800 samples = 1.1 s
        echo.toMobile(out, 160);
    }

    echo.toMobile(out, 160);
    CHECK(out[0] == 0.0f);
}

static void testMusicIsBoundedContinuousAndLoops()
{
    MusicEndpoint music;
    const int total = 8000 * 4;         // melody is 2.1 s long, so this covers two loops
    std::vector<float> samples(total);

    for (int i = 0; i < total; i += 160) {
        music.toMobile(samples.data() + i, 160);
    }

    float peak = 0.0f;
    float maxStep = 0.0f;
    bool anyNonZero = false;

    for (int i = 0; i < total; i++)
    {
        peak = std::fmax(peak, std::fabs(samples[i]));
        anyNonZero = anyNonZero || (samples[i] != 0.0f);

        if (i > 0) {
            maxStep = std::fmax(maxStep, std::fabs(samples[i] - samples[i - 1]));
        }
    }

    CHECK(anyNonZero);
    CHECK(peak <= 0.3f + 1e-6f);
    CHECK(peak > 0.25f);
    // Highest note 1046.5 Hz at level 0.3: largest sample step is about 0.3 * 2 pi * f / fs = 0.25.
    CHECK(maxStep < 0.26f);

    // The melody repeats after 7 notes of 300 ms = 16800 samples.
    bool loops = true;

    for (int i = 0; i + 16800 < total; i++) {
        loops = loops && (std::fabs(samples[i] - samples[i + 16800]) < 1e-5f);
    }

    CHECK(loops);
}

static void testMusicIgnoresReceivedAudio()
{
    MusicEndpoint a;
    MusicEndpoint b;
    float x[160];
    float y[160];
    const float noise[160] = {0.9f};

    a.fromMobile(noise, 160);
    a.toMobile(x, 160);
    b.toMobile(y, 160);

    bool same = true;

    for (int i = 0; i < 160; i++) {
        same = same && (x[i] == y[i]);
    }

    CHECK(same);
}

int main()
{
    testEchoDelaysAudioExactly();
    testEchoUnderrunGivesSilence();
    testEchoDelayDoesNotGrowWithoutBound();
    testMusicIsBoundedContinuousAndLoops();
    testMusicIgnoresReceivedAudio();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all endpoint tests passed\n");
    return 0;
}
