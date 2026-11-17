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

// The sum of the carriers stays below full scale with its headroom, whatever the number of
// carriers (a 16 bit device takes 1.0 as +32768: one too many).
static void testLevel()
{
    for (size_t carriers : {1u, 2u, 4u, 8u})
    {
        CarrierTransmitter tx(960000.0);
        std::vector<CarrierTuning> t;
        for (size_t i = 0; i < carriers; i++) {
            t.push_back({static_cast<int>(i + 1), -100000.0 + 25000.0 * static_cast<double>(i), 9000.0, 25000.0});
        }
        tx.setCarriers(t);
        uint64_t at = 0;
        tx.setAudioSource([&](size_t index, float* a, size_t n) {
            // full deviation, a different tone on each carrier
            for (size_t i = 0; i < n; i++) {
                a[i] = static_cast<float>(4700.0 * std::sin(2.0 * kPi * (700.0 + 300.0 * index) * static_cast<double>(at + i) / 48000.0));
            }
            if (index + 1 == carriers) {
                at += n;
            }
        });
        std::vector<std::complex<float>> s(96000);
        double peak = 0.0;
        for (int b = 0; b < 5; b++)
        {
            tx.pull(s.data(), s.size());
            for (const std::complex<float>& v : s) {
                peak = std::max(peak, static_cast<double>(std::max(std::fabs(v.real()), std::fabs(v.imag()))));
            }
        }
        std::printf("%zu carriers: peak %.4f of full scale\n", carriers, peak);
        CHECK(peak < 0.96);
        CHECK(peak > 0.8 / static_cast<double>(carriers));
    }
}

// Without carriers the clock keeps the remainder of the decimation.
static void testIdleClock()
{
    CarrierReceiver rx(960000.0);
    std::vector<std::complex<float>> s(7001);

    for (int i = 0; i < 1000; i++) {
        rx.feed(s.data(), s.size());
    }
    CHECK(rx.samples() == 7001000 / 20);
}

// A carrier that stays is not touched by a change of the others: the receiver hands out exactly
// the same audio for it, the blocks go on, and the transmitter sends it on without a click.
static void testChangeOthers()
{
    const double rate = 960000.0;
    const CarrierTuning a = {1, 100000.0, 9000.0, 25000.0};
    const CarrierTuning b = {2, 150000.0, 9000.0, 25000.0};
    const CarrierTuning c = {3, -200000.0, 9000.0, 25000.0};
    uint64_t at = 0;
    auto audio = [&](size_t index, float* s, size_t n) {
        for (size_t i = 0; i < n; i++) {
            s[i] = static_cast<float>(3000.0 * std::sin(2.0 * kPi * (1000.0 + 500.0 * index) * static_cast<double>(at + i) / 48000.0));
        }
    };

    // the stream: all three carriers
    CarrierTransmitter source(rate);
    source.setCarriers({a, b, c});
    source.setAudioSource([&](size_t index, float* s, size_t n) { audio(index, s, n); if (index == 2) at += n; });
    std::vector<std::complex<float>> stream(960000);
    source.pull(stream.data(), stream.size());

    // a receiver that keeps a and b, and one that changes b for c after 0.3 s (in the middle of a block)
    CarrierReceiver steady(rate), changing(rate);
    std::vector<float> heardSteady, heardChanging;
    std::vector<uint64_t> starts;
    steady.setCarriers({a, b});
    changing.setCarriers({a, b});
    steady.setBatchHandler([&](const CarrierBatch& batch) { heardSteady.insert(heardSteady.end(), batch.carrier(0), batch.carrier(0) + batch.length); });
    changing.setBatchHandler([&](const CarrierBatch& batch) {
        const int i = batch.find(1);
        CHECK(i >= 0);
        heardChanging.insert(heardChanging.end(), batch.carrier(static_cast<size_t>(i)), batch.carrier(static_cast<size_t>(i)) + batch.length);
        starts.push_back(batch.sample);
    });
    const size_t cut = 288000 + 7001;
    steady.feed(stream.data(), stream.size());
    changing.feed(stream.data(), cut);
    changing.setCarriers({c, a});
    changing.feed(stream.data() + cut, stream.size() - cut);

    CHECK(heardSteady.size() == heardChanging.size());
    double diff = 0.0;
    for (size_t i = 0; i < heardSteady.size() && i < heardChanging.size(); i++) {
        diff = std::max(diff, static_cast<double>(std::fabs(heardSteady[i] - heardChanging[i])));
    }
    CHECK(diff == 0.0);
    for (size_t i = 1; i < starts.size(); i++) {
        CHECK(starts[i] == starts[i - 1] + CarrierRadio::kBlock);
    }

    // the transmitter: a stays while b goes and c comes; a received as from a transmitter without
    // the change
    auto run = [&](bool change) {
        CarrierTransmitter tx(rate);
        uint64_t t = 0;
        tx.setCarriers({a, b});
        tx.setAudioSource([&](size_t index, float* out, size_t n) {
            for (size_t i = 0; i < n; i++) {
                out[i] = static_cast<float>(3000.0 * std::sin(2.0 * kPi * (1000.0 + 500.0 * index) * static_cast<double>(t + i) / 48000.0));
            }
            if (index == 1) {
                t += n;
            }
        });
        CarrierReceiver rx(rate);
        rx.setCarriers({a});
        std::vector<float> heard;
        rx.setBatchHandler([&](const CarrierBatch& batch) { heard.insert(heard.end(), batch.carrier(0), batch.carrier(0) + batch.length); });
        std::vector<std::complex<float>> s(9600);
        for (int i = 0; i < 100; i++)
        {
            if (change && i == 50) {
                tx.setCarriers({a, c});
            }
            tx.pull(s.data(), s.size());
            rx.feed(s.data(), s.size());
        }
        return heard;
    };
    const std::vector<float> plain = run(false);
    const std::vector<float> changed = run(true);
    double worst = 0.0;
    size_t worstAt = 0;
    for (size_t i = 0; i < plain.size() && i < changed.size(); i++)
    {
        const double d = std::fabs(plain[i] - changed[i]);
        if (d > worst)
        {
            worst = d;
            worstAt = i;
        }
    }
    size_t differing = 0;
    for (size_t i = 0; i < plain.size() && i < changed.size(); i++) {
        differing += std::fabs(plain[i] - changed[i]) > 1.0 ? 1 : 0;
    }
    std::printf("change of the others: receiver difference %.1f Hz, transmitter difference %.1f Hz at sample %zu of %zu, %zu samples over 1 Hz (change at %d)\n",
        diff, worst, worstAt, plain.size(), differing, 50 * 480);
    CHECK(plain.size() == changed.size());
    CHECK(worst < 30.0);
}

int main()
{
    testRates();
    testFilters();
    testLoopback();
    testNoCarriers();
    testLevel();
    testIdleClock();
    testChangeOthers();

    if (g_failures == 0) {
        std::printf("all carrier radio tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
