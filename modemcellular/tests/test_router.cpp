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
#include <vector>

#include "callrouter.h"
#include "echoendpoint.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// Records everything the router tells it. Optionally calls back into the router.
struct FakeLeg : public MobileLeg
{
    std::string name;
    CallRouter* router = nullptr;
    std::vector<std::string> events;
    CallRef lastRef = 0;
    bool releaseBackFromRelease = false;

    explicit FakeLeg(const char* n) : name(n) {}

    void setup(CallRef callref, const std::string& callerId, NumberType type, const std::string& dialed) override
    {
        lastRef = callref;
        events.push_back("setup:" + callerId + ":" + dialed + ":" + (type == NumberType::Subscriber ? "sub" : "na"));
    }

    void proceeding(CallRef) override { events.push_back("proceeding"); }
    void alerting(CallRef) override { events.push_back("alerting"); }
    void answer(CallRef) override { events.push_back("answer"); }

    void release(CallRef callref, Cause cause) override
    {
        events.push_back(std::string("release:") + causeName(cause));

        if (releaseBackFromRelease && router) {
            router->release(callref, Cause::Normal);    // must be ignored
        }
    }

    static const char* causeName(Cause c)
    {
        switch (c) {
        case Cause::Normal: return "normal";
        case Cause::Busy: return "busy";
        case Cause::NoChannel: return "nochannel";
        case Cause::OutOfOrder: return "outoforder";
        case Cause::InvalidNumber: return "invalid";
        case Cause::TempFail: return "tempfail";
        }
        return "?";
    }

    std::string joined() const
    {
        std::string s;

        for (const auto& e : events) {
            s += (s.empty() ? "" : ",") + e;
        }

        return s;
    }
};

struct Fixture
{
    SampleClock clock{1000.0};
    TimerService timers{clock};
    CallRouter router;

    explicit Fixture(ToneSet toneSet = ToneSet::Denmark) : router(timers, toneSet) {}

    // Runs due timers; timers started while running wait for the next call.
    void run() { timers.run(); }
    void step(int64_t ms) { clock.advance(ms); timers.run(); }
};

static bool silent(const std::vector<float>& v)
{
    for (float x : v) {
        if (x != 0.0f) {
            return false;
        }
    }

    return true;
}

static float peak(const std::vector<float>& v)
{
    float p = 0.0f;

    for (float x : v) {
        p = std::fmax(p, std::fabs(x));
    }

    return p;
}

// Reads a block of audio for a mobile.
static std::vector<float> readAudio(CallRouter& router, CallRef ref, int count = 160)
{
    std::vector<float> out(count, 7.0f);
    router.audioToMobile(ref, out.data(), count);
    return out;
}

static void testUnregisteredNumberGoesToMusic()
{
    Fixture f;
    FakeLeg a("A");
    f.router.setFallback(CallRouter::Fallback::Music);

    CallRouter::SetupResult r = f.router.setup(a, "100", "999");
    CHECK(r.ok());
    CHECK(r.cause == Cause::Normal);
    CHECK(a.events.empty());            // nothing happens inside setup()

    f.run();
    CHECK(a.joined() == "proceeding");
    f.run();
    CHECK(a.joined() == "proceeding,answer");

    CHECK(peak(readAudio(f.router, r.callref)) > 0.0f);

    f.router.release(r.callref, Cause::Normal);
    CHECK(f.router.callCount() == 0);
    CHECK(a.joined() == "proceeding,answer");   // the releasing leg is not notified
}

