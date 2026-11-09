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
#include <memory>
#include <string>

#include "timer.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// Clock at 1000 samples per second: one sample is one millisecond, all times exact.
struct Fixture
{
    SampleClock clock{1000.0};
    TimerService service{clock};
    std::string log;

    void step(int64_t ms)
    {
        clock.advance(ms);
        service.run();
    }
};

static void testExpiryOrderAndTies()
{
    Fixture f;
    Timer a(f.service, [&f] { f.log += 'a'; });
    Timer b(f.service, [&f] { f.log += 'b'; });
    Timer c(f.service, [&f] { f.log += 'c'; });

    c.start(1.0);
    a.start(2.0);
    b.start(2.0);   // same expiry as a, started later

    f.step(999);
    CHECK(f.log.empty());
    f.step(1);
    CHECK(f.log == "c");
    f.step(1000);
    CHECK(f.log == "cab");
    CHECK(!f.service.hasPending());
}

static void testFiresInOrderWhenManyAreDueAtOnce()
{
    Fixture f;
    Timer a(f.service, [&f] { f.log += 'a'; });
    Timer b(f.service, [&f] { f.log += 'b'; });

    a.start(3.0);
    b.start(1.0);
    f.step(5000);   // one coarse step, both due
    CHECK(f.log == "ba");
}

static void testRestartReplacesPending()
{
    Fixture f;
    Timer t(f.service, [&f] { f.log += 't'; });

    t.start(1.0);
    f.step(500);
    t.start(1.0);   // new expiry at 1.5 s
    f.step(600);
    CHECK(f.log.empty());
    CHECK(t.active());
    f.step(400);
    CHECK(f.log == "t");
    CHECK(!t.active());
}

static void testStopIsIdempotent()
{
    Fixture f;
    Timer t(f.service, [&f] { f.log += 't'; });

    t.stop();       // never started
    t.start(1.0);
    t.stop();
    t.stop();
    CHECK(!t.active());
    f.step(2000);
    CHECK(f.log.empty());
}

static void testNegativeDelayCountsAsZero()
{
    Fixture f;
    Timer t(f.service, [&f] { f.log += 't'; });

    t.start(-5.0);
    f.service.run();
    CHECK(f.log == "t");
}

static void testSelfRestartWithZeroDelayDoesNotLoop()
{
    Fixture f;
    int count = 0;
    std::unique_ptr<Timer> t;
    t.reset(new Timer(f.service, [&] {
        count++;
        t->start(0.0);
    }));

    t->start(0.0);
    f.service.run();
    CHECK(count == 1);      // restarted timer waits for the next run
    CHECK(t->active());
    f.service.run();
    CHECK(count == 2);
}

static void testCallbackStopsAnotherDueTimer()
{
    Fixture f;
    Timer b(f.service, [&f] { f.log += 'b'; });
    Timer a(f.service, [&f, &b] { f.log += 'a'; b.stop(); });

    a.start(1.0);
    b.start(1.0);
    f.step(1000);
    CHECK(f.log == "a");
    CHECK(!b.active());
}

static void testCallbackStartsAnotherTimer()
{
    Fixture f;
    Timer b(f.service, [&f] { f.log += 'b'; });
    Timer a(f.service, [&f, &b] { f.log += 'a'; b.start(1.0); });

    a.start(1.0);
    f.step(1000);
    CHECK(f.log == "a");
    f.step(1000);
    CHECK(f.log == "ab");
}

static void testDestroyOwnTimerInCallback()
{
    Fixture f;
    std::unique_ptr<Timer> t;
    t.reset(new Timer(f.service, [&] {
        f.log += 't';
        t.reset();      // destroys the timer that is running this callback
    }));

    t->start(1.0);
    f.step(1000);
    CHECK(f.log == "t");
    CHECK(!t);
    CHECK(!f.service.hasPending());
}

static void testDestroyPendingTimer()
{
    Fixture f;
    {
        Timer t(f.service, [&f] { f.log += 't'; });
        t.start(1.0);
        CHECK(f.service.hasPending());
    }
    CHECK(!f.service.hasPending());
    f.step(2000);
    CHECK(f.log.empty());
}

static void testDestroyAnotherDueTimerInCallback()
{
    Fixture f;
    std::unique_ptr<Timer> b;
    b.reset(new Timer(f.service, [&f] { f.log += 'b'; }));
    Timer a(f.service, [&f, &b] { f.log += 'a'; b.reset(); });

    a.start(1.0);
    b->start(1.0);
    f.step(1000);
    CHECK(f.log == "a");
}

static void testFiresExactlyAtExpiry()
{
    Fixture f;
    Timer t(f.service, [&f] { f.log += 't'; });

    t.start(0.5);
    f.step(499);
    CHECK(f.log.empty());
    f.step(1);
    CHECK(f.log == "t");
}

static void testSampleClockExactOverLongRun()
{
    SampleClock clock(44100.0);
    const int64_t block = 441;      // 10 ms
    const int64_t blocks = 44100LL * 3600 / block;   // one hour
    for (int64_t i = 0; i < blocks; i++) {
        clock.advance(block);
    }
    CHECK(std::fabs(clock.now() - 3600.0) < 1e-9);
}

static void testSampleClockRateChangeKeepsTime()
{
    SampleClock clock(1000.0);
    clock.advance(2500);
    CHECK(clock.now() == 2.5);
    clock.setSampleRate(2000.0);
    CHECK(clock.now() == 2.5);
    clock.advance(1000);
    CHECK(clock.now() == 3.0);
    clock.advance(-5);          // ignored
    clock.setSampleRate(0.0);   // ignored
    CHECK(clock.now() == 3.0);
    CHECK(clock.sampleRate() == 2000.0);
}

int main()
{
    testExpiryOrderAndTies();
    testFiresInOrderWhenManyAreDueAtOnce();
    testRestartReplacesPending();
    testStopIsIdempotent();
    testNegativeDelayCountsAsZero();
    testSelfRestartWithZeroDelayDoesNotLoop();
    testCallbackStopsAnotherDueTimer();
    testCallbackStartsAnotherTimer();
    testDestroyOwnTimerInCallback();
    testDestroyPendingTimer();
    testDestroyAnotherDueTimerInCallback();
    testFiresExactlyAtExpiry();
    testSampleClockExactOverLongRun();
    testSampleClockRateChangeKeepsTime();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all timer and clock tests passed\n");
    return 0;
}
