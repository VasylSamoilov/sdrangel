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

// The test caller: a call from the network to a mobile, registered or not, with echo audio.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "cellularsystem.h"
#include "echoendpoint.h"
#include "testcaller.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// A mobile that rings and answers when it is called, and remembers the call.
struct Mobile : public MobileLeg
{
    CellularSystem* system = nullptr;
    bool answers = true;
    CallRef ref = 0;
    std::string dialed;
    int releases = 0;
    Cause releaseCause = Cause::Normal;

    void setup(CallRef callref, const std::string&, NumberType, const std::string& number) override
    {
        ref = callref;
        dialed = number;
        if (answers)
        {
            system->alerting(callref);
            system->answer(callref);
        }
    }
    void proceeding(CallRef) override {}
    void alerting(CallRef) override {}
    void answer(CallRef) override {}
    void release(CallRef, Cause cause) override
    {
        releases++;
        releaseCause = cause;
        ref = 0;
    }
};

struct Bench
{
    SampleClock clock{8000.0};
    CellularSystem system{clock, ToneSet::Denmark};
    std::vector<std::string> events;

    // one block of 20 ms
    void step(TestCaller& caller)
    {
        clock.advance(kAudioFrameSamples);
        system.run();
        caller.process(kAudioFrameSamples);
    }
};

static bool has(const std::vector<std::string>& events, const std::string& text)
{
    return std::any_of(events.begin(), events.end(), [&text](const std::string& e) { return e.find(text) != std::string::npos; });
}

// A mobile that is not registered is called on its leg; it answers and hears its own speech.
static void testCallUnregisteredMobile()
{
    Bench bench;
    Mobile mobile;
    mobile.system = &bench.system;
    TestCaller caller(bench.system);
    caller.setEventHandler([&bench](const std::string& text) { bench.events.push_back(text); });

    CHECK(!bench.system.isRegistered("1123456"));
    Cause cause = Cause::Normal;
    CHECK(caller.call("1123456", "", &mobile, std::make_unique<EchoEndpoint>(0.0), &cause));

    for (int i = 0; i < 5; i++) {
        bench.step(caller);
    }
    CHECK(mobile.ref != 0);
    CHECK(mobile.dialed == "1123456");
    CHECK(caller.state() == TestCaller::State::Connected);
    CHECK(has(bench.events, "answered"));

    // speech of the mobile comes back to it
    std::vector<float> speech(kAudioFrameSamples);
    std::vector<float> heard(kAudioFrameSamples);
    double energy = 0.0;
    for (int block = 0; block < 10; block++)
    {
        for (int i = 0; i < kAudioFrameSamples; i++) {
            speech[i] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * (block * kAudioFrameSamples + i) / 8000.0));
        }
        bench.system.audioFromMobile(mobile.ref, speech.data(), kAudioFrameSamples);
        bench.step(caller);
        std::fill(heard.begin(), heard.end(), 0.0f);
        bench.system.audioToMobile(mobile.ref, heard.data(), kAudioFrameSamples);
        energy = std::accumulate(heard.begin(), heard.end(), energy, [](double sum, float v) { return sum + static_cast<double>(v) * v; });
    }
    CHECK(energy > 1.0);

    // one call at a time
    CHECK(!caller.call("1999999", "", &mobile, std::make_unique<EchoEndpoint>(0.0), &cause));
    CHECK(cause == Cause::Busy);

    // hanging up releases the mobile
    caller.hangUp();
    bench.step(caller);
    CHECK(mobile.releases == 1);
    CHECK(caller.state() == TestCaller::State::Idle);
    CHECK(!bench.system.isRegistered("1123456"));
}

// Without a leg the call goes to the mobile the number is registered on.
static void testCallRegisteredMobile()
{
    Bench bench;
    Mobile mobile;
    mobile.system = &bench.system;
    bench.system.registerSubscriber("5654321", mobile);
    TestCaller caller(bench.system);

    CHECK(caller.call("5654321", "", nullptr, std::make_unique<EchoEndpoint>(0.0)));
    for (int i = 0; i < 5; i++) {
        bench.step(caller);
    }
    CHECK(mobile.ref != 0);
    CHECK(caller.state() == TestCaller::State::Connected);
    bench.system.unregisterSubscriber("5654321", mobile);
}

// The mobile ends the call: the caller is told why and is free again.
static void testMobileEndsTheCall()
{
    Bench bench;
    Mobile mobile;
    mobile.system = &bench.system;
    TestCaller caller(bench.system);
    caller.setEventHandler([&bench](const std::string& text) { bench.events.push_back(text); });

    CHECK(caller.call("1123456", "", &mobile, std::make_unique<EchoEndpoint>(0.0)));
    for (int i = 0; i < 5; i++) {
        bench.step(caller);
    }
    bench.system.release(mobile.ref, Cause::Normal);
    bench.step(caller);
    bench.step(caller);
    CHECK(caller.state() == TestCaller::State::Idle);
    CHECK(has(bench.events, "ended"));
    CHECK(caller.call("1123456", "", &mobile, std::make_unique<EchoEndpoint>(0.0)));
}

// A call that cannot be made at all is refused at once.
static void testRefused()
{
    Bench bench;
    TestCaller caller(bench.system);
    Cause cause = Cause::Normal;

    CHECK(!caller.call("", "", nullptr, std::make_unique<EchoEndpoint>(0.0), &cause));
    CHECK(cause == Cause::InvalidNumber);
    CHECK(caller.state() == TestCaller::State::Idle);
}

int main()
{
    testCallUnregisteredMobile();
    testCallRegisteredMobile();
    testMobileEndsTheCall();
    testRefused();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all test caller tests passed\n");
    return 0;
}
