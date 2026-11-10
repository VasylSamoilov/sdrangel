///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Andreas Eversberg <jolly@eversberg.eu>                     //
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

// Switching a call in progress to another traffic channel (scheme C2').

#include <cmath>

#include "phone.h"

struct Station
{
    modemcellular::SampleClock clock;
    NmtSystem system;
    NmtChannel* a;
    NmtChannel* b;
    Calls calls;
    Phone phone;

    Station() :
        clock(kRate),
        system(clock)
    {
        ChannelConfig ca = config(System::Nmt450, ChannelType::Tc, 100);
        ChannelConfig cb = config(System::Nmt450, ChannelType::Tc, 102);

        ca.supervisory = 2;
        cb.supervisory = 2;
        a = &system.addChannel(ca, kRate, 1.0);
        b = &system.addChannel(cb, kRate, 1.0);
        connect(system, calls);
    }

    ~Station()
    {
        system.setCallHandler(nullptr);
        system.setClearHandler(nullptr);
    }

    // The phone calls on channel a and the call is connected.
    void connectCall()
    {
        toDialTone(*a, phone);
        dial(*a, phone, "99");
        phoneSends(*a, idleFromPhone(), 1);
        for (int i = 0; i < 12 && a->state() != State::Active; i++)
        {
            FrameBits bits;
            a->nextFrame(bits);
        }
        CHECK(a->state() == State::Active);
        CHECK(system.transactionCount() == 1);
    }

    void wait(double seconds)
    {
        clock.advance(static_cast<int64_t>(kRate * seconds));
        system.run();
    }
};

static bool moreFrames(NmtChannel& channel)
{
    FrameBits bits;

    return channel.nextFrame(bits);
}

static void testSwitch()
{
    Station s;

    s.connectCall();
    CHECK(s.system.moveCall(s.phone.country, s.phone.number));
    CHECK(s.a->state() == State::ScipOrder);
    CHECK(s.b->state() == State::ScipIdent);

    // the order: 3c with the new channel, four times; then four idle frames
    for (int i = 0; i < 4; i++)
    {
        const Frame f = pull(*s.a);
        CHECK(f.mt == MessageType::Frame3c);
        CHECK(f.msNumber == digitsToValue(s.phone.number));
        CHECK(f.channelNo == encodeChannel(System::Nmt450, 102, 1));
        CHECK(f.tcNo == encodeTc(System::Nmt450, 102, 1));
        CHECK(f.channelNo == f.tcNo);
    }
    for (int i = 0; i < 4; i++) {
        CHECK(pull(*s.a).mt == MessageType::Frame6);
    }
    // then the speech is back on the old channel until the phone shows up
    CHECK(!moreFrames(*s.a));
    CHECK(s.a->state() == State::Active);
    CHECK(s.a->dspMode() == DspMode::Audio);

    // the new channel does not look free
    CHECK(pull(*s.b).mt == MessageType::Frame6);

    // a second switch is refused while one is going on
    CHECK(!s.system.moveCall(s.phone.country, s.phone.number));

    // the phone identifies itself on the new channel: the call is there
    phoneSends(*s.b, seizureFrame(*s.b, s.phone, MessageType::Frame10b), 1);
    CHECK(s.b->state() == State::Active);
    CHECK(s.a->state() == State::Idle);
    CHECK(s.system.transactionCount() == 1);
    CHECK(s.calls.cleared == 0);
    CHECK(pull(*s.a).mt == MessageType::Frame4);            // the old channel is free again

    // and it can be released there
    CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
    CHECK(s.b->state() == State::MtRelease);
}

static void testAskedForIdentity()
{
    Station s;

    s.connectCall();
    CHECK(s.system.moveCall(s.phone.country, s.phone.number, 102));
    for (int i = 0; i < 8; i++) {
        pull(*s.a);
    }
    moreFrames(*s.a);

    // no identity within T': the phone is asked for it, for four frames
    s.wait(0.6);
    CHECK(s.b->state() == State::ScipIdentReq);
    for (int i = 0; i < 4; i++)
    {
        const Frame f = pull(*s.b);
        CHECK(f.mt == MessageType::Frame3b);
        CHECK(f.msNumber == digitsToValue(s.phone.number));
    }
    CHECK(pull(*s.b).mt == MessageType::Frame6);

    // it answers
    phoneSends(*s.b, seizureFrame(*s.b, s.phone, MessageType::Frame10b), 1);
    CHECK(s.b->state() == State::Active);
    CHECK(s.a->state() == State::Idle);
}

