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
#include <complex>
#include <cstdio>
#include <vector>

#include "carrierradio.h"
#include "firfilter.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kPi = 3.14159265358979323846;

static void testRates()
{
    CHECK(CarrierRadio::decimation(96000.0) == 2);
    CHECK(CarrierRadio::decimation(960000.0) == 20);
    CHECK(CarrierRadio::decimation(2400000.0) == 50);
    CHECK(CarrierRadio::decimation(48000.0) == 0);          // odd
    CHECK(CarrierRadio::decimation(144000.0) == 0);         // odd
    CHECK(CarrierRadio::decimation(1000000.0) == 0);
    CHECK(!CarrierReceiver(1e6).valid());
    CHECK(CarrierTransmitter(1.92e6).valid());
}

static void testFilters()
{
    // DC gain 1, and an interpolator of 4 keeps a constant
    const std::vector<float> taps = designLowpass(10000.0, 96000.0, 31);
    double sum = 0.0;
    for (float t : taps) {
        sum += t;
    }
    CHECK(taps.size() == 31 && std::fabs(sum - 1.0) < 1e-6);
    CHECK(lowpassTaps(10000.0, 96000.0) % 2 == 1);

    FirInterpolator up(4, designLowpass(10000.0, 192000.0, 63));
    std::vector<std::complex<float>> in(100, std::complex<float>(1.0f, -0.5f));
    std::vector<std::complex<float>> out;
    up.process(in.data(), in.size(), out);
    CHECK(out.size() == 400);
    CHECK(std::abs(out[399] - std::complex<float>(1.0f, -0.5f)) < 1e-3f);

    FirDecimator down(4, designLowpass(10000.0, 192000.0, 63));
    std::vector<std::complex<float>> low;
    down.process(out.data(), out.size(), low);
    CHECK(low.size() == 100);
    CHECK(std::abs(low[99] - std::complex<float>(1.0f, -0.5f)) < 1e-3f);
}

// The amplitude of a tone in audio (correlation over whole periods).
static double toneAmplitude(const std::vector<float>& audio, size_t from, double frequency)
{
    double re = 0.0;
    double im = 0.0;
    const size_t n = audio.size() - from;

    for (size_t i = from; i < audio.size(); i++)
    {
        const double w = 2.0 * kPi * frequency * static_cast<double>(i) / CarrierRadio::kAudioRate;
        re += audio[i] * std::cos(w);
        im += audio[i] * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(n);
}

// Three carriers through the transmitter and back through the receiver: each one comes back with
// its own tone at its deviation, the neighbour 25 kHz away (the next channel of the plan) leaves
// nothing in it.
static void testLoopback()
{
    const double rate = 960000.0;
    CarrierTransmitter tx(rate);
    CarrierReceiver rx(rate);
    const std::vector<CarrierTuning> carriers = {
        {11, 100000.0, 9000.0, 25000.0},
        {12, 125000.0, 9000.0, 25000.0},
        {13, -300000.0, 9000.0, 25000.0}
    };
    const double tones[3] = {1000.0, 1500.0, 2200.0};
    const double deviation[3] = {3000.0, 4500.0, 2000.0};
    uint64_t audioSample = 0;
    std::vector<std::vector<float>> heard(3);
    int batches = 0;
    uint64_t lastSample = 0;

    tx.setCarriers(carriers);
    rx.setCarriers(carriers);
    tx.setAudioSource([&](size_t index, float* audio, size_t count) {
        for (size_t i = 0; i < count; i++)
        {
            const double t = static_cast<double>(audioSample + i) / CarrierRadio::kAudioRate;
            audio[i] = static_cast<float>(deviation[index] * std::sin(2.0 * kPi * tones[index] * t));
        }
        if (index == 2) {
            audioSample += count;
        }
    });
    rx.setBatchHandler([&](const CarrierBatch& b) {
        CHECK(b.length == CarrierRadio::kBlock);
        CHECK(b.carriers.size() == 3 && b.carriers[0] == 11 && b.carriers[2] == 13);
        CHECK(b.sample == static_cast<uint64_t>(batches) * CarrierRadio::kBlock);
        CHECK(b.levels.size() == 3);
        lastSample = b.sample;
        for (size_t c = 0; c < 3; c++) {
            heard[c].insert(heard[c].end(), b.carrier(c), b.carrier(c) + b.length);
        }
        batches++;
    });

    // 0.5 s in odd chunk sizes
    std::vector<std::complex<float>> stream(7001);
    size_t total = 0;
    while (total < static_cast<size_t>(rate / 2))
    {
        tx.pull(stream.data(), stream.size());
        // each carrier is at 1/3 of full scale
        CHECK(std::fabs(std::abs(stream[3500])) <= 1.0f + 1e-4f);
        rx.feed(stream.data(), stream.size());
        total += stream.size();
    }

    CHECK(batches == static_cast<int>(total / 20 / CarrierRadio::kBlock));
    CHECK(rx.samples() == static_cast<uint64_t>(batches) * CarrierRadio::kBlock);
    CHECK(lastSample + CarrierRadio::kBlock == rx.samples());

    for (size_t c = 0; c < 3; c++)
    {
        const double own = toneAmplitude(heard[c], 4800, tones[c]);
        std::printf("carrier %zu: %.0f Hz deviation at %.0f Hz (sent %.0f)", c, own, tones[c], deviation[c]);
        CHECK(std::fabs(own - deviation[c]) < 0.03 * deviation[c]);
        for (size_t other = 0; other < 3; other++)
        {
            if (other == c) {
                continue;
            }
            const double leak = toneAmplitude(heard[c], 4800, tones[other]);
            std::printf(", %.1f Hz of carrier %zu", leak, other);
            CHECK(leak < 10.0);
        }
        std::printf("\n");
    }
}

// The receiver keeps the sample count without carriers; new carriers start a new block.
static void testNoCarriers()
{
    CarrierReceiver rx(96000.0);
    std::vector<std::complex<float>> stream(9600);
    int batches = 0;

    rx.setBatchHandler([&](const CarrierBatch&) { batches++; });
    rx.feed(stream.data(), stream.size());
    CHECK(batches == 0);
    CHECK(rx.samples() == 4800);

    rx.setCarriers({{1, 0.0, 9000.0, 25000.0}});
    rx.feed(stream.data(), stream.size());
    CHECK(batches == 10);
    CHECK(rx.samples() == 9600);

    CarrierTransmitter tx(96000.0);
    std::vector<std::complex<float>> out(100, std::complex<float>(1.0f, 1.0f));
    tx.pull(out.data(), out.size());
    CHECK(out[50] == std::complex<float>(0.0f, 0.0f));
}

int main()
{
    testRates();
    testFilters();
    testLoopback();
    testNoCarriers();

    if (g_failures == 0) {
        std::printf("all carrier radio tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
