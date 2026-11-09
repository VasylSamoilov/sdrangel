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

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cellularsystem.h"

using namespace modemcellular;

static std::atomic<int> g_failures{0};

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// Leg that records what it is told. Its callbacks call back into the system, which would
// deadlock if the system held its mutex during the callback.
struct Leg : public MobileLeg
{
    CellularSystem* system = nullptr;
    bool autoAnswer = false;                // behave like a mobile that rings and answers at once

    std::mutex mutex;
    std::vector<std::string> events;
    std::atomic<CallRef> setupRef{0};
    std::atomic<int> proceedings{0};
    std::atomic<int> answers{0};
    std::atomic<int> releases{0};
    std::atomic<int> queriedCalls{0};

    std::string joined()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::string s;

        for (const auto& e : events) {
            s += (s.empty() ? "" : ",") + e;
        }

        return s;
    }

    void record(const std::string& e)
    {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(e);
    }

    void setup(CallRef callref, const std::string&, NumberType, const std::string&) override
    {
        record("setup");
        setupRef = callref;
        queriedCalls = static_cast<int>(system->callCount());   // takes the system mutex

        if (autoAnswer)
        {
            system->alerting(callref);
            system->answer(callref);
        }
    }

    void proceeding(CallRef) override { record("proceeding"); proceedings++; }
    void alerting(CallRef) override { record("alerting"); }
    void answer(CallRef) override { record("answer"); answers++; }
    void release(CallRef, Cause cause) override
    {
        record(cause == Cause::Normal ? "release" : "release:other");
        releases++;
    }
};

static bool waitFor(const std::function<bool()>& condition, int milliseconds = 5000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);

    while (!condition())
    {
        if (std::chrono::steady_clock::now() > end) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    return true;
}

static void testCallbacksRunOutsideTheMutex()
{
    SampleClock clock(1000.0);
    CellularSystem system(clock, ToneSet::Denmark);
    Leg a;
    Leg b;
    a.system = &system;
    b.system = &system;
    b.autoAnswer = true;                    // answers from inside its setup() callback

    system.registerSubscriber("100", a);
    system.registerSubscriber("200", b);

    CallRouter::SetupResult r = system.setup(a, "100", "200");
    CHECK(r.ok());
    CHECK(b.joined() == "setup");           // delivered before setup() returned
    CHECK(b.queriedCalls == 1);             // the callback could use the system
    CHECK(a.joined().empty());              // nothing is delivered from inside timers yet

    system.run();
    CHECK(a.joined() == "proceeding,alerting,answer");

    system.release(r.callref, Cause::Normal);
    CHECK(b.joined() == "setup,release");
    CHECK(system.callCount() == 0);
}

static void testSubscriberListener()
{
    SampleClock clock(1000.0);
    CellularSystem system(clock, ToneSet::Denmark);
    Leg a;
    Leg b;
    std::vector<std::string> log;

    system.setSubscriberListener([&log, &system](const std::string& number, bool registered) {
        log.push_back(number + (registered ? "+" : "-"));
        (void)system.isRegistered(number);  // the listener may use the system
    });

    CHECK(system.registerSubscriber("100", a));
    CHECK(system.registerSubscriber("200", a));
    CHECK(!system.registerSubscriber("", a));
    system.unregisterSubscriber("200", b);  // not registered on b: no event
    system.unregisterSubscriber("200", a);
    system.detach(a);                       // removes "100"

    CHECK(log.size() == 4);
    CHECK((log == std::vector<std::string>{"100+", "200+", "200-", "100-"}));
    CHECK(!system.isRegistered("100"));
}

// Leg that, when called, runs the timers (which queues a notification for the victim) and
// then detaches the victim, all from inside a callback while delivery is in progress.
struct DetachingLeg : public Leg
{
    Leg* victim = nullptr;

    void setup(CallRef callref, const std::string& callerId, NumberType type, const std::string& dialed) override
    {
        Leg::setup(callref, callerId, type, dialed);
        system->run();                      // queues "proceeding" for the victim's call
        system->detach(*victim);            // must drop it
    }
};

static void testDetachDropsQueuedNotifications()
{
    SampleClock clock(1000.0);
    CellularSystem system(clock, ToneSet::Denmark);
    Leg victim;
    Leg caller;
    DetachingLeg callee;
    victim.system = &system;
    caller.system = &system;
    callee.system = &system;
    callee.victim = &victim;
    system.registerSubscriber("400", callee);

    system.setup(victim, "100", "999");     // fallback call, "proceeding" is due
    system.setup(caller, "300", "400");     // calls DetachingLeg::setup from the delivery loop

    CHECK(victim.joined().empty());         // the queued "proceeding" was dropped
    CHECK(callee.joined() == "setup");
    CHECK(caller.joined() == "proceeding"); // delivery went on after the callback
    CHECK(system.callCount() == 1);
}

// A leg whose callback blocks until the test lets it go.
struct BlockingLeg : public Leg
{
    std::atomic<bool> inCallback{false};
    std::atomic<bool> proceed{false};
    std::atomic<bool> finished{false};

