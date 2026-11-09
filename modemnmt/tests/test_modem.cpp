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
#include <random>
#include <vector>

#include "biquad.h"
#include "ffsk.h"
#include "nmtmodem.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kPi = 3.14159265358979323846;

// A frame with the standard sync and 140 random encoded bits.
static FrameBits randomFrame(std::mt19937& rng)
{
    FrameBits bits = encodeFrame(Frame());
    MessageBytes message;

    for (auto& b : message) {
        b = static_cast<uint8_t>(rng() & 0xff);
    }
    const EncodedBits encoded = hagelbargerEncode(message);
    for (int i = 0; i < kEncodedBits; i++) {
        bits[static_cast<size_t>(kBitSyncBits + kFrameSyncBits + i)] = encoded[static_cast<size_t>(i)];
    }

    return bits;
}

static bool sameEncoded(const FrameBits& sent, const EncodedBits& received)
{
    for (int i = 0; i < kEncodedBits; i++)
    {
        if (sent[static_cast<size_t>(kBitSyncBits + kFrameSyncBits + i)] != received[static_cast<size_t>(i)]) {
            return false;
        }
    }
    return true;
}

static void testSyncPattern()
{
    const FrameBits bits = encodeFrame(Frame());
    uint32_t window = 0;

    // the receiver looks for the last 16 bits before the information bits
    for (int i = kBitSyncBits + kFrameSyncBits - 16; i < kBitSyncBits + kFrameSyncBits; i++) {
        window = (window << 1) | bits[static_cast<size_t>(i)];
    }
    CHECK(window == 0xaf12);
}

static void testBiquad()
{
    const double fs = 48000.0;
    const double cutoff = 600.0;

    // gain of the cascade for a sine of the given frequency, measured after the transient
    auto gain = [&](double f) {
        BiquadLowpass lp(cutoff, fs, 2);
        double peak = 0.0;
        for (int n = 0; n < 48000; n++)
        {
            const double out = lp.process(std::sin(2.0 * kPi * f * n / fs));
            if (n > 24000 && std::fabs(out) > peak) {
                peak = std::fabs(out);
            }
        }
        return peak;
    };

    CHECK(std::fabs(gain(20.0) - 1.0) < 0.01);
    CHECK(std::fabs(20.0 * std::log10(gain(cutoff)) + 3.01) < 0.3);      // 3 dB down at the cut-off
    CHECK(20.0 * std::log10(gain(4.0 * cutoff)) < -40.0);                // 4th order: about -48 dB

    BiquadLowpass lp(cutoff, fs, 2);
    for (int n = 0; n < 100; n++) {
        lp.process(1.0);
    }
    lp.reset();
    CHECK(std::fabs(lp.process(0.0)) < 1e-12);
}