static void testEchoFallbackReturnsAudio()
{
    Fixture f;
    FakeLeg a("A");
    f.router.setFallback(CallRouter::Fallback::Echo);

    CallRouter::SetupResult r = f.router.setup(a, "100", "999");
    f.run();
    f.run();
    CHECK(a.joined() == "proceeding,answer");

    // Default echo delay is one second.
    std::vector<float> in(160, 0.25f);
    f.router.audioFromMobile(r.callref, in.data(), 160);
    CHECK(silent(readAudio(f.router, r.callref)));

    for (int i = 0; i < 49; i++) {      // 50 blocks of 160 = 1 s
        f.router.audioFromMobile(r.callref, in.data(), 160);
        readAudio(f.router, r.callref);
    }

    f.router.audioFromMobile(r.callref, in.data(), 160);
    CHECK(readAudio(f.router, r.callref)[0] == 0.25f);
}

static void testAnswerDelayOfEndpoint()
{
    Fixture f;
    FakeLeg a("A");
    f.router.setAnswerDelay(2.0);

    f.router.setup(a, "100", "999");
    f.step(0);
    CHECK(a.joined() == "proceeding");
    f.step(1999);
    CHECK(a.joined() == "proceeding");
    f.step(1);
    CHECK(a.joined() == "proceeding,answer");
}

static void testAudioBeforeAnswerIsDropped()
{
    Fixture f;
    FakeLeg a("A");
    CallRouter::SetupResult r = f.router.setup(a, "100", "999");

    std::vector<float> loud(160, 1.0f);
    CHECK(silent(readAudio(f.router, r.callref)));
    f.router.audioFromMobile(r.callref, loud.data(), 160);  // ignored, must not crash
}

static void testRegisteredNumberRoutesToMobileWithRingback()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    CHECK(f.router.registerSubscriber("100", a));
    CHECK(f.router.registerSubscriber("200", b));
    CHECK(f.router.isRegistered("200"));

    CallRouter::SetupResult r = f.router.setup(a, "100", "200");
    CHECK(r.ok());
    CHECK(b.joined() == "setup:100:200:sub");
    const CallRef refA = r.callref;
    const CallRef refB = b.lastRef;
    CHECK(refB != 0);
    CHECK(refB != refA);

    f.run();
    CHECK(a.joined() == "proceeding");
    CHECK(silent(readAudio(f.router, refA)));       // no tone before the far end rings

    f.router.alerting(refB);
    CHECK(a.joined() == "proceeding,alerting");

    // The caller hears ringback, the called mobile hears nothing.
    const std::vector<float> ring = readAudio(f.router, refA, 800);
    CHECK(peak(ring) > 0.2f);
    CHECK(silent(readAudio(f.router, refB)));

    // Audio from the caller is dropped while the tone plays.
    std::vector<float> aSends(160, 0.5f);
    f.router.audioFromMobile(refA, aSends.data(), 160);

    f.router.answer(refB);
    CHECK(a.joined() == "proceeding,alerting,answer");
    CHECK(silent(readAudio(f.router, refA)));       // tone stopped, nothing was queued
    CHECK(silent(readAudio(f.router, refB)));

    // Audio crosses in both directions.
    std::vector<float> bSends(160);
    std::vector<float> aSends2(160);

    for (int i = 0; i < 160; i++) {
        aSends2[i] = static_cast<float>(i + 1);
        bSends[i] = static_cast<float>(-(i + 1));
    }

    f.router.audioFromMobile(refA, aSends2.data(), 160);
    f.router.audioFromMobile(refB, bSends.data(), 160);
    CHECK(readAudio(f.router, refB) == aSends2);
    CHECK(readAudio(f.router, refA) == bSends);
    CHECK(silent(readAudio(f.router, refB)));       // nothing queued any more

    // The caller hangs up: the called mobile is released, the caller is not told.
    f.router.release(refA, Cause::Normal);
    CHECK(b.joined() == "setup:100:200:sub,release:normal");
    CHECK(a.joined() == "proceeding,alerting,answer");
    CHECK(f.router.callCount() == 0);

    // Both numbers are free again.
    f.router.registerSubscriber("100", a);
    CHECK(f.router.setup(b, "200", "100").ok());
    CHECK(b.joined() == "setup:100:200:sub,release:normal");
    CHECK(a.joined().find("setup:200:100:sub") != std::string::npos);
}