static void testPhoneDoesNotCome()
{
    Station s;

    s.connectCall();
    CHECK(s.system.moveCall(s.phone.country, s.phone.number));
    for (int i = 0; i < 8; i++) {
        pull(*s.a);
    }
    moreFrames(*s.a);
    CHECK(s.a->state() == State::Active);

    s.wait(0.6);
    CHECK(s.b->state() == State::ScipIdentReq);
    s.wait(1.0);

    // forced release on the new channel, the call stays on the old one
    CHECK(s.b->state() == State::ScipRelease);
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.b), 15));
    }
    CHECK(s.b->state() == State::Idle);
    CHECK(s.a->state() == State::Active);
    CHECK(s.system.transactionCount() == 1);
    CHECK(s.calls.cleared == 0);

    // the switch can be tried again
    CHECK(s.system.moveCall(s.phone.country, s.phone.number));
}

static void testReleaseDuringSwitch()
{
    // the far end hangs up while the phone is on its way
    {
        Station s;

        s.connectCall();
        CHECK(s.system.moveCall(s.phone.country, s.phone.number));
        CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
        CHECK(s.a->state() == State::MtRelease);
        CHECK(s.b->state() == State::ScipRelease);
        for (int i = 0; i < 4; i++) {
            CHECK(isLineSignal(pull(*s.b), 15));
        }
        CHECK(s.b->state() == State::Idle);
        CHECK(s.system.transactionCount() == 1);
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x11111), 1);
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.a->state() == State::Idle);
    }

    // the phone hangs up on the old channel
    {
        Station s;

        s.connectCall();
        CHECK(s.system.moveCall(s.phone.country, s.phone.number));
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x11111), 1);
        CHECK(s.calls.cleared == 1);
        CHECK(s.b->state() == State::Idle);
        CHECK(s.a->state() == State::MoRelease);
        for (int i = 0; i < 4; i++) {
            pull(*s.a);
        }
        CHECK(s.a->state() == State::Idle);
        CHECK(s.system.transactionCount() == 0);
    }

    // the phone hangs up on the old channel after the order, while the new one asks for it
    {
        Station s;

        s.connectCall();
        CHECK(s.system.moveCall(s.phone.country, s.phone.number));
        for (int i = 0; i < 8; i++) {
            pull(*s.a);
        }
        moreFrames(*s.a);
        s.wait(0.6);
        CHECK(s.b->state() == State::ScipIdentReq);
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x11111), 1);
        CHECK(s.calls.cleared == 1);
        CHECK(s.b->state() == State::Idle);             // no release signal: nobody is there
        CHECK(s.a->state() == State::MoRelease);
    }
}

// An old channel that is not rendered ends its order by itself; so does a release that is not.
static void testWatchdogs()
{
    Station s;

    s.connectCall();
    CHECK(s.system.moveCall(s.phone.country, s.phone.number));
    s.wait(5.1);
    CHECK(s.a->state() == State::Active);               // the call goes on
    CHECK(s.b->state() == State::ScipRelease);
    CHECK(s.calls.cleared == 0);
    // moveCall is refused until the new channel is free again
    CHECK(!s.system.moveCall(s.phone.country, s.phone.number));
    s.wait(5.1);
    CHECK(s.b->state() == State::Idle);
}

// Only the phone of the call, on the new channel, with the right area, completes the switch.
static void testWrongIdentity()
{
    Station s;

    s.connectCall();
    CHECK(s.system.moveCall(s.phone.country, s.phone.number));

    Phone other;
    other.number = "654321";
    phoneSends(*s.b, seizureFrame(*s.b, other, MessageType::Frame10b), 1);
    CHECK(s.b->state() == State::ScipIdent);

    // a frame for the old channel
    phoneSends(*s.b, seizureFrame(*s.a, s.phone, MessageType::Frame10b), 1);
    CHECK(s.b->state() == State::ScipIdent);

    // nothing has been ordered yet: a 10b of the phone is not an answer to anything
    phoneSends(*s.b, seizureFrame(*s.b, s.phone, MessageType::Frame10b), 1);
    CHECK(s.b->state() == State::ScipIdent);
    CHECK(s.a->state() == State::ScipOrder);

    // the order is going out: the phone is early, and the switch works. Before the identity
    // request the area of the old channel is all the phone knows, it is not checked.
    pull(*s.a);
    Frame f = seizureFrame(*s.b, s.phone, MessageType::Frame10b);
    f.trafficArea = 0x22;
    phoneSends(*s.b, f, 1);
    CHECK(s.b->state() == State::Active);
    CHECK(s.a->state() == State::Idle);
    CHECK(s.system.transactionCount() == 1);
}