static void testModulator()
{
    const double fs = 48000.0;

    CHECK(!FfskModulator(fs, 1200.0, 1700.0, 1200.0, 1.0).valid());     // 1700 Hz does not fit
    CHECK(FfskModulator(fs, 1200.0, 1800.0, 1200.0, 1.0).valid());
    CHECK(FfskModulator(fs, 1200.0, 1800.0, 1200.0, 1.0).render(nullptr, 0, false) == 0);   // no source

    // bits 0 1 1 0 0 0 1 0: count the zero crossings per bit, 3 for 1800 Hz and 2 for 1200 Hz
    const int pattern[] = {0, 1, 1, 0, 0, 0, 1, 0};
    size_t next = 0;
    FfskModulator mod(fs, 1200.0, 1800.0, 1200.0, 0.5);
    mod.setBitSource([&]() { return next < 8 ? pattern[next++] : -1; });

    std::vector<float> out(8 * 40 + 100, 99.0f);
    const int count = mod.render(out.data(), static_cast<int>(out.size()), false);
    CHECK(count == 8 * 40);
    CHECK(out[static_cast<size_t>(count)] == 99.0f);                    // nothing written after the end

    float peak = 0.0f;
    for (int i = 0; i < count; i++) {
        peak = std::fmax(peak, std::fabs(out[static_cast<size_t>(i)]));
    }
    CHECK(std::fabs(peak - 0.5f) < 0.01f);

    for (int b = 0; b < 8; b++)
    {
        int crossings = 0;
        for (int i = b * 40 + 1; i < (b + 1) * 40; i++)
        {
            const float a = out[static_cast<size_t>(i - 1)];
            const float c = out[static_cast<size_t>(i)];
            if ((a < 0.0f && c >= 0.0f) || (a > 0.0f && c <= 0.0f)) {
                crossings++;
            }
        }
        // a zero sample at the bit edge counts once, so allow one less
        const int expected = pattern[b] == 0 ? 3 : 2;
        CHECK(crossings == expected || crossings == expected - 1);
    }

    // add mode keeps what is in the buffer
    next = 0;
    mod.reset();
    std::vector<float> base(8 * 40, 1.0f);
    mod.render(base.data(), static_cast<int>(base.size()), true);
    CHECK(std::fabs(base[0] - 1.0f) < 1e-6f);                           // first sample is sin(0)
    CHECK(std::fabs(base[5] - (1.0f + out[5])) < 1e-5f);
}

// Sends frames through the modem and collects what the receiver gets.
struct Loopback
{
    NmtModem tx;
    NmtModem rx;
    std::vector<FrameBits> toSend;
    size_t sendIndex = 0;
    std::vector<RxFrame> received;

    Loopback(double txRate, double rxRate) : tx(txRate, 1.0), rx(rxRate, 1.0)
    {
        tx.setFrameSource([this](FrameBits& frame) {
            if (sendIndex >= toSend.size()) {
                return false;
            }
            frame = toSend[sendIndex++];
            return true;
        });
        rx.setFrameSink([this](const RxFrame& frame) { received.push_back(frame); });
    }
};

static std::vector<float> renderAll(Loopback& loop, double fs, size_t frames, int leadIn, int tail)
{
    const size_t total = static_cast<size_t>(leadIn) + static_cast<size_t>(std::ceil(fs * frames * kFrameBits / kBitRate)) + static_cast<size_t>(tail);
    std::vector<float> audio(total, 0.0f);

    loop.tx.render(audio.data() + leadIn, static_cast<int>(total) - leadIn - tail, false);
    return audio;
}

static void receiveChunked(NmtModem& rx, const std::vector<float>& audio, std::mt19937& rng)
{
    size_t pos = 0;

    while (pos < audio.size())
    {
        // odd block sizes, as the host delivers them
        const size_t n = std::min<size_t>(1 + rng() % 3000, audio.size() - pos);
        rx.receive(audio.data() + pos, static_cast<int>(n));
        pos += n;
    }
}

static void testLoopbackRates()
{
    const double rates[] = {16000.0, 24000.0, 32000.0, 44100.0, 48000.0, 96000.0};
    std::mt19937 rng(1234);

    for (double fs : rates)
    {
        for (int run = 0; run < 3; run++)
        {
            Loopback loop(fs, fs);
            for (int i = 0; i < 6; i++) {
                loop.toSend.push_back(randomFrame(rng));
            }
            const int leadIn = static_cast<int>(rng() % 5000);
            const std::vector<float> audio = renderAll(loop, fs, loop.toSend.size(), leadIn, 6000);
            receiveChunked(loop.rx, audio, rng);

            // the first frame may be lost while the clock locks, all others must come through
            CHECK(loop.received.size() >= 5);
            CHECK(loop.received.size() <= 6);
            const size_t skipped = loop.toSend.size() - loop.received.size();
            for (size_t i = 0; i < loop.received.size(); i++)
            {
                CHECK(sameEncoded(loop.toSend[i + skipped], loop.received[i].bits));
                // a clean signal measures 0.83 to 0.89: the 600 Hz filter rounds the tone changes
                CHECK(loop.received[i].quality > 0.8);
                CHECK(loop.received[i].level > 0.9 && loop.received[i].level < 1.1);
                if (i > 0) {
                    CHECK(loop.received[i].framesElapsed == 1);
                }
            }
            if (loop.received.size() != 6) {
                std::fprintf(stderr, "note: fs %.0f run %d lost %zu frame(s)\n", fs, run, skipped);
            }
        }
    }
}

