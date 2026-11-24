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
#include <cstdlib>
#include <vector>

#include "carrierbatch.h"
#include "carrierqueue.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const int kBlock = 480;

static void testBatch()
{
    CarrierBatch b;

    b.reset(1000, kBlock, {7, 3, 12});
    CHECK(b.sample == 1000 && b.length == kBlock);
    CHECK(b.samples.size() == 3u * kBlock);
    CHECK(b.find(3) == 1 && b.find(12) == 2 && b.find(5) == -1);
    b.carrier(1)[0] = 1.0f;
    CHECK(b.samples[kBlock] == 1.0f);
    b.reset(0, -1, {1});
    CHECK(b.samples.empty());
}

// The queue starts with its lead of silence, then hands on what was written, in order.
static void testLeadAndOrder()
{
    CarrierQueue q(100);
    std::vector<float> in(50), out(150);

    CHECK(q.fill() == 100);
    for (int i = 0; i < 50; i++) {
        in[static_cast<size_t>(i)] = static_cast<float>(i + 1);
    }
    q.write(in.data(), in.size());
    CHECK(q.fill() == 150);
    q.read(out.data(), 140, 1.0);
    CHECK(out[99] == 0.0f);
    CHECK(out[100] == 1.0f && out[139] == 40.0f);
    CHECK(q.fill() == 10);
    CHECK(q.underruns() == 0);
    CHECK(std::fabs(q.ratio() - 1.0) <= 200e-6);
}

// An empty queue gives silence and counts once until it holds its lead again.
static void testUnderrun()
{
    CarrierQueue q(10);
    std::vector<float> out(30, 5.0f);

    q.read(out.data(), out.size(), 1.0);
    CHECK(out[29] == 0.0f);
    CHECK(q.underruns() == 1);
    q.read(out.data(), out.size(), 1.0);
    CHECK(q.underruns() == 1);              // still the same time
    q.reset();
    CHECK(q.underruns() == 0 && q.fill() == 10);
}

// After a stall of the writer the queue waits for its whole lead before it reads on: the lead is
// back at once, not after minutes of resampling at 200 ppm.
static void testStall()
{
    const size_t target = 4800;
    CarrierQueue q(target);
    std::vector<float> block(kBlock, 1.0f), out(kBlock);
    double worst = 0.0;

    for (int i = 0; i < 100 * 20; i++)
    {
        if (i < 500 || i >= 510) {          // 100 ms without writes after 5 s
            q.write(block.data(), block.size());
        }
        const double ratio = q.ratio();
        worst = std::max(worst, std::fabs(ratio - 1.0) * 1e6);
        q.read(out.data(), out.size(), ratio);
        if (i == 515) {
            CHECK(q.underruns() == 1);
            // the queue fills again while it is silent
            CHECK(q.fill() >= 5 * kBlock);
        }
    }
    CHECK(q.underruns() == 1);
    // one clock: the lost lead comes back by silence, not by resampling; while the stall lasts the
    // ratio moves a little, well inside the bit rate tolerance of NMT (83 ppm)
    CHECK(worst < 25.0);
    std::printf("stall of 100 ms: fill %zu of %zu afterwards, worst ratio %.2f ppm\n", q.fill(), target, worst);
    CHECK(q.fill() + kBlock >= target && q.fill() <= target + kBlock);
}

// Linear interpolation: a ratio of 0.5 doubles a ramp.
static void testInterpolation()
{
    CarrierQueue q(0);
    std::vector<float> in = {0.0f, 2.0f, 4.0f, 6.0f};
    std::vector<float> out(5);

    q.write(in.data(), in.size());
    q.read(out.data(), out.size(), 0.5);
    CHECK(std::fabs(out[0] - 0.0f) < 1e-6f);
    CHECK(std::fabs(out[1] - 1.0f) < 1e-6f);
    CHECK(std::fabs(out[2] - 2.0f) < 1e-6f);
    CHECK(std::fabs(out[3] - 3.0f) < 1e-6f);
    CHECK(std::fabs(out[4] - 4.0f) < 1e-6f);
}