// T' is counted from the first order frame, not from the decision.
static void testIdentityTimerStartsWithOrder()
{
    Station s;

    s.connectCall();
    CHECK(s.system.moveCall(s.phone.country, s.phone.number));
    s.wait(0.4);
    CHECK(s.b->state() == State::ScipIdent);
    pull(*s.a);                                     // the first 3c, 0.4 s after the decision
    s.wait(0.3);
    CHECK(s.b->state() == State::ScipIdent);        // 0.3 s after the order
    s.wait(0.3);
    CHECK(s.b->state() == State::ScipIdentReq);     // 0.6 s: T' is over

    // and the request is given up 853 ms later, not before
    s.wait(0.5);
    CHECK(s.b->state() == State::ScipIdentReq);
    s.wait(0.5);
    CHECK(s.b->state() == State::ScipRelease);
}

// After the identity request the area counts: the right one completes, another one does not.
static void testAreaAfterRequest()
{
    Station s;

    s.connectCall();
    CHECK(s.system.moveCall(s.phone.country, s.phone.number));
    for (int i = 0; i < 8; i++) {
        pull(*s.a);
    }
    moreFrames(*s.a);
    s.wait(0.6);
    CHECK(s.b->state() == State::ScipIdentReq);

    Frame f = seizureFrame(*s.b, s.phone, MessageType::Frame10b);
    f.trafficArea = 0x22;
    phoneSends(*s.b, f, 1);
    CHECK(s.b->state() == State::ScipIdentReq);

    f.areaInfo = 1;                                 // the traffic area 0x11 of the channels:
    f.trafficArea = 1;                              // two bits in the area info, the rest in Y2
    phoneSends(*s.b, f, 1);
    CHECK(s.b->state() == State::Active);
}

// The new channel has to listen for the tone the phone sends: a channel with another one is no target.
static void testSupervisoryChoice()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    Calls calls;
    Phone phone;
    ChannelConfig c1 = config(System::Nmt450, ChannelType::Tc, 100);
    ChannelConfig c2 = config(System::Nmt450, ChannelType::Tc, 102);
    ChannelConfig c3 = config(System::Nmt450, ChannelType::Tc, 104);

    c1.supervisory = 2;
    c2.supervisory = 3;
    c3.supervisory = 2;
    NmtChannel& a = system.addChannel(c1, kRate, 1.0);
    NmtChannel& b = system.addChannel(c2, kRate, 1.0);
    NmtChannel& c = system.addChannel(c3, kRate, 1.0);
    connect(system, calls);
    toDialTone(a, phone);
    dial(a, phone, "99");
    phoneSends(a, idleFromPhone(), 1);
    for (int i = 0; i < 12 && a.state() != State::Active; i++) {
        moreFrames(a);
    }

    CHECK(!system.moveCall(phone.country, phone.number, 102));
    CHECK(system.moveCall(phone.country, phone.number));
    CHECK(b.state() == State::Idle);
    CHECK(c.state() == State::ScipIdent);
    system.setCallHandler(nullptr);
    system.setClearHandler(nullptr);
}

