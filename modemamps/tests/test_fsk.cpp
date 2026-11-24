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
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "ampsfsk.h"
#include "ampsstream.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// dotting, the word sync, the data, dotting after it
static std::vector<uint8_t> burst(const std::vector<uint8_t>& data, int dotting)
{
    std::vector<uint8_t> bits;

    for (int i = 0; i < dotting; i++) {
        bits.push_back((i & 1) ? 0 : 1);
    }
    for (int i = kWordSyncBits - 1; i >= 0; i--) {
        bits.push_back((kWordSync >> i) & 1);
    }
    bits.insert(bits.end(), data.begin(), data.end());
    for (int i = 0; i < 20; i++) {
        bits.push_back((i & 1) ? 0 : 1);
    }
    return bits;
}

struct Result
{
    bool found = false;
    std::vector<uint8_t> data;
    double quality = 0.0;
    double level = 0.0;
};

// Modulates the bits at the rate (the bit clock of the sender off by ppm), adds noise (Hz rms) and
// receives the data after the word sync, as the frame receiver will.
static Result loop(double rate, const std::vector<uint8_t>& data, double noise, double ppm, unsigned seed,
    double trackGain = ManchesterDemodulator::kDefaultTrackGain)
{
    const std::vector<uint8_t> bits = burst(data, 30);
    ManchesterModulator mod(rate, kFskBitRate * (1.0 + ppm * 1e-6), kFskDeviation);
    ManchesterDemodulator demod(rate, kFskBitRate, kFskDeviation);
    demod.setTrackGain(trackGain);
    std::mt19937 gen(seed);
    std::normal_distribution<double> gauss(0.0, noise);
    size_t next = 0;
    uint32_t sync = 0;
    Result r;

    mod.setBitSource([&]() { return next < bits.size() ? static_cast<int>(bits[next++]) : -1; });
    demod.setBitSink([&](int bit, double quality, double level) {
        if (r.data.size() >= data.size() && r.found) {
            return;
        }
        if (!r.found)
        {
            sync = ((sync << 1) | static_cast<uint32_t>(bit)) & 0x7ff;
            if (sync == kWordSync)
            {
                r.found = true;
                demod.startFrame();
            }
            return;
        }
        r.data.push_back(static_cast<uint8_t>(bit));
        r.quality += quality / data.size();
        r.level += level / data.size();
        if (r.data.size() == data.size()) {
            demod.endFrame();
        }
    });

    // silence before, in blocks of 10 ms
    std::vector<float> block(static_cast<size_t>(rate / 100.0));
    std::vector<float> zero(block.size(), 0.0f);
    demod.process(zero.data(), static_cast<int>(zero.size()));
    for (;;)
    {
        const int n = mod.render(block.data(), static_cast<int>(block.size()), false);
        for (int i = 0; i < n; i++) {
            block[i] += static_cast<float>(gauss(gen));
        }
        demod.process(block.data(), n);
        if (n < static_cast<int>(block.size())) {
            break;
        }
    }
    return r;
}

static std::vector<uint8_t> randomBits(size_t n, unsigned seed)
{
    std::mt19937 gen(seed);
    std::vector<uint8_t> bits(n);

    for (auto& b : bits) {
        b = static_cast<uint8_t>(gen() & 1);
    }
    return bits;
}

static int errors(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    int e = 0;

    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
        e += a[i] != b[i];
    }
    return e + static_cast<int>(a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
}

// The modulator: a 1 goes from low to high in its middle, a 0 from high to low; the frequency
// stays within the deviation and turns over with half a cosine.
static void testModulator()
{
    const double rate = 96000.0;               // 4.8 samples per half bit
    ManchesterModulator mod(rate, kFskBitRate, kFskDeviation);
    const std::vector<uint8_t> bits = {1, 1, 0, 0};
    size_t next = 0;
    std::vector<float> out(200, 0.0f);

    mod.setBitSource([&]() { return next < bits.size() ? static_cast<int>(bits[next++]) : -1; });
    const int n = mod.render(out.data(), static_cast<int>(out.size()), false);
    CHECK(n == 39);                             // 4 bits are 38.4 samples: 39 start within them
    for (int i = 0; i < n; i++) {
        CHECK(std::fabs(out[i]) <= kFskDeviation + 0.01);
    }
    // the first 1 after the low start: low for half a bit, up to high at its end
    CHECK(out[2] == static_cast<float>(-kFskDeviation));
    CHECK(out[9] > 0.9 * kFskDeviation);
    // the second 1: down to low at its middle, high again at its end
    CHECK(out[14] < -0.95 * kFskDeviation && out[19] > 0.95 * kFskDeviation);
    // the 0 after a 1: high for half a bit, then down
    CHECK(out[21] > 0.95 * kFskDeviation && out[28] < -0.85 * kFskDeviation);
}

