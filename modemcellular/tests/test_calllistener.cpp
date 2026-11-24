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

#include "calllistener.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const int kOut = 48000;

static std::vector<float> tone(double hz, double amplitude, int count, int first = 0)
{
    std::vector<float> v(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        v[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * hz * (first + i) / 8000.0));
    }
    return v;
}

// amplitude of a tone in one side of the stereo output (from frame `from` on), 0..1 of full scale
static double amplitude(const std::vector<int16_t>& stereo, int side, double hz, size_t from)
{
    double re = 0.0, im = 0.0;
    size_t n = 0;
    for (size_t f = from; 2 * f + 1 < stereo.size(); f++, n++)
    {
        const double v = stereo[2 * f + static_cast<size_t>(side)] / 32767.0;
        re += v * std::cos(2.0 * M_PI * hz * f / kOut);
        im += v * std::sin(2.0 * M_PI * hz * f / kOut);
    }
    return n ? 2.0 * std::sqrt(re * re + im * im) / n : 0.0;
}

// The speech of the call in blocks of 160 (as the channel hands it), 1 s; the phone at 1 kHz, the
// far end at 2 kHz, both at the reference (1.0)
static std::vector<int16_t> run(CallListener& l, double seconds)
{
    std::vector<int16_t> out;
    const int blocks = static_cast<int>(seconds * 50);
    for (int b = 0; b < blocks; b++)
    {
        const std::vector<float> a = tone(1000.0, 1.0, 160, b * 160);
        const std::vector<float> c = tone(2000.0, 1.0, 160, b * 160);
        l.fromPhone(a.data(), 160);
        l.toPhone(c.data(), 160);
        l.take(out);
    }
    return out;
}

static void testModes()
{
    const double ref = CallListener::kReferenceLevel;

    CallListener both(kOut);
    both.setMode(CallListener::Mode::Both);
    std::vector<int16_t> s = run(both, 1.0);
    CHECK(s.size() / 2 >= 47994 && s.size() / 2 <= 48006);       // 6 frames per call sample
    CHECK(std::fabs(amplitude(s, 0, 1000.0, 4800) - ref) < 0.01);   // the phone on the left
    CHECK(amplitude(s, 0, 2000.0, 4800) < 0.002);
    CHECK(std::fabs(amplitude(s, 1, 2000.0, 4800) - ref) < 0.01);   // the far end on the right
    CHECK(amplitude(s, 1, 1000.0, 4800) < 0.002);
    std::printf("both: %zu frames for 8000 call samples, left 1 kHz %.3f, right 2 kHz %.3f of full scale (reference %.3f)\n",
        s.size() / 2, amplitude(s, 0, 1000.0, 4800), amplitude(s, 1, 2000.0, 4800), ref);

    CallListener phone(kOut);
    phone.setMode(CallListener::Mode::FromPhone);
    s = run(phone, 1.0);
    for (int side = 0; side < 2; side++)
    {
        CHECK(std::fabs(amplitude(s, side, 1000.0, 4800) - ref) < 0.01);
        CHECK(amplitude(s, side, 2000.0, 4800) < 0.002);
    }

    CallListener far(kOut);
    far.setMode(CallListener::Mode::ToPhone);
    s = run(far, 1.0);
    for (int side = 0; side < 2; side++)
    {
        CHECK(std::fabs(amplitude(s, side, 2000.0, 4800) - ref) < 0.01);
        CHECK(amplitude(s, side, 1000.0, 4800) < 0.002);
    }

    CallListener off(kOut);
    CHECK(run(off, 1.0).empty());

    // the volume only lowers
    CallListener quiet(kOut);
    quiet.setMode(CallListener::Mode::Both);
    quiet.setVolumeDb(-6.0);
    s = run(quiet, 1.0);
    CHECK(std::fabs(amplitude(s, 0, 1000.0, 4800) - ref * 0.501) < 0.01);
    quiet.setVolumeDb(+6.0);
    s = run(quiet, 1.0);
    CHECK(std::fabs(amplitude(s, 0, 1000.0, 4800) - ref) < 0.01);
}

// One direction that stops does not hold the other up for more than 200 ms.
static void testOneDirection()
{
    CallListener l(kOut);
    l.setMode(CallListener::Mode::Both);
    std::vector<int16_t> out;
    for (int b = 0; b < 50; b++)
    {
        const std::vector<float> a = tone(1000.0, 1.0, 160, b * 160);
        l.fromPhone(a.data(), 160);
        l.take(out);
    }
    CHECK(out.size() / 2 >= 6 * (8000 - 1600 - 160) && out.size() / 2 <= 6 * 8000);
    CHECK(amplitude(out, 1, 1000.0, 4800) < 0.002);                // nothing on the far end's side
}

int main()
{
    testModes();
    testOneDirection();
    if (g_failures == 0) {
        std::printf("all call listener tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