// The order is on air in the NMT-900 coding of the channel: N1'N2'N3' and Y1 equal NaNbNc.
static void testOrder900()
{
    struct Case { int channel; unsigned channelNo; unsigned area; unsigned tcNo; };
    static const Case cases[] = {
        {100,  0x264, 0x01, 0x064},
        {600,  0xA58, 0x01, 0x258},
        {1030, 0x206, 0x81, 0x406},
        {1500, 0x3DC, 0x81, 0x5DC},
    };

    for (const Case& c : cases)
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        Calls calls;
        Phone phone;
        ChannelConfig c1 = config(System::Nmt900, ChannelType::Tc, 200);
        ChannelConfig c2 = config(System::Nmt900, ChannelType::Tc, c.channel);

        c1.trafficArea = 0x01;
        c2.trafficArea = 0x01;
        NmtChannel& a = system.addChannel(c1, kRate, 1.0);
        system.addChannel(c2, kRate, 1.0);
        connect(system, calls);
        toDialTone(a, phone);
        dial(a, phone, "99");
        phoneSends(a, idleFromPhone(), 1);
        for (int i = 0; i < 12 && a.state() != State::Active; i++) {
            moreFrames(a);
        }

        CHECK(system.moveCall(phone.country, phone.number));
        const Frame f = pull(a);
        CHECK(f.mt == MessageType::Frame3c);
        CHECK(f.channelNo == c.channelNo);
        CHECK(f.trafficArea == c.area);
        CHECK(f.tcNo == c.tcNo);
        // the channel number in the order is the new channel
        int channel = 0, power = 0;
        CHECK(decodeChannel(System::Nmt900, f.channelNo, channel, power));
        CHECK((channel | ((f.trafficArea & 0x80) ? 0x400 : 0)) == static_cast<int>(f.tcNo));
        system.setCallHandler(nullptr);
        system.setClearHandler(nullptr);
    }
}

// The tone of the call is on both channels while the call is switched.
static void testSupervisoryTone()
{
    Station s;

    s.connectCall();
    CHECK(s.system.moveCall(s.phone.country, s.phone.number));

    std::vector<float> a(4800), b(4800);
    s.a->render(a.data(), static_cast<int>(a.size()), false);
    s.b->render(b.data(), static_cast<int>(b.size()), false);

    // a channel in use without the tone, for comparison: the frames alone
    Station quiet;
    std::vector<float> idle(4800);
    quiet.a->render(idle.data(), static_cast<int>(idle.size()), false);

    // supervisory tone 2 is 3985 Hz: correlate against it
    const auto level = [](const std::vector<float>& v) {
        double c = 0.0, sn = 0.0;
        for (size_t i = 0; i < v.size(); i++)
        {
            const double phase = 2.0 * 3.14159265358979 * 3985.0 * static_cast<double>(i) / kRate;
            c += v[i] * std::cos(phase);
            sn += v[i] * std::sin(phase);
        }
        return std::sqrt(c * c + sn * sn) / static_cast<double>(v.size());
    };

    CHECK(level(a) > 10.0 * level(idle) + 1e-4);
    CHECK(level(b) > 10.0 * level(idle) + 1e-4);
}

static void testRefused()
{
    Station s;

    // no call yet
    CHECK(!s.system.moveCall(s.phone.country, s.phone.number));

    s.connectCall();
    // unknown phone, a channel that does not exist, a channel that is not free
    Phone other;
    other.number = "654321";
    CHECK(!s.system.moveCall(other.country, other.number));
    CHECK(!s.system.moveCall(s.phone.country, s.phone.number, 104));
    CHECK(!s.system.moveCall(s.phone.country, s.phone.number, 100));
    CHECK(s.a->state() == State::Active);
    CHECK(s.b->state() == State::Idle);

    // the only other channel is taken
    Phone second;
    second.number = "222222";
    toDialTone(*s.b, second);
    CHECK(!s.system.moveCall(s.phone.country, s.phone.number));
}

static void testSystemDestroyedDuringOrder()
{
    Calls calls;
    Phone phone;
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    ChannelConfig ca = config(System::Nmt450, ChannelType::Tc, 100);
    NmtChannel& a = system.addChannel(ca, kRate, 1.0);

    connect(system, calls);
    system.addChannel(config(System::Nmt450, ChannelType::Tc, 102), kRate, 1.0);
    toDialTone(a, phone);
    dial(a, phone, "99");
    phoneSends(a, idleFromPhone(), 1);
    for (int i = 0; i < 12 && a.state() != State::Active; i++) {
        moreFrames(a);
    }
    CHECK(system.moveCall(phone.country, phone.number));
    system.setCallHandler(nullptr);
    system.setClearHandler(nullptr);
}

int main()
{
    testSwitch();
    testAskedForIdentity();
    testPhoneDoesNotCome();
    testReleaseDuringSwitch();
    testRefused();
    testWatchdogs();
    testWrongIdentity();
    testIdentityTimerStartsWithOrder();
    testAreaAfterRequest();
    testSupervisoryChoice();
    testOrder900();
    testSupervisoryTone();
    testSystemDestroyedDuringOrder();

    if (g_failures) {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("call switching tests passed\n");
    return 0;
}