static void testCallerHangsUpWhileRinging()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);

    CallRouter::SetupResult r = f.router.setup(a, "100", "200");
    f.run();
    f.router.alerting(b.lastRef);
    f.router.release(r.callref, Cause::Normal);
    CHECK(b.joined() == "setup:100:200:sub,release:normal");
    CHECK(f.router.callCount() == 0);
    CHECK(silent(readAudio(f.router, r.callref)));  // unknown call: silence
}

static void testCalledMobileCannotBeReachedGivesCongestionTone()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);
    f.router.setToneTimeout(2.0);

    CallRouter::SetupResult r = f.router.setup(a, "100", "200");
    f.run();
    f.router.release(b.lastRef, Cause::NoChannel);      // paging got no answer

    CHECK(a.joined() == "proceeding");
    const std::vector<float> tone = readAudio(f.router, r.callref, 2000);     // 0.25 s
    CHECK(peak(tone) > 0.2f);
    // Denmark congestion cadence: 0.25 s on, 0.25 s off
    const std::vector<float> gap = readAudio(f.router, r.callref, 2000);
    CHECK(silent(gap));

    f.step(1999);
    CHECK(a.joined() == "proceeding");
    f.step(1);
    CHECK(a.joined() == "proceeding,release:nochannel");
    CHECK(f.router.callCount() == 0);
    CHECK(b.joined() == "setup:100:200:sub");           // the called mobile is not told again
}

static void testCallerHangsUpDuringTone()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);

    CallRouter::SetupResult r = f.router.setup(a, "100", "200");
    f.run();
    f.router.release(b.lastRef, Cause::Busy);
    CHECK(f.router.callCount() == 1);

    f.router.release(r.callref, Cause::Normal);
    CHECK(f.router.callCount() == 0);
    CHECK(a.joined() == "proceeding");
    CHECK(b.joined() == "setup:100:200:sub");

    f.step(60000);                                      // the tone timer died with the call
    CHECK(a.joined() == "proceeding");
}

static void testCalledMobileReleasesWithNormalCauseHasNoTone()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);

    f.router.setup(a, "100", "200");
    f.run();
    f.router.release(b.lastRef, Cause::Normal);
    CHECK(a.joined() == "proceeding,release:normal");
    CHECK(f.router.callCount() == 0);
}

static void testCalledMobileHangsUpWhenConnected()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);

    CallRouter::SetupResult r = f.router.setup(a, "100", "200");
    f.run();
    f.router.alerting(b.lastRef);
    f.router.answer(b.lastRef);
    f.router.release(b.lastRef, Cause::Busy);           // cause is passed on, no tone
    CHECK(a.joined() == "proceeding,alerting,answer,release:busy");
    CHECK(f.router.callCount() == 0);
    CHECK(silent(readAudio(f.router, r.callref)));
}

static void testFarEndEventsBeforeProceedingAreOrdered()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);

    f.router.setup(a, "100", "200");
    f.router.alerting(b.lastRef);
    f.router.answer(b.lastRef);
    CHECK(a.events.empty());                            // the caller has not been told "proceeding" yet

    f.run();
    CHECK(a.joined() == "proceeding,alerting,answer");

    // Ringing, then busy before "proceeding": busy tone replaces ringback.
    FakeLeg c("C");
    FakeLeg d("D");
    f.router.registerSubscriber("300", c);
    f.router.registerSubscriber("400", d);
    CallRouter::SetupResult r = f.router.setup(c, "300", "400");
    f.router.alerting(d.lastRef);
    f.router.release(d.lastRef, Cause::Busy);
    f.run();
    CHECK(c.joined() == "proceeding,alerting");

    // Denmark busy cadence: 0.25 s on, 0.25 s off
    CHECK(peak(readAudio(f.router, r.callref, 2000)) > 0.2f);
    CHECK(silent(readAudio(f.router, r.callref, 2000)));
}