// The receiver writes a block per 480 samples of its clock, the transmitter reads at a clock that
// is off by ppm. After some minutes the fill is back near the target and nothing ran empty.
static uint64_t runDrift(double ppm, double& finalRatio, double& worstError)
{
    const size_t target = 4800;         // 100 ms at 48 kHz
    CarrierQueue q(target);
    std::vector<float> block(kBlock, 0.5f), out(kBlock + 2);
    double owed = 0.0;

    worstError = 0.0;
    finalRatio = 1.0;
    for (int i = 0; i < 100 * 600; i++)              // 10 minutes
    {
        q.write(block.data(), block.size());
        owed += kBlock * (1.0 + ppm * 1e-6);
        const size_t count = static_cast<size_t>(owed);
        owed -= static_cast<double>(count);
        finalRatio = q.ratio();
        q.read(out.data(), count, finalRatio);
        if (i > 100 * 300)                           // the last 5 minutes
        {
            const double error = std::fabs(static_cast<double>(q.fill()) - static_cast<double>(target));
            worstError = std::max(worstError, error);
        }
    }
    return q.underruns();
}

// As on the air (2026-11-24: fill 4381 of 4800 after 9 s, ratio -142 ppm): the transmitter reads a
// block before the network writes its first, so the fill stays a block below the target. That
// offset is not drift: with both on one clock the ratio stays 1, the bit rate of the signalling
// exact. With a drift the ratio never goes beyond it (no overshoot that moves the bit rate further
// off than the clocks are).
static double runReaderAhead(double ppm)
{
    CarrierQueue q(4800);
    std::vector<float> block(kBlock, 0.5f), out(kBlock + 2);
    double owed = 0.0;
    double worst = 0.0;

    q.read(out.data(), kBlock, q.ratio());          // the transmitter starts first
    for (int i = 0; i < 100 * 300; i++)              // 5 minutes
    {
        owed += kBlock * (1.0 + ppm * 1e-6);
        const size_t count = static_cast<size_t>(owed);
        owed -= static_cast<double>(count);
        const double ratio = q.ratio();
        q.read(out.data(), count, ratio);
        q.write(block.data(), block.size());
        worst = std::max(worst, std::fabs((ratio - 1.0) * 1e6));
    }
    CHECK(q.underruns() == 0);
    return worst;
}

static void testReaderAhead()
{
    const double same = runReaderAhead(0.0);
    std::printf("reader a block ahead, one clock: worst ratio %.2f ppm\n", same);
    CHECK(same < 2.0);      // the first read (before the first write) is in the measured fill
    const double drift = runReaderAhead(50.0);
    std::printf("reader a block ahead, 50 ppm drift: worst ratio %.2f ppm\n", drift);
    CHECK(drift < 55.0);
}

static void testDrift()
{
    for (double ppm : {-50.0, 50.0, 150.0})
    {
        double ratio = 1.0;
        double worst = 0.0;

        CHECK(runDrift(ppm, ratio, worst) == 0);
        // the ratio follows the drift: input per output is 1 / (1 + ppm)
        CHECK(std::fabs((ratio - 1.0) * 1e6 + ppm) < 10.0);
        // proportional control: the fill stays off by drift / gain of the target, plus a block
        CHECK(worst < std::fabs(ppm) * 1e-6 / 0.002 * 4800 + 2 * kBlock);
        std::printf("drift %+.0f ppm: ratio %+.1f ppm, worst fill error %.0f samples\n", ppm, (ratio - 1.0) * 1e6, worst);
    }

    // beyond the limit the ratio stops at 200 ppm and the queue runs empty
    double ratio = 1.0;
    double worst = 0.0;
    CHECK(runDrift(400.0, ratio, worst) > 0);
    CHECK(std::fabs((ratio - 1.0) * 1e6 + 200.0) < 1e-3);
}

int main()
{
    testBatch();
    testLeadAndOrder();
    testUnderrun();
    testStall();
    testInterpolation();
    testReaderAhead();
    testDrift();

    if (g_failures == 0) {
        std::printf("all carrier queue tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
