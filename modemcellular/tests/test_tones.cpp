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
#include <utility>
#include <vector>

#include "calltypes.h"
#include "tonegenerator.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static std::vector<float> generate(ToneGenerator& gen, int count)
{
    std::vector<float> out(count);
    gen.read(out.data(), count);
    return out;
}

// Signal power at one frequency over [from, to), Goertzel algorithm.
static double power(const std::vector<float>& x, int from, int to, double frequency)
{
    const double w = 2.0 * 3.14159265358979323846 * frequency / kAudioSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0;
    double s2 = 0.0;

    for (int i = from; i < to; i++)
    {
        const double s = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }

    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

static bool allZero(const std::vector<float>& x, int from, int to)
{
    for (int i = from; i < to; i++) {
        if (x[i] != 0.0f) {
            return false;
        }
    }

    return true;
}

static float peakOf(const std::vector<float>& x, int from, int to)
{
    float p = 0.0f;

    for (int i = from; i < to; i++) {
        p = std::fmax(p, std::fabs(x[i]));
    }

    return p;
}

// One expected tone, written down from the ITU-T E.180 list (ITU Operational Bulletin 781),
// independent of the tables in the generator.
struct Expected
{
    const char* name;
    ToneSet toneSet;
    Tone tone;
    double frequency1;
    double frequency2;                      // 0: single frequency
    std::vector<std::pair<int, bool>> cadence;   // milliseconds, on
};

static const std::vector<Expected> kExpected = {
    {"American ringback",   ToneSet::American, Tone::Ringback,   440.0, 480.0, {{2000, true}, {4000, false}}},
    {"American busy",       ToneSet::American, Tone::Busy,       480.0, 620.0, {{500, true}, {500, false}}},
    {"American congestion", ToneSet::American, Tone::Congestion, 480.0, 620.0, {{300, true}, {200, false}}},
    {"Denmark ringback",    ToneSet::Denmark,  Tone::Ringback,   425.0, 0.0,   {{1000, true}, {4000, false}}},
    {"Denmark busy",        ToneSet::Denmark,  Tone::Busy,       425.0, 0.0,   {{250, true}, {250, false}}},
    {"Denmark congestion",  ToneSet::Denmark,  Tone::Congestion, 425.0, 0.0,   {{250, true}, {250, false}}},
    {"Uk ringback",         ToneSet::Uk,       Tone::Ringback,   400.0, 450.0, {{400, true}, {200, false}, {400, true}, {2000, false}}},
    {"Uk busy",             ToneSet::Uk,       Tone::Busy,       400.0, 0.0,   {{375, true}, {375, false}}},
    {"Uk congestion",       ToneSet::Uk,       Tone::Congestion, 400.0, 0.0,   {{400, true}, {350, false}, {225, true}, {525, false}}},
    {"Japan busy",          ToneSet::Japan,    Tone::Busy,       400.0, 0.0,   {{500, true}, {500, false}}},
    {"Japan congestion",    ToneSet::Japan,    Tone::Congestion, 400.0, 0.0,   {{500, true}, {500, false}}},
};

static void checkCadenceAndLevel(const Expected& e, const std::vector<float>& x, int cycleSamples)
{
    int pos = 0;

    for (int cycle = 0; cycle < 2; cycle++)
    {
        for (const auto& step : e.cadence)
        {
            const int length = step.first * kAudioSampleRate / 1000;

            if (step.second)
            {
                // Signal inside the burst; the 5 ms ramps at both ends are left out.
                if (peakOf(x, pos + 80, pos + length - 80) < 0.1f) {
                    std::fprintf(stderr, "%s: no signal in burst at %d\n", e.name, pos);
                    g_failures++;
                }
            }
            else if (!allZero(x, pos, pos + length))
            {
                std::fprintf(stderr, "%s: signal in pause at %d\n", e.name, pos);
                g_failures++;
            }

            pos += length;
        }
    }

    CHECK(pos == 2 * cycleSamples);
    CHECK(peakOf(x, 0, static_cast<int>(x.size())) <= 0.25f + 1e-6f);
}

static void checkFrequencies(const Expected& e, const std::vector<float>& x)
{
    // First burst, without the ramps.
    const int from = 80;
    const int to = e.cadence[0].first * kAudioSampleRate / 1000 - 80;
    const double far = power(x, from, to, 1000.0);
    const double p1 = power(x, from, to, e.frequency1);

    if (!(p1 > 20.0 * far)) {
        std::fprintf(stderr, "%s: no tone at %.0f Hz\n", e.name, e.frequency1);
        g_failures++;
    }

    if (e.frequency2 > 0.0)
    {
        const double p2 = power(x, from, to, e.frequency2);

        if (!(p2 > 20.0 * far) || !(p1 / p2 > 0.5) || !(p1 / p2 < 2.0)) {
            std::fprintf(stderr, "%s: second tone at %.0f Hz missing or unequal\n", e.name, e.frequency2);
            g_failures++;
        }
    }
}

static void testCadencesFrequenciesAndLevels()
{
    for (const Expected& e : kExpected)
    {
        int cycleSamples = 0;

        for (const auto& step : e.cadence) {
            cycleSamples += step.first * kAudioSampleRate / 1000;
        }

        ToneGenerator gen(e.toneSet);
        gen.setTone(e.tone);
        const std::vector<float> x = generate(gen, 2 * cycleSamples);

        checkCadenceAndLevel(e, x, cycleSamples);
        checkFrequencies(e, x);
    }
}

static void testJapanRingbackIsModulated()
{
    ToneGenerator gen(ToneSet::Japan);
    gen.setTone(Tone::Ringback);
    const std::vector<float> x = generate(gen, 24000);       // 1 s on, 2 s off

    CHECK(allZero(x, 8000, 24000));
    CHECK(peakOf(x, 80, 7920) > 0.2f);

    // 400 Hz carrier with 16 Hz modulation: sidebands at 384 and 416 Hz, carrier at full level.
    const double carrier = power(x, 80, 7920, 400.0);
    CHECK(carrier > 20.0 * power(x, 80, 7920, 1000.0));
    CHECK(power(x, 80, 7920, 416.0) > 0.05 * carrier);
    CHECK(power(x, 80, 7920, 384.0) > 0.05 * carrier);
    CHECK(power(x, 80, 7920, 450.0) < 0.01 * carrier);

    // The 16 Hz modulation gives a period of 500 samples: the level is low at 250 + n * 500.
    CHECK(peakOf(x, 1000, 1030) > 0.2f);        // near the start of a period, high
    CHECK(peakOf(x, 1245, 1255) < 0.02f);       // half a period later, nearly zero
}

static void testOffIsSilentAndUnknownCombinationsToo()
{
    ToneGenerator gen(ToneSet::Uk);
    CHECK(gen.tone() == Tone::Off);
    CHECK(allZero(generate(gen, 800), 0, 800));

    gen.setTone(Tone::Busy);
    gen.setTone(Tone::Off);
    CHECK(allZero(generate(gen, 800), 0, 800));
}

static void testBurstEdgesAreRamped()
{
    ToneGenerator gen(ToneSet::Denmark);
    gen.setTone(Tone::Busy);
    const std::vector<float> x = generate(gen, 2000);

    CHECK(x[0] == 0.0f);
    CHECK(std::fabs(x[1]) < 0.01f);
    CHECK(x[1999] == 0.0f);

    float maxStep = 0.0f;

    for (int i = 1; i < 2000; i++) {
        maxStep = std::fmax(maxStep, std::fabs(x[i] - x[i - 1]));
    }

    // 425 Hz at 0.25 peak: largest sample step is 0.25 * 2 pi * 425 / 8000 = 0.083, plus the ramp
    CHECK(maxStep < 0.09f);
}

static void testSetToneRestartsTheCadence()
{
    ToneGenerator gen(ToneSet::Denmark);
    gen.setTone(Tone::Busy);
    generate(gen, 1000);                // in the middle of a burst
    gen.setTone(Tone::Busy);
    const std::vector<float> x = generate(gen, 400);

    CHECK(x[0] == 0.0f);                // starts at the ramp again
    CHECK(peakOf(x, 100, 400) > 0.1f);
}

static void testBlockSizeDoesNotMatter()
{
    for (ToneSet set : {ToneSet::American, ToneSet::Denmark, ToneSet::Uk, ToneSet::Japan})
    {
        ToneGenerator a(set);
        ToneGenerator b(set);
        a.setTone(Tone::Ringback);
        b.setTone(Tone::Ringback);

        const std::vector<float> whole = generate(a, 20000);
        std::vector<float> pieces;

        for (int i = 0; i < 20000; i += 160)
        {
            const int n = (20000 - i) < 160 ? (20000 - i) : 160;
            const std::vector<float> p = generate(b, n);
            pieces.insert(pieces.end(), p.begin(), p.end());
        }

        CHECK(whole == pieces);
    }
}

static void testRegionsDiffer()
{
    std::vector<std::vector<float>> busy;

    for (ToneSet set : {ToneSet::American, ToneSet::Denmark, ToneSet::Uk, ToneSet::Japan})
    {
        ToneGenerator gen(set);
        gen.setTone(Tone::Busy);
        busy.push_back(generate(gen, 8000));    // UK and Japan busy tones only differ after 0.375 s
    }

    for (size_t i = 0; i < busy.size(); i++) {
        for (size_t j = i + 1; j < busy.size(); j++) {
            CHECK(busy[i] != busy[j]);
        }
    }
}

int main()
{
    testCadencesFrequenciesAndLevels();
    testJapanRingbackIsModulated();
    testOffIsSilentAndUnknownCombinationsToo();
    testBurstEdgesAreRamped();
    testSetToneRestartsTheCadence();
    testBlockSizeDoesNotMatter();
    testRegionsDiffer();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all tone tests passed\n");
    return 0;
}