static void testBusyCalleeGivesBusyTone()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    FakeLeg c("C");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);
    f.router.registerSubscriber("300", c);
    f.router.setToneTimeout(5.0);

    CHECK(f.router.setup(a, "100", "200").ok());

    CallRouter::SetupResult busy = f.router.setup(c, "300", "200");
    CHECK(busy.ok());                                   // accepted: the caller hears the tone
    CHECK(f.router.callCount() == 2);
    CHECK(c.events.empty());

    f.run();
    CHECK(c.joined() == "proceeding");
    CHECK(b.joined() == "setup:100:200:sub");           // the busy mobile is not disturbed

    CHECK(peak(readAudio(f.router, busy.callref, 2000)) > 0.2f);
    CHECK(silent(readAudio(f.router, busy.callref, 2000)));

    f.step(5000);
    CHECK(c.joined() == "proceeding,release:busy");
    CHECK(f.router.callCount() == 1);
}

static void testCallToBusyCallerNumberAndSelfCall()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    FakeLeg c("C");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);
    f.router.registerSubscriber("300", c);

    CHECK(f.router.setup(a, "100", "200").ok());

    // 100 is in a call as the caller, so it is busy for others too.
    CallRouter::SetupResult toCaller = f.router.setup(c, "300", "100");
    CHECK(toCaller.ok());
    f.run();
    CHECK(c.joined() == "proceeding");
    CHECK(peak(readAudio(f.router, toCaller.callref, 2000)) > 0.2f);

    // A mobile calling its own number hears busy.
    CallRouter::SetupResult self = f.router.setup(c, "300", "300");
    CHECK(self.ok());
    f.run();
    CHECK(c.joined() == "proceeding,proceeding");
    CHECK(peak(readAudio(f.router, self.callref, 2000)) > 0.2f);
    CHECK(b.joined() == "setup:100:200:sub");
}

// The first samples of a tone of a region.
static std::vector<float> toneOf(ToneSet toneSet, Tone tone, int count = 1600)
{
    ToneGenerator reference(toneSet);
    reference.setTone(tone);
    std::vector<float> expected(count);
    reference.read(expected.data(), count);
    return expected;
}

// The base station of a standard plays the tones of its region: the router created with a
// tone set gives that set to the caller for ringback, busy and congestion.
static void testToneSetOfTheBaseStation()
{
    std::vector<std::vector<float>> heardBusy;

    for (ToneSet set : {ToneSet::American, ToneSet::Denmark, ToneSet::Uk, ToneSet::Japan})
    {
        Fixture f(set);
        FakeLeg a("A");
        FakeLeg b("B");
        f.router.registerSubscriber("100", a);
        f.router.registerSubscriber("200", b);

        // Ringback while the called mobile rings
        CallRouter::SetupResult r = f.router.setup(a, "100", "200");
        f.run();
        f.router.alerting(b.lastRef);
        CHECK(readAudio(f.router, r.callref, 1600) == toneOf(set, Tone::Ringback));

        // Busy tone when it turns out to be busy
        f.router.release(b.lastRef, Cause::Busy);
        // one second: the busy tones of UK and Japan only differ after 0.375 s
        heardBusy.push_back(readAudio(f.router, r.callref, 8000));
        CHECK(heardBusy.back() == toneOf(set, Tone::Busy, 8000));
        f.router.release(r.callref, Cause::Normal);

        // Congestion tone when the called mobile cannot be reached
        FakeLeg c("C");
        FakeLeg d("D");
        f.router.registerSubscriber("300", c);
        f.router.registerSubscriber("400", d);
        CallRouter::SetupResult r2 = f.router.setup(c, "300", "400");
        f.run();
        f.router.release(d.lastRef, Cause::NoChannel);
        CHECK(readAudio(f.router, r2.callref, 1600) == toneOf(set, Tone::Congestion));
    }

    for (size_t i = 0; i < heardBusy.size(); i++) {
        for (size_t j = i + 1; j < heardBusy.size(); j++) {
            CHECK(heardBusy[i] != heardBusy[j]);
        }
    }
}