static void testClockMismatch()
{
    const double fs = 48000.0;
    std::mt19937 rng(77);

    // the receiver runs 0.05 percent fast or slow
    for (double ratio : {1.0005, 0.9995})
    {
        Loopback loop(fs, fs * ratio);
        for (int i = 0; i < 20; i++) {
            loop.toSend.push_back(randomFrame(rng));
        }
        const std::vector<float> audio = renderAll(loop, fs, loop.toSend.size(), 1234, 6000);
        receiveChunked(loop.rx, audio, rng);

        CHECK(loop.received.size() >= 19);
        for (const RxFrame& frame : loop.received) {
            CHECK(frame.quality > 0.8);
        }
    }
}

static void testNoise()
{
    const double fs = 48000.0;
    std::mt19937 rng(99);
    std::normal_distribution<double> gauss(0.0, 1.0);

    // signal peak is kTxPeakFsk, noise is white over the whole audio band
    struct Case { double snrDb; size_t minFrames; };
    const Case cases[] = {{30.0, 40}, {20.0, 40}, {12.0, 40}, {0.0, 30}};
    // measured: 38 of 40 at 0 dB, 17 at -3 dB, none at -6 dB (noise over the whole audio band)

    for (const Case& c : cases)
    {
        Loopback loop(fs, fs);
        for (int i = 0; i < 40; i++) {
            loop.toSend.push_back(randomFrame(rng));
        }
        std::vector<float> audio = renderAll(loop, fs, loop.toSend.size(), 2000, 6000);

        const double signalPower = kTxPeakFsk * kTxPeakFsk / 2.0;
        const double sigma = std::sqrt(signalPower / std::pow(10.0, c.snrDb / 10.0));
        for (float& s : audio) {
            s += static_cast<float>(gauss(rng) * sigma);
        }
        receiveChunked(loop.rx, audio, rng);

        // count frames that arrived bit-exact
        size_t exact = 0;
        for (const RxFrame& frame : loop.received)
        {
            for (const FrameBits& sent : loop.toSend)
            {
                if (sameEncoded(sent, frame.bits)) {
                    exact++;
                    break;
                }
            }
        }
        std::fprintf(stderr, "note: SNR %.0f dB: %zu of 40 frames exact (%zu received)\n", c.snrDb, exact, loop.received.size());
        CHECK(exact >= c.minFrames);
    }
}

// Silence and noise alone must not produce frames.
static void testNoFalseFrames()
{
    const double fs = 48000.0;
    std::mt19937 rng(5);
    std::normal_distribution<double> gauss(0.0, 0.3);
    Loopback loop(fs, fs);
    std::vector<float> audio(static_cast<size_t>(fs * 20.0));

    for (float& s : audio) {
        s = static_cast<float>(gauss(rng));
    }
    receiveChunked(loop.rx, audio, rng);
    CHECK(loop.received.empty());

    std::fill(audio.begin(), audio.end(), 0.0f);
    receiveChunked(loop.rx, audio, rng);
    CHECK(loop.received.empty());
}