static void testRoundTrip()
{
    const std::vector<uint8_t> data = randomBits(480, 1);

    for (double rate : {48000.0, 96000.0, 192000.0})
    {
        const Result r = loop(rate, data, 0.0, 0.0, 2);
        CHECK(r.found);
        CHECK(errors(r.data, data) == 0);
        // between the samples the ramp is read as straight lines: a little lower at 48 kHz
        // (level 0.89, quality 0.82) than at 96 (0.96, 0.91) and 192 kHz (0.99, 0.93)
        CHECK(r.level > 0.85 && r.level < 1.05);
        CHECK(r.quality > 0.78 && r.quality < 1.0);
    }
}

// Noise and a sender clock off by 100 ppm (half a bit over 5000 bits; a RECC message is 288 bits)
static void testNoiseAndClock()
{
    const std::vector<uint8_t> data = randomBits(480, 3);

    for (double rate : {48000.0, 96000.0})
    {
        const Result r = loop(rate, data, 2000.0, 100.0, 4);
        CHECK(r.found);
        CHECK(errors(r.data, data) == 0);
        const Result slow = loop(rate, data, 2000.0, -100.0, 5);
        CHECK(slow.found);
        CHECK(errors(slow.data, data) == 0);
    }
}

// In a frame the clock follows the crossings in the middle of the bits: a sender 1000 ppm off
// (half a bit over the 480 bits) is received; free running (osmocom) it is not
static void testClockTracking()
{
    const std::vector<uint8_t> data = randomBits(480, 7);

    for (double ppm : {1000.0, -1000.0})
    {
        const Result r = loop(48000.0, data, 0.0, ppm, 8);
        CHECK(r.found && errors(r.data, data) == 0);
        const Result free = loop(48000.0, data, 0.0, ppm, 8, 0.0);
        CHECK(free.found && errors(free.data, data) > 0);
    }
}

// Noise alone does not keep a clock running: the dotting is not found in it, or dies within bits.
static void testNoiseOnly()
{
    const double rate = 48000.0;
    ManchesterDemodulator demod(rate, kFskBitRate, kFskDeviation);
    std::mt19937 gen(6);
    std::normal_distribution<double> gauss(0.0, kFskDeviation);
    std::vector<float> block(480);
    int bits = 0;

    demod.setBitSink([&](int, double, double) { bits++; });
    for (int b = 0; b < 100; b++)                // 1 s: 10000 bit periods
    {
        for (auto& s : block) {
            s = static_cast<float>(gauss(gen));
        }
        demod.process(block.data(), static_cast<int>(block.size()));
    }
    CHECK(bits < 1000);
}

// The phase of the dotting is the mean of the offsets of the crossings: the newest one late by d
// (the others on time) makes every offset d, the phase d
static void testDottingPhase()
{
    double phase = 0.0;
    double intervals[ManchesterDemodulator::kDottingIntervals];

    for (double& i : intervals) {
        i = 1.0;
    }
    CHECK(ManchesterDemodulator::dottingPhase(intervals, phase) && std::fabs(phase) < 1e-12);
    intervals[0] = 1.2;
    CHECK(ManchesterDemodulator::dottingPhase(intervals, phase) && std::fabs(phase - 0.2) < 1e-12);
    // an older interval long: the offsets from it on
    intervals[0] = 1.0;
    intervals[3] = 1.35;
    CHECK(ManchesterDemodulator::dottingPhase(intervals, phase) && std::fabs(phase - 0.35 * 4 / 7) < 1e-12);
    // half a bit off: no dotting
    intervals[3] = 1.5;
    CHECK(!ManchesterDemodulator::dottingPhase(intervals, phase));
}

int main()
{
    testDottingPhase();
    testModulator();
    testRoundTrip();
    testNoiseAndClock();
    testClockTracking();
    testNoiseOnly();

    if (g_failures == 0) {
        std::printf("all FSK tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
