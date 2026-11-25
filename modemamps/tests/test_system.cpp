///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

#include <cstdio>
#include <string>
#include <vector>

#include "ampsfvc.h"
#include "ampsmessages.h"
#include "ampssystem.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

// The user's network: 353 CC/PC, 356 and 359 VC, SID 54; the phone of the capture
static const int kCc = 0;
static const int kVc = 1;

struct Network
{
    modemcellular::SampleClock clock;
    AmpsSystem system;
    std::vector<std::string> calls;
    int clears = 0;
    int registrations = 0;
    bool accept = true;

    static AmpsSystemConfig config()
    {
        AmpsSystemConfig c;
        c.si.sid = 54;
        c.channels = {{353, AmpsChannelType::ControlPaging, 0}, {356, AmpsChannelType::Voice, 0},
            {359, AmpsChannelType::Voice, 0}};
        return c;
    }

    Network() :
        clock(48000.0),
        system(clock, config())
    {
        system.setCallHandler([this](const AmpsSubscriber&, const std::string& d) {
            calls.push_back(d);
            return accept;
        });
        system.setClearHandler([this](const AmpsSubscriber&) { clears++; });
        system.setRegistrationHandler([this](const AmpsSubscriber&, bool) { registrations++; });
    }

    void wait(double seconds)
    {
        const int64_t step = 480;
        for (int64_t s = 0; s < static_cast<int64_t>(seconds * 48000.0); s += step)
        {
            clock.advance(step);
            system.run();
        }
    }
};

static ReccMessage phone(uint8_t t, uint8_t order, uint8_t ordq, const std::string& dialled)
{
    ReccMessage m;
    m.t = t;
    m.order = order;
    m.ordq = ordq;
    m.min1 = ReccAssembler::min1Of("2177526");
    m.min2 = ReccAssembler::min2Of("216");
    m.esn = 0xe0ec3aac;
    m.hasEsn = true;
    m.dialled = dialled;
    return m;
}

static uint64_t field(const WordDef& def, uint64_t word, const char *name)
{
    return wordField(def, word, name);
}

// The registration: acknowledged once on the FOCC with its order (osmocom echoes it), then gone
static void testRegistration()
{
    Network n;
    FoccMessage m;

    n.system.receivedRecc(kCc, phone(1, kOrderRegistration, 3, ""));
    CHECK(n.registrations == 1);
    CHECK(n.system.nextFocc(kCc, m));
    CHECK(field(words::kExtendedAddressOrder, m.word2, "ORDER") == kOrderRegistration);
    CHECK(field(words::kExtendedAddressOrder, m.word2, "ORDQ") == 3);
    CHECK(field(words::kAbbreviatedAddress, m.word1, "MIN1") == ReccAssembler::min1Of("2177526"));
    CHECK(!n.system.nextFocc(kCc, m));
    CHECK(n.system.transactions().empty());
    CHECK(n.system.registeredCount() == 1);
    int seen = 0;
    n.system.forEachRegistered([&seen](const AmpsSubscriber& s) { seen += s.number() == "2162177526"; });
    CHECK(seen == 1);

    // a power down (MSG_TYPE 1) is acknowledged the same way, and the phone is gone
    ReccMessage down = phone(1, kOrderRegistration, 3, "");
    down.msgType = 1;
    n.system.receivedRecc(kCc, down);
    CHECK(n.system.nextFocc(kCc, m) && field(words::kExtendedAddressOrder, m.word2, "LOCAL/MSG_TYPE") == 1);
    CHECK(n.system.registeredCount() == 0 && n.registrations == 2);
}