static void testInvalidAndRefused()
{
    Fixture f;
    FakeLeg a("A");

    CallRouter::SetupResult r = f.router.setup(a, "100", "");
    CHECK(!r.ok());
    CHECK(r.cause == Cause::InvalidNumber);

    f.router.setFallbackFactory([] { return std::unique_ptr<AudioEndpoint>(); });
    r = f.router.setup(a, "100", "999");
    CHECK(!r.ok());
    CHECK(r.cause == Cause::NoChannel);
    CHECK(f.router.callCount() == 0);
}

static void testCustomFallbackFactory()
{
    Fixture f;
    FakeLeg a("A");
    int made = 0;

    f.router.setFallbackFactory([&made] {
        made++;
        return std::unique_ptr<AudioEndpoint>(new EchoEndpoint(0.0));
    });

    CallRouter::SetupResult r = f.router.setup(a, "100", "999");
    CHECK(r.ok());
    CHECK(made == 1);
    f.run();
    f.run();

    std::vector<float> in(10, 0.5f);
    f.router.audioFromMobile(r.callref, in.data(), 10);
    CHECK(readAudio(f.router, r.callref, 10)[0] == 0.5f);

    f.router.setFallback(CallRouter::Fallback::Music);  // drops the factory
    f.router.setup(a, "100", "998");
    CHECK(made == 1);
}

static void testRegistrationMoveAndUnregister()
{
    Fixture f;
    FakeLeg oldLeg("old");
    FakeLeg newLeg("new");
    FakeLeg caller("caller");

    f.router.registerSubscriber("200", oldLeg);
    f.router.registerSubscriber("200", newLeg);     // handset moved to another channel

    CHECK(!f.router.unregisterSubscriber("200", oldLeg));   // stale: must not remove the new entry
    CHECK(f.router.isRegistered("200"));

    CHECK(f.router.setup(caller, "100", "200").ok());
    CHECK(newLeg.joined() == "setup:100:200:sub");
    CHECK(oldLeg.events.empty());
    f.router.release(newLeg.lastRef, Cause::Normal);
    f.run();

    CHECK(f.router.unregisterSubscriber("200", newLeg));
    CHECK(!f.router.isRegistered("200"));

    // Not registered any more: the call ends on the fallback endpoint.
    CallRouter::SetupResult r = f.router.setup(caller, "100", "200");
    CHECK(r.ok());
    CHECK(newLeg.joined() == "setup:100:200:sub");
    CHECK(!f.router.registerSubscriber("", newLeg));
}

static void testDetachEndsCallsAndUnregisters()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    FakeLeg c("C");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);
    f.router.registerSubscriber("201", b);
    f.router.setToneTimeout(1.0);

    CHECK(f.router.setup(a, "100", "200").ok());
    CHECK(f.router.setup(c, "300", "999").ok());   // endpoint call of another leg

    const std::vector<std::string> removed = f.router.detach(b);
    CHECK(removed.size() == 2);
    CHECK(!f.router.isRegistered("200"));
    CHECK(!f.router.isRegistered("201"));
    CHECK(f.router.isRegistered("100"));
    CHECK(b.joined() == "setup:100:200:sub");      // the detached leg is not notified

    f.run();                                        // the caller hears congestion tone first
    CHECK(a.joined() == "proceeding");
    CHECK(f.router.callCount() == 2);
    f.step(1000);
    CHECK(a.joined() == "proceeding,release:outoforder");
    CHECK(f.router.callCount() == 1);

    CHECK(c.joined() == "proceeding,answer");
    f.router.detach(c);                             // endpoint call of c ends silently
    CHECK(f.router.callCount() == 0);
    f.step(5000);
    CHECK(c.joined() == "proceeding,answer");
}