static void testFrameReceiver()
{
    // feed bits directly: bit sync tail, frame sync, 140 bits
    const FrameBits bits = encodeFrame(Frame());

    for (double quality : {1.0, 0.5})
    {
        FrameReceiver rx;
        std::vector<RxFrame> got;
        rx.setFrameSink([&](const RxFrame& f) { got.push_back(f); });

        // 3 frames, 166 bits each, with 10 stray bits in front
        for (int i = 0; i < 10; i++) {
            rx.receiveBit(i & 1, quality, kTxPeakFsk);
        }
        for (int f = 0; f < 3; f++) {
            for (int i = 0; i < kFrameBits; i++) {
                rx.receiveBit(bits[static_cast<size_t>(i)], quality, kTxPeakFsk);
            }
        }

        if (quality > 0.9)
        {
            CHECK(got.size() == 3);
            if (got.size() == 3)
            {
                CHECK(got[0].framesElapsed == 0);
                CHECK(got[1].framesElapsed == 1);
                CHECK(got[2].framesElapsed == 1);
                CHECK(std::fabs(got[0].level - 1.0) < 1e-9);
                CHECK(std::fabs(got[0].quality - 1.0) < 1e-9);
                CHECK(sameEncoded(bits, got[0].bits));
            }
        }
        else {
            CHECK(got.empty());     // below the sync quality gate
        }
    }

    // the six lead bits Y2, Y4 .. Y12: any of them set clears the flag, the frame still comes through
    for (int bad = -1; bad < kEncodedBits; bad++)
    {
        FrameBits copy = bits;
        const bool leadBit = bad >= 1 && bad <= 11 && (bad & 1);
        if (bad >= 0) {
            copy[static_cast<size_t>(kBitSyncBits + kFrameSyncBits + bad)] ^= 1;
        }

        FrameReceiver one;
        std::vector<RxFrame> out;
        one.setFrameSink([&](const RxFrame& f) { out.push_back(f); });
        for (int i = 0; i < kFrameBits; i++) {
            one.receiveBit(copy[static_cast<size_t>(i)], 1.0, kTxPeakFsk);
        }
        CHECK(out.size() == 1);
        if (out.size() == 1) {
            CHECK(out[0].leadingZeros == !leadBit);
        }
    }

    // start to start distances of 5 frames + 40 bits and 3 frames - 40 bits round to 5 and 3
    FrameReceiver rx;
    std::vector<RxFrame> got;
    rx.setFrameSink([&](const RxFrame& f) { got.push_back(f); });
    auto sendFrame = [&]() {
        for (int i = 0; i < kFrameBits; i++) {
            rx.receiveBit(bits[static_cast<size_t>(i)], 1.0, kTxPeakFsk);
        }
    };
    auto sendGap = [&](int n) {
        for (int i = 0; i < n; i++) {
            rx.receiveBit(1, 0.0, 0.0);
        }
    };
    sendFrame();
    sendGap(kFrameBits * 4 + 40);
    sendFrame();
    sendGap(kFrameBits * 2 - 40);
    sendFrame();
    CHECK(got.size() == 3);
    if (got.size() == 3)
    {
        CHECK(got[1].framesElapsed == 5);
        CHECK(got[2].framesElapsed == 3);
    }
}

static void testSourceRunsDry()
{
    const double fs = 48000.0;
    std::mt19937 rng(3);
    Loopback loop(fs, fs);
    loop.toSend.push_back(randomFrame(rng));

    std::vector<float> audio(20000, 7.0f);
    const int count = loop.tx.render(audio.data(), static_cast<int>(audio.size()), false);

    CHECK(count == static_cast<int>(fs * kFrameBits / kBitRate));
    CHECK(audio[static_cast<size_t>(count)] == 0.0f);               // silence after the frame
    CHECK(audio.back() == 0.0f);

    // a new frame is picked up on the next call
    loop.toSend.push_back(randomFrame(rng));
    const int again = loop.tx.render(audio.data(), static_cast<int>(audio.size()), false);
    CHECK(again == count);
}

int main()
{
    testSyncPattern();
    testBiquad();
    testModulator();
    testLoopbackRates();
    testClockMismatch();
    testNoise();
    testNoFalseFrames();
    testFrameReceiver();
    testSourceRunsDry();

    if (g_failures == 0) {
        std::printf("all modem tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