// An origination to 555 as in the capture: the designation of 356 (SCC 0, VMAC 4), the SAT of the
// phone, the call, the phone's tone of 1.67 s releases it
static void testOrigination()
{
    Network n;
    FoccMessage m;

    n.system.receivedRecc(kCc, phone(1, kOrderPage, 0, "555"));
    CHECK(n.calls.size() == 1 && n.calls[0] == "555");
    CHECK(n.system.nextFocc(kCc, m));
    CHECK(field(words::kExtendedAddressChannel, m.word2, "CHAN") == 356);
    CHECK(field(words::kExtendedAddressChannel, m.word2, "SCC") == 0 && field(words::kExtendedAddressChannel, m.word2, "VMAC") == 4);
    CHECK(n.system.mode(kVc) == AmpsMode::Silence);         // keyed while the designation goes out
    CHECK(!n.system.nextFocc(kCc, m));                      // sent: the phone is moved
    CHECK(n.system.transactions().size() == 1 && n.system.transactions()[0].state == AmpsSystem::State::MoAssignConfirm);

    n.wait(0.5);
    n.system.satChanged(kVc, 0);
    CHECK(n.system.mode(kVc) == AmpsMode::Audio && n.system.transactions()[0].state == AmpsSystem::State::Call);

    n.wait(5.0);
    n.system.signallingTone(kVc, true);
    n.wait(1.67);
    n.system.signallingTone(kVc, false);
    CHECK(n.clears == 1 && n.system.transactions().empty() && n.system.mode(kVc) == AmpsMode::Off);
}

// The network refuses the call: reorder on the FOCC (ORDER 4), then gone
static void testRefused()
{
    Network n;
    FoccMessage m;

    n.accept = false;
    n.system.receivedRecc(kCc, phone(1, kOrderPage, 0, "555"));
    CHECK(n.system.nextFocc(kCc, m) && field(words::kExtendedAddressOrder, m.word2, "ORDER") == kOrderReorder);
    CHECK(!n.system.nextFocc(kCc, m) && n.system.transactions().empty());
    CHECK(n.system.mode(kVc) == AmpsMode::Off);
}

static void connect(Network& n)
{
    FoccMessage m;
    n.system.receivedRecc(kCc, phone(1, kOrderPage, 0, "555"));
    n.system.nextFocc(kCc, m);
    n.system.nextFocc(kCc, m);
    n.system.satChanged(kVc, 0);
}

// No SAT on the voice channel within 5 s: our release on the FVC, again and again, the network told;
// the phone's tone of 1 s confirms it
static void testNoSat()
{
    Network n;
    FoccMessage m;
    uint64_t w = 0;

    n.system.receivedRecc(kCc, phone(1, kOrderPage, 0, "555"));
    n.system.nextFocc(kCc, m);
    n.system.nextFocc(kCc, m);
    n.wait(4.9);
    CHECK(n.system.mode(kVc) == AmpsMode::Silence);
    n.wait(0.2);
    CHECK(n.system.mode(kVc) == AmpsMode::Frame && n.clears == 1);
    for (int i = 0; i < 3; i++)
    {
        CHECK(n.system.nextFvc(kVc, w));
        CHECK(field(words::kFvcOrder, w, "ORDER") == kOrderRelease && field(words::kFvcOrder, w, "SCC") == 3);
    }
    n.system.signallingTone(kVc, true);
    n.wait(1.0);
    n.system.signallingTone(kVc, false);
    CHECK(n.system.transactions().empty() && n.system.mode(kVc) == AmpsMode::Off);
}

// The network releases the call: the release on the FVC (the network is not told back); no
// confirmation within 5 s: idle anyway
static void testNetworkRelease()
{
    Network n;
    uint64_t w = 0;

    connect(n);
    CHECK(n.system.releaseCall("2162177526"));
    CHECK(n.system.mode(kVc) == AmpsMode::Frame && n.clears == 0);
    CHECK(n.system.nextFvc(kVc, w) && field(words::kFvcOrder, w, "ORDER") == kOrderRelease);
    n.wait(5.1);
    CHECK(n.system.transactions().empty() && n.system.mode(kVc) == AmpsMode::Off);
    CHECK(!n.system.releaseCall("2162177526"));
}

// The SAT lost: back within 5 s the call goes on, gone for 5 s our release
static void testSatFade()
{
    Network n;

    connect(n);
    n.system.satChanged(kVc, -1);
    n.wait(3.0);
    n.system.satChanged(kVc, 0);
    n.wait(5.0);
    CHECK(n.system.mode(kVc) == AmpsMode::Audio && n.clears == 0);
    n.system.satChanged(kVc, -1);
    n.wait(5.1);
    CHECK(n.system.mode(kVc) == AmpsMode::Frame && n.clears == 1);
}