    void proceeding(CallRef) override
    {
        inCallback = true;

        while (!proceed) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }

        finished = true;
    }
};

static void testDetachWaitsForRunningCallback()
{
    SampleClock clock(1000.0);
    CellularSystem system(clock, ToneSet::Denmark);
    BlockingLeg leg;
    leg.system = &system;

    system.setup(leg, "100", "999");
    std::thread runner([&system] { system.run(); });

    CHECK(waitFor([&leg] { return leg.inCallback.load(); }));

    std::atomic<bool> detached{false};
    std::thread detacher([&] { system.detach(leg); detached = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!detached);                       // still waiting for the callback

    leg.proceed = true;
    detacher.join();
    runner.join();
    CHECK(leg.finished);
    CHECK(detached);
    CHECK(system.callCount() == 0);
}

static void testDetachFromInsideOwnCallbackDoesNotDeadlock()
{
    struct SelfDetachingLeg : public Leg
    {
        void proceeding(CallRef) override { system->detach(*this); record("proceeding"); }
    };

    SampleClock clock(1000.0);
    CellularSystem system(clock, ToneSet::Denmark);
    SelfDetachingLeg leg;
    leg.system = &system;

    system.setup(leg, "100", "999");
    system.run();
    CHECK(leg.joined() == "proceeding");
    CHECK(system.callCount() == 0);
}

// Many threads, each with its own legs, placing and ending calls while one thread runs
// the timers. Fallback calls and mobile to mobile calls.
static void testManyThreads()
{
    SteadyClock clock;
    CellularSystem system(clock, ToneSet::Denmark);
    system.setFallback(CallRouter::Fallback::Echo);
    system.setAnswerDelay(0.0);

    const int kFallbackThreads = 4;
    const int kPairs = 2;
    const int kIterations = 60;

    std::vector<std::unique_ptr<Leg>> fallbackLegs;
    std::vector<std::unique_ptr<Leg>> callers;
    std::vector<std::unique_ptr<Leg>> callees;

    for (int i = 0; i < kFallbackThreads; i++)
    {
        fallbackLegs.emplace_back(new Leg());
        fallbackLegs.back()->system = &system;
    }

    for (int i = 0; i < kPairs; i++)
    {
        callers.emplace_back(new Leg());
        callees.emplace_back(new Leg());
        callers.back()->system = &system;
        callees.back()->system = &system;
        callees.back()->autoAnswer = true;
        system.registerSubscriber("callee" + std::to_string(i), *callees.back());
    }

    std::atomic<bool> stop{false};
    std::thread timekeeper([&] {
        while (!stop) {
            system.run();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    std::atomic<int> completed{0};
    std::vector<std::thread> workers;
    std::vector<float> audio(160, 0.1f);

    for (int t = 0; t < kFallbackThreads; t++)
    {
        workers.emplace_back([&, t] {
            Leg& leg = *fallbackLegs[t];

            for (int i = 0; i < kIterations; i++)
            {
                const int answersBefore = leg.answers;
                CallRouter::SetupResult r = system.setup(leg, "fb" + std::to_string(t), "999");

                if (!r.ok() || !waitFor([&] { return leg.answers > answersBefore; })) {
                    g_failures++;
                    return;
                }

                std::vector<float> out(160);
                system.audioFromMobile(r.callref, audio.data(), 160);
                system.audioToMobile(r.callref, out.data(), 160);
                system.release(r.callref, Cause::Normal);
                completed++;
            }
        });
    }

    for (int p = 0; p < kPairs; p++)
    {
        workers.emplace_back([&, p] {
            Leg& caller = *callers[p];
            Leg& callee = *callees[p];

            for (int i = 0; i < kIterations; i++)
            {
                const int answersBefore = caller.answers;
                CallRouter::SetupResult r = system.setup(caller, "caller" + std::to_string(p), "callee" + std::to_string(p));

                if (!r.ok() || !waitFor([&] { return caller.answers > answersBefore; })) {
                    g_failures++;
                    return;
                }

                std::vector<float> out(160);
                system.audioFromMobile(r.callref, audio.data(), 160);
                system.audioToMobile(callee.setupRef, out.data(), 160);
                system.release(r.callref, Cause::Normal);
                completed++;
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    CHECK(waitFor([&] {
        int releases = 0;

        for (auto& c : callees) {
            releases += c->releases;
        }

        return releases == kPairs * kIterations;
    }));

    stop = true;
    timekeeper.join();

    CHECK(completed == (kFallbackThreads + kPairs) * kIterations);
    CHECK(system.callCount() == 0);

    for (int i = 0; i < kPairs; i++)
    {
        system.detach(*callers[i]);
        system.detach(*callees[i]);
    }
}

int main()
{
    alarm(120);     // a deadlock must end the test, not hang it

    testCallbacksRunOutsideTheMutex();
    testSubscriberListener();
    testDetachDropsQueuedNotifications();
    testDetachWaitsForRunningCallback();
    testDetachFromInsideOwnCallbackDoesNotDeadlock();
    testManyThreads();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures.load());
        return 1;
    }

    std::printf("all system tests passed\n");
    return 0;
}
