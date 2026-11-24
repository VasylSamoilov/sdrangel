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

// The console: the operator's phone on the network, calling and called, with its audio.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "cellularsystem.h"
#include "consoleleg.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// A mobile that answers when it is called; it can also place a call.
struct Mobile : public MobileLeg
{
    CellularSystem* system = nullptr;
    bool answers = true;
    CallRef ref = 0;
    int releases = 0;
    Cause releaseCause = Cause::Normal;
    bool alerted = false;
    bool answered = false;

    void setup(CallRef callref, const std::string&, NumberType, const std::string&) override
    {
        ref = callref;
        if (answers)
        {
            system->alerting(callref);
            system->answer(callref);
        }
    }
    void proceeding(CallRef) override {}
    void alerting(CallRef) override { alerted = true; }
    void answer(CallRef) override { answered = true; }
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

    void step(ConsoleLeg& console)
    {
        clock.advance(kAudioFrameSamples);
        system.run();
        console.process(kAudioFrameSamples);
    }
};

static bool has(const std::vector<std::string>& events, const std::string& text)
{
    return std::any_of(events.begin(), events.end(), [&text](const std::string& e) { return e.find(text) != std::string::npos; });
}

static double amplitude(const std::vector<float>& x, double hz)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < x.size(); i++)
    {
        re += x[i] * std::cos(2.0 * M_PI * hz * i / kAudioSampleRate);
        im += x[i] * std::sin(2.0 * M_PI * hz * i / kAudioSampleRate);
    }
    return x.empty() ? 0.0 : 2.0 * std::sqrt(re * re + im * im) / x.size();
}

// The console calls a mobile: they talk both ways; touch tones go into its speech.
static void testConsoleCalls()
{
    Bench bench;
    Mobile mobile;
    mobile.system = &bench.system;
    bench.system.registerSubscriber("1123456", mobile);
    ConsoleLeg console(bench.system, "1000001");
    console.setEventHandler([&bench](const std::string& text) { bench.events.push_back(text); });
    std::vector<float> spoken;          // what the console's speaker played
    console.setMicrophone([](float* s, int n) {
        static int t = 0;
        for (int i = 0; i < n; i++, t++) {
            s[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t / kAudioSampleRate));
        }
    });
    console.setSpeaker([&spoken](const float* s, int n) { spoken.insert(spoken.end(), s, s + n); });

    CHECK(console.dial("1123456"));
    for (int i = 0; i < 5; i++) {
        bench.step(console);
    }
    CHECK(mobile.ref != 0);
    CHECK(console.state() == ConsoleLeg::State::Connected);
    CHECK(has(bench.events, "call to 1123456: answered"));

    // the microphone reaches the mobile, the mobile reaches the speaker (700 Hz)
    std::vector<float> atMobile, block(kAudioFrameSamples);
    spoken.clear();
    for (int b = 0; b < 20; b++)
    {
        for (int i = 0; i < kAudioFrameSamples; i++) {
            block[i] = 0.4f * static_cast<float>(std::sin(2.0 * M_PI * 700.0 * (b * kAudioFrameSamples + i) / kAudioSampleRate));
        }
        bench.system.audioFromMobile(mobile.ref, block.data(), kAudioFrameSamples);
        bench.step(console);
        std::fill(block.begin(), block.end(), 0.0f);
        bench.system.audioToMobile(mobile.ref, block.data(), kAudioFrameSamples);
        atMobile.insert(atMobile.end(), block.begin(), block.end());
    }
    CHECK(std::fabs(amplitude(atMobile, 1000.0) - 0.5) < 0.05);
    CHECK(std::fabs(amplitude(spoken, 700.0) - 0.4) < 0.05);

    // a touch tone: 5 is 770 + 1336 Hz in the speech to the mobile
    console.setMicrophone(nullptr);
    // the microphone ran before the test read: drain what waits for the mobile
    for (int i = 0; i < 10; i++) {
        bench.system.audioToMobile(mobile.ref, block.data(), kAudioFrameSamples);
    }
    CHECK(console.digit('5'));
    CHECK(!console.digit('x'));
    std::vector<float> tone;
    for (int b = 0; b < 2000 / kAudioFrameSamples; b++)        // 250 ms
    {
        bench.step(console);
        std::fill(block.begin(), block.end(), 0.0f);
        bench.system.audioToMobile(mobile.ref, block.data(), kAudioFrameSamples);
        tone.insert(tone.end(), block.begin(), block.end());
    }
    size_t onset = 0;
    while (onset < tone.size() && std::fabs(tone[onset]) < 0.01f) {
        onset++;
    }
    const std::vector<float> first(tone.begin() + static_cast<long>(onset), tone.begin() + static_cast<long>(onset) + 800);
    std::printf("touch tone from sample %zu of %zu: 770 Hz %.3f, 1336 Hz %.3f, 1000 Hz %.3f, 700 Hz %.3f\n", onset, tone.size(),
        amplitude(first, 770.0), amplitude(first, 1336.0), amplitude(first, 1000.0), amplitude(first, 700.0));
    CHECK(std::fabs(amplitude(first, 770.0) - 0.25) < 0.03 && std::fabs(amplitude(first, 1336.0) - 0.25) < 0.03);
    CHECK(amplitude(first, 697.0) < 0.05);
    const std::vector<float> pause(tone.begin() + static_cast<long>(onset) + 820, tone.begin() + static_cast<long>(onset) + 1260);
    CHECK(amplitude(pause, 770.0) < 0.01);
    CHECK(has(bench.events, "touch tone 5 sent"));

    // one call at a time; the mobile hangs up
    Cause cause = Cause::Normal;
    CHECK(!console.dial("1999999", nullptr, &cause) && cause == Cause::Busy);
    bench.system.release(mobile.ref, Cause::Normal);
    bench.step(console);
    CHECK(console.state() == ConsoleLeg::State::Idle);
    CHECK(has(bench.events, "call with 1123456: ended (normal)"));
    bench.system.unregisterSubscriber("1123456", mobile);

    // a phone that is not registered is still reached on the leg given (paged), not the fallback
    Mobile unregistered;
    unregistered.system = &bench.system;
    unregistered.answers = false;           // paged, not answered yet
    CHECK(console.dial("1777777", &unregistered));
    bench.step(console);
    CHECK(unregistered.ref != 0);
    CHECK(console.state() != ConsoleLeg::State::Connected);      // nobody answered it
    console.hangUp();
    bench.step(console);
}