// A tone of 0.5 s is a flash, the call goes on; a tone held 3 s releases
static void testFlashAndHeldTone()
{
    Network n;

    connect(n);
    n.system.signallingTone(kVc, true);
    n.wait(0.5);
    n.system.signallingTone(kVc, false);
    CHECK(n.system.mode(kVc) == AmpsMode::Audio && n.clears == 0);
    n.system.signallingTone(kVc, true);
    n.wait(3.1);
    CHECK(n.clears == 1 && n.system.mode(kVc) == AmpsMode::Off);
}

struct Mt
{
    int alerting = 0;
    int answered = 0;
    std::vector<modemcellular::Cause> failed;
};

static void mtHandlers(Network& n, Mt& mt)
{
    n.system.setCallToPhoneHandlers([&mt](const AmpsSubscriber&) { mt.alerting++; },
        [&mt](const AmpsSubscriber&) { mt.answered++; },
        [&mt](const AmpsSubscriber&, modemcellular::Cause c) { mt.failed.push_back(c); });
}

// A call to the phone: paged on the FOCC (ORDER 0), its page response (T 0), the designation, its SAT,
// the alert on the FVC, its tone (ringing), the tone off (answered), the call
static void testCallToPhone()
{
    Network n;
    Mt mt;
    FoccMessage m;
    uint64_t w = 0;

    mtHandlers(n, mt);
    n.system.receivedRecc(kCc, phone(1, kOrderRegistration, 3, ""));
    n.system.nextFocc(kCc, m);
    n.system.nextFocc(kCc, m);
    CHECK(n.system.startCall("2162177526"));
    CHECK(n.system.nextFocc(kCc, m));
    CHECK(field(words::kExtendedAddressOrder, m.word2, "ORDER") == kOrderPage && field(words::kExtendedAddressOrder, m.word2, "SCC") == 3);
    CHECK(!n.system.nextFocc(kCc, m));                      // sent: waiting for the response
    n.wait(1.0);
    n.system.receivedRecc(kCc, phone(0, kOrderPage, 0, ""));
    CHECK(n.system.nextFocc(kCc, m) && field(words::kExtendedAddressChannel, m.word2, "CHAN") == 356);
    CHECK(!n.system.nextFocc(kCc, m));
    CHECK(n.system.mode(kVc) == AmpsMode::Silence);
    n.system.satChanged(kVc, 0);
    CHECK(n.system.mode(kVc) == AmpsMode::Frame);
    CHECK(n.system.nextFvc(kVc, w) && field(words::kFvcOrder, w, "ORDER") == kOrderAlert);
    CHECK(!n.system.nextFvc(kVc, w) && n.system.mode(kVc) == AmpsMode::Silence);
    n.wait(0.5);
    n.system.signallingTone(kVc, true);
    CHECK(mt.alerting == 1 && n.system.mode(kVc) == AmpsMode::Audio);
    n.wait(3.0);
    n.system.signallingTone(kVc, false);
    CHECK(mt.answered == 1 && mt.failed.empty());
    CHECK(n.system.transactions().size() == 1 && n.system.transactions()[0].state == AmpsSystem::State::Call);
    // a phone in a call is busy
    modemcellular::Cause cause = modemcellular::Cause::Normal;
    CHECK(!n.system.startCall("2162177526", &cause) && cause == modemcellular::Cause::Busy);
    CHECK(!n.system.startCall("21621775", &cause) && cause == modemcellular::Cause::InvalidNumber);
}

// With caller id on: the first alert is the alert with info (ORDER 17, the length, the characters),
// the phone rings on it as on the alert
static void testCallerId()
{
    AmpsSystemConfig c = Network::config();
    c.sendCallerId = true;
    modemcellular::SampleClock clock(48000.0);
    AmpsSystem system(clock, c);
    FoccMessage m;
    uint64_t w = 0;
    std::vector<uint64_t> sent;

    CHECK(system.startCall("2162177526", nullptr, "1000001"));
    system.nextFocc(kCc, m);
    system.nextFocc(kCc, m);
    system.receivedRecc(kCc, phone(0, kOrderPage, 0, ""));
    system.nextFocc(kCc, m);
    system.nextFocc(kCc, m);
    system.satChanged(kVc, 0);
    while (system.nextFvc(kVc, w)) {
        sent.push_back(w);
    }
    CHECK(sent.size() == 5 && sent == fvc::alertWithInfo(0, "1000001"));
    CHECK(system.mode(kVc) == AmpsMode::Silence);
}