static void testDetachOfCalledMobileInConnectedCallReleasesCaller()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);

    f.router.setup(a, "100", "200");
    f.run();
    f.router.answer(b.lastRef);
    f.router.detach(b);
    CHECK(a.joined() == "proceeding,answer,release:outoforder");
    CHECK(f.router.callCount() == 0);

    // Detaching the calling mobile releases the called mobile.
    FakeLeg c("C");
    FakeLeg d("D");
    f.router.registerSubscriber("300", c);
    f.router.registerSubscriber("400", d);
    f.router.setup(c, "300", "400");
    f.router.detach(c);
    CHECK(d.joined() == "setup:300:400:sub,release:outoforder");
    CHECK(f.router.callCount() == 0);
}

static void testReleaseCallbackMayCallBack()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    a.router = &f.router;
    a.releaseBackFromRelease = true;
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);

    f.router.setup(a, "100", "200");
    f.run();
    f.router.answer(b.lastRef);
    f.router.release(b.lastRef, Cause::Normal);
    CHECK(a.joined() == "proceeding,answer,release:normal");
    CHECK(f.router.callCount() == 0);

    f.router.release(12345, Cause::Normal);         // unknown reference
    f.router.alerting(12345);
    f.router.answer(12345);
    CHECK(f.router.callCount() == 0);
}

static void testReleaseDuringProceedingCallback()
{
    // The origin leg releases the call from inside "proceeding" (timer callback context).
    struct ReleasingLeg : public FakeLeg
    {
        using FakeLeg::FakeLeg;
        CallRef ref = 0;
        void proceeding(CallRef callref) override
        {
            FakeLeg::proceeding(callref);
            router->release(callref, Cause::Normal);
        }
    };

    Fixture f;
    ReleasingLeg a("A");
    a.router = &f.router;

    f.router.setup(a, "100", "999");
    f.run();
    CHECK(a.joined() == "proceeding");
    CHECK(f.router.callCount() == 0);
    f.step(5000);
    CHECK(a.joined() == "proceeding");      // the answer timer died with the call
}

static void testNotificationHandlerQueuesInsteadOfDelivering()
{
    Fixture f;
    FakeLeg a("A");
    FakeLeg b("B");
    std::vector<CallRouter::Notification> queue;
    f.router.setNotificationHandler([&queue](CallRouter::Notification&& n) { queue.push_back(std::move(n)); });
    f.router.registerSubscriber("100", a);
    f.router.registerSubscriber("200", b);

    f.router.setup(a, "100", "200");
    f.run();
    CHECK(a.events.empty());
    CHECK(b.events.empty());
    CHECK(queue.size() == 2);

    for (const auto& n : queue) {
        CallRouter::deliver(n);
    }

    CHECK(b.joined() == "setup:100:200:sub");
    CHECK(a.joined() == "proceeding");
}

int main()
{
    testUnregisteredNumberGoesToMusic();
    testEchoFallbackReturnsAudio();
    testAnswerDelayOfEndpoint();
    testAudioBeforeAnswerIsDropped();
    testRegisteredNumberRoutesToMobileWithRingback();
    testCallerHangsUpWhileRinging();
    testCalledMobileCannotBeReachedGivesCongestionTone();
    testCallerHangsUpDuringTone();
    testCalledMobileReleasesWithNormalCauseHasNoTone();
    testCalledMobileHangsUpWhenConnected();
    testFarEndEventsBeforeProceedingAreOrdered();
    testBusyCalleeGivesBusyTone();
    testCallToBusyCallerNumberAndSelfCall();
    testToneSetOfTheBaseStation();
    testInvalidAndRefused();
    testCustomFallbackFactory();
    testRegistrationMoveAndUnregister();
    testDetachEndsCallsAndUnregisters();
    testDetachOfCalledMobileInConnectedCallReleasesCaller();
    testReleaseCallbackMayCallBack();
    testReleaseDuringProceedingCallback();
    testNotificationHandlerQueuesInsteadOfDelivering();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all router tests passed\n");
    return 0;
}
