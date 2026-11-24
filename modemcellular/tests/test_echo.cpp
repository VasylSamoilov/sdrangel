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

#include "echosuppressor.h"

using modemcellular::EchoSuppressor;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kPi = 3.14159265358979323846;
static const int kRate = 8000;
static const int kBlock = 160;

// The far end talking in bursts of irregular length (so the envelope has a shape to match), a
// 1 kHz "voice" at amplitude a.
static std::vector<float> farEnd(double seconds, double a)
{
    std::vector<float> x(static_cast<size_t>(seconds * kRate), 0.0f);
    unsigned seed = 4711;
    size_t pos = 0;
    bool on = true;

    while (pos < x.size())
    {
        seed = seed * 1103515245u + 12345u;
        const size_t length = static_cast<size_t>(kRate * (0.08 + ((seed >> 16) % 400) / 1000.0));
        for (size_t i = pos; i < pos + length && i < x.size(); i++) {
            x[i] = on ? static_cast<float>(a * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / kRate)) : 0.0f;
        }
        pos += length;
        on = !on;
    }
    return x;
}

static double rms(const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; i++) {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

// Runs both directions block by block; rx is changed in place.
static void run(EchoSuppressor& suppressor, const std::vector<float>& tx, std::vector<float>& rx)
{
    for (size_t pos = 0; pos + kBlock <= tx.size(); pos += kBlock)
    {
        suppressor.processTx(tx.data() + pos, kBlock);
        suppressor.processRx(rx.data() + pos, kBlock);
    }
}

// The echo of the far end, 28 dB down and 120 ms late, is measured and taken out.
static void testEcho()
{
    const double seconds = 8.0;
    const std::vector<float> tx = farEnd(seconds, 0.5);
    const size_t delay = static_cast<size_t>(0.120 * kRate);
    const double echoGain = std::pow(10.0, -28.0 / 20.0);
    std::vector<float> rx(tx.size(), 0.0f);
    for (size_t i = delay; i < rx.size(); i++) {
        rx[i] = static_cast<float>(tx[i - delay] * echoGain);
    }
    const std::vector<float> echo = rx;

    EchoSuppressor suppressor;
    run(suppressor, tx, rx);

    std::printf("echo: delay measured %d ms (correlation %.2f)\n", suppressor.measuredDelayMs(), suppressor.measuredCorrelation());
    CHECK(std::abs(suppressor.measuredDelayMs() - 120) <= 4);
    CHECK(suppressor.measuredCorrelation() >= 0.7);
    // after the measurement the echo is about 20 dB further down
    const size_t from = static_cast<size_t>(4.0 * kRate);
    const double left = rms(rx, from, rx.size()) / rms(echo, from, echo.size());
    std::printf("echo: left %.1f dB of the echo\n", 20.0 * std::log10(left + 1e-12));
    CHECK(left < std::pow(10.0, -15.0 / 20.0));
}

// The mobile alone (the far end silent) passes unchanged.
static void testNearAlone()
{
    std::vector<float> tx(static_cast<size_t>(2 * kRate), 0.0f);
    std::vector<float> rx(tx.size());
    for (size_t i = 0; i < rx.size(); i++) {
        rx[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 700.0 * static_cast<double>(i) / kRate));
    }
    const std::vector<float> in = rx;

    EchoSuppressor suppressor;
    run(suppressor, tx, rx);
    for (size_t i = 0; i < rx.size(); i++) {
        CHECK(rx[i] == in[i]);
    }
}

// Double talk: the mobile talks over the far end (louder than any echo can be): it passes.
static void testDoubleTalk()
{
    const std::vector<float> tx = farEnd(4.0, 0.5);
    std::vector<float> rx(tx.size(), 0.0f);
    const size_t start = static_cast<size_t>(2.0 * kRate);
    for (size_t i = start; i < rx.size(); i++) {
        rx[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 700.0 * static_cast<double>(i) / kRate));
    }
    const std::vector<float> in = rx;

    EchoSuppressor suppressor;
    run(suppressor, tx, rx);
    // after the first syllable the mobile's speech is through within 1 dB
    const size_t from = start + static_cast<size_t>(0.1 * kRate);
    const double passed = rms(rx, from, rx.size()) / rms(in, from, in.size());
    CHECK(passed > std::pow(10.0, -1.0 / 20.0));
}

int main()
{
    testEcho();
    testNearAlone();
    testDoubleTalk();

    if (g_failures == 0) {
        std::printf("all echo suppressor tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