// The tester's orders: to an idle phone on the FOCC (sent once, its confirmation on the RECC shown),
// in the call on the FVC (back to the speech after them), while it rings back to the ringing; what
// is refused
static void testOrders()
{
    Network n;
    Mt mt;
    FoccMessage m;
    uint64_t w = 0;
    std::vector<std::string> logs;
    const std::string number = "2162177526";
    const auto logged = [&logs](const std::string& text) {
        for (const std::string& l : logs) {
            if (l == text) {
                return true;
            }
        }
        return false;
    };

    n.system.setLogHandler([&logs](const std::string& t) { logs.push_back(t); });
    mtHandlers(n, mt);

    // idle: audit and message waiting on the FOCC
    CHECK(n.system.audit(number) == AmpsSystem::OrderResult::Sent);
    CHECK(n.system.nextFocc(kCc, m) && field(words::kExtendedAddressOrder, m.word2, "SCC") == 3
        && field(words::kExtendedAddressOrder, m.word2, "ORDER") == kOrderAudit
        && field(words::kAbbreviatedAddress, m.word1, "MIN1") == ReccAssembler::min1Of("2177526"));
    CHECK(!n.system.nextFocc(kCc, m) && n.system.transactions().empty());
    n.system.receivedRecc(kCc, phone(0, kOrderAudit, 0, ""));
    CHECK(logged(number + ": Audit confirmed"));
    CHECK(n.system.messageWaiting(number, 0, 3) == AmpsSystem::OrderResult::Sent);
    CHECK(n.system.nextFocc(kCc, m) && field(words::kExtendedAddressOrder, m.word2, "ORDER") == kOrderMessageWaiting
        && field(words::kExtendedAddressOrder, m.word2, "ORDQ") == 0 && field(words::kExtendedAddressOrder, m.word2, "LOCAL/MSG_TYPE") == 3);
    CHECK(!n.system.nextFocc(kCc, m));
    // change power is of a call only; values out of range
    CHECK(n.system.changePower(number, 2) == AmpsSystem::OrderResult::NoCall);
    CHECK(n.system.changePower(number, 8) == AmpsSystem::OrderResult::Invalid);
    CHECK(n.system.messageWaiting(number, 3, 1) == AmpsSystem::OrderResult::Invalid);
    CHECK(n.system.audit("216217752") == AmpsSystem::OrderResult::Invalid);

    // a call to the phone: paged (busy), ringing (message waiting on the FVC, back to ringing)
    CHECK(n.system.startCall(number));
    CHECK(n.system.audit(number) == AmpsSystem::OrderResult::Busy);
    n.system.nextFocc(kCc, m);
    n.system.nextFocc(kCc, m);
    n.system.receivedRecc(kCc, phone(0, kOrderPage, 0, ""));
    n.system.nextFocc(kCc, m);
    n.system.nextFocc(kCc, m);
    n.system.satChanged(kVc, 0);
    n.system.nextFvc(kVc, w);
    n.system.nextFvc(kVc, w);
    CHECK(n.system.changePower(number, 2) == AmpsSystem::OrderResult::NoCall);     // the alert waits for its tone
    n.system.signallingTone(kVc, true);
    CHECK(mt.alerting == 1 && n.system.mode(kVc) == AmpsMode::Audio);
    CHECK(n.system.messageWaiting(number, 1, 2) == AmpsSystem::OrderResult::Sent);
    CHECK(n.system.mode(kVc) == AmpsMode::Frame);
    CHECK(n.system.nextFvc(kVc, w) && field(words::kFvcOrder, w, "ORDER") == kOrderMessageWaiting
        && field(words::kFvcOrder, w, "ORDQ") == 1 && field(words::kFvcOrder, w, "LOCAL/MSG_TYPE") == 2);
    CHECK(!n.system.nextFvc(kVc, w) && n.system.mode(kVc) == AmpsMode::Audio);

    // answered: change power and audit on the FVC, the phone confirms on the RVC
    n.system.signallingTone(kVc, false);
    CHECK(mt.answered == 1 && n.system.transactions()[0].state == AmpsSystem::State::Call);
    CHECK(n.system.changePower(number, 2) == AmpsSystem::OrderResult::Sent);
    CHECK(n.system.audit(number) == AmpsSystem::OrderResult::Sent);
    CHECK(n.system.nextFvc(kVc, w) && field(words::kFvcOrder, w, "ORDER") == kOrderChangePower && field(words::kFvcOrder, w, "ORDQ") == 2);
    CHECK(n.system.nextFvc(kVc, w) && field(words::kFvcOrder, w, "ORDER") == kOrderAudit);
    CHECK(!n.system.nextFvc(kVc, w) && n.system.mode(kVc) == AmpsMode::Audio);
    n.system.receivedRvcOrder(kVc, kOrderChangePower, 2);
    CHECK(logged(number + ": Change Power to Power Level 2 confirmed"));
    // a text to the display in the call: the words of the flash with info, back to the speech
    CHECK(n.system.flashWithInfo(number, "Call waiting") == AmpsSystem::OrderResult::Sent);
    std::vector<uint64_t> sent;
    while (n.system.nextFvc(kVc, w)) {
        sent.push_back(w);
    }
    CHECK(sent == fvc::flashWithInfo(0, "Call waiting") && n.system.mode(kVc) == AmpsMode::Audio);
    CHECK(n.system.flashWithInfo(number, "") == AmpsSystem::OrderResult::Invalid);
    CHECK(n.system.flashWithInfo(number, std::string(34, 'x')) == AmpsSystem::OrderResult::Invalid);
    CHECK(n.system.flashWithInfo(number, "caf\xc3\xa9") == AmpsSystem::OrderResult::Invalid);
    CHECK(n.system.flashWithInfo("2162170000", "hi") == AmpsSystem::OrderResult::NoCall);
    CHECK(n.system.transactions().size() == 1 && n.system.transactions()[0].state == AmpsSystem::State::Call);

    // a release goes before the orders
    CHECK(n.system.audit(number) == AmpsSystem::OrderResult::Sent);
    CHECK(n.system.releaseCall(number));
    CHECK(n.system.nextFvc(kVc, w) && field(words::kFvcOrder, w, "ORDER") == kOrderRelease);
}