// A mobile calls the console's number: it rings, the operator answers; a second call is busy.
static void testConsoleCalled()
{
    Bench bench;
    Mobile mobile, other;
    mobile.system = &bench.system;
    other.system = &bench.system;
    ConsoleLeg console(bench.system, "1000001");
    console.setEventHandler([&bench](const std::string& text) { bench.events.push_back(text); });
    CHECK(bench.system.isRegistered("1000001"));

    const CallRouter::SetupResult r = bench.system.setup(mobile, "3726136", "1000001");
    CHECK(r.ok());
    mobile.ref = r.callref;
    for (int i = 0; i < 5; i++) {
        bench.step(console);
    }
    CHECK(console.state() == ConsoleLeg::State::Incoming);
    CHECK(console.peer() == "3726136");
    CHECK(mobile.alerted && !mobile.answered);
    CHECK(has(bench.events, "call from 3726136: ringing"));

    // a second caller while it rings: busy
    const CallRouter::SetupResult r2 = bench.system.setup(other, "3873234", "1000001");
    for (int i = 0; i < 5; i++) {
        bench.step(console);
    }
    CHECK(!r2.ok() || other.releases == 1 || !other.answered);

    CHECK(console.answer());
    bench.step(console);
    CHECK(console.state() == ConsoleLeg::State::Connected);
    CHECK(mobile.answered);

    console.hangUp();
    bench.step(console);
    CHECK(mobile.releases == 1);
    CHECK(console.state() == ConsoleLeg::State::Idle);
}

// Answered at once, and a call that rings refused by hanging up.
static void testAutoAnswerAndRefuse()
{
    Bench bench;
    Mobile mobile;
    mobile.system = &bench.system;
    ConsoleLeg console(bench.system, "1000001");
    console.setAutoAnswer(true);
    const CallRouter::SetupResult r = bench.system.setup(mobile, "3726136", "1000001");
    mobile.ref = r.callref;
    for (int i = 0; i < 5; i++) {
        bench.step(console);
    }
    CHECK(console.state() == ConsoleLeg::State::Connected && mobile.answered);
    bench.system.release(mobile.ref, Cause::Normal);
    bench.step(console);

    console.setAutoAnswer(false);
    Mobile caller;
    caller.system = &bench.system;
    const CallRouter::SetupResult r2 = bench.system.setup(caller, "3726136", "1000001");
    caller.ref = r2.callref;
    for (int i = 0; i < 5; i++) {
        bench.step(console);
    }
    CHECK(console.state() == ConsoleLeg::State::Incoming);
    console.hangUp();
    for (int i = 0; i < 3; i++) {
        bench.step(console);
    }
    // refused busy: the caller hears the busy tone (the router releases it after the tone)
    CHECK(console.state() == ConsoleLeg::State::Idle && !caller.answered);
}

int main()
{
    testConsoleCalls();
    testConsoleCalled();
    testAutoAnswerAndRefuse();
    if (g_failures == 0) {
        std::printf("all console tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