// No page response: paged again after 8 s, given up 4 s after that
static void testNoPageResponse()
{
    Network n;
    Mt mt;
    FoccMessage m;

    mtHandlers(n, mt);
    CHECK(n.system.startCall("2162177526"));
    n.system.nextFocc(kCc, m);
    n.system.nextFocc(kCc, m);
    n.wait(7.9);
    CHECK(!n.system.nextFocc(kCc, m));
    n.wait(0.2);
    CHECK(n.system.nextFocc(kCc, m) && field(words::kExtendedAddressOrder, m.word2, "ORDER") == kOrderPage);
    n.system.nextFocc(kCc, m);
    n.wait(4.1);
    CHECK(mt.failed.size() == 1 && !mt.failed.empty() && mt.failed[0] == modemcellular::Cause::OutOfOrder);
    CHECK(n.system.transactions().empty());
}

// The phone does not ring after three alerts: our release, the call failed
static void testNoRing()
{
    Network n;
    Mt mt;
    FoccMessage m;
    uint64_t w = 0;
    int alerts = 0;

    mtHandlers(n, mt);
    n.system.startCall("2162177526");
    n.system.nextFocc(kCc, m);
    n.system.nextFocc(kCc, m);
    n.system.receivedRecc(kCc, phone(0, kOrderPage, 0, ""));
    n.system.nextFocc(kCc, m);
    n.system.nextFocc(kCc, m);
    n.system.satChanged(kVc, 0);
    for (int k = 0; k < 6; k++)
    {
        while (n.system.mode(kVc) == AmpsMode::Frame && n.system.nextFvc(kVc, w))
        {
            if (field(words::kFvcOrder, w, "ORDER") == kOrderAlert) {
                alerts++;
            } else {
                break;
            }
        }
        n.wait(1.0);
    }
    CHECK(alerts == 3);
    CHECK(mt.failed.size() == 1 && !mt.failed.empty() && mt.failed[0] == modemcellular::Cause::TempFail);
    CHECK(mt.alerting == 0);
}

int main()
{
    testCallToPhone();
    testCallerId();
    testNoPageResponse();
    testNoRing();
    testRegistration();
    testOrigination();
    testRefused();
    testNoSat();
    testNetworkRelease();
    testSatFade();
    testFlashAndHeldTone();
    testOrders();

    if (g_failures == 0) {
        std::printf("all system tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
