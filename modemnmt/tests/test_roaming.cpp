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

#include "phone.h"

static void testRoamingUpdate()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    std::vector<NmtSubscriber> registered;
    system.setRegistrationListener([&](const NmtSubscriber& s) { registered.push_back(s); });
    const Phone phone;

    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(pull(tc).mt == MessageType::Frame4);

    // the seizure; the idle channel does not count the frames before it
    phoneSends(tc, roamingFrame(tc, phone), 40);
    CHECK(tc.state() == NmtChannel::State::RoamingIdent);
    CHECK(system.transactionCount() == 1);

    // identity request: frame 3b with the subscriber
    for (int i = 0; i < 3; i++)
    {
        const Frame f = pull(tc);
        CHECK(f.mt == MessageType::Frame3b);
        CHECK(f.msCountry == digitsToValue("1"));
        CHECK(f.msNumber == digitsToValue("123456"));
        CHECK(f.channelNo == encodeChannel(System::Nmt450, 100, 1));
    }

    // the repeated seizure frame one period later is still the seizure
    phoneSends(tc, roamingFrame(tc, phone), 1);
    CHECK(tc.state() == NmtChannel::State::RoamingIdent);
    CHECK(registered.empty());

    // two periods after the seizure: the identity
    phoneSends(tc, roamingFrame(tc, phone), 1);
    CHECK(tc.state() == NmtChannel::State::RoamingConfirm);
    CHECK(registered.size() == 1);
    CHECK(registered[0].country == '1' && registered[0].number == "123456" && registered[0].password == "789");
    CHECK(system.isRegistered('1', "123456"));

    // L(3) twice (scheme D: T''' = 2 frames), the second one starts the release
    for (int i = 0; i < 2; i++)
    {
        CHECK(isLineSignal(pull(tc), 3));
        CHECK(tc.state() == (i < 1 ? NmtChannel::State::RoamingConfirm : NmtChannel::State::MtRelease));
    }
    for (int i = 0; i < 5; i++) {
        CHECK(isLineSignal(pull(tc), 15));
    }

    // the release guard of the phone ends it
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);
    CHECK(pull(tc).mt == MessageType::Frame4);
    CHECK(system.registeredCount() == 1);
}

static void testReleaseGuardTimeout()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    const Phone phone;

    seize(tc, phone);
    CHECK(tc.state() == NmtChannel::State::RoamingConfirm);
    for (int i = 0; i < 2; i++) {
        pull(tc);
    }
    CHECK(tc.state() == NmtChannel::State::MtRelease);

    clock.advance(static_cast<int64_t>(kRate * 1.9));
    system.run();
    CHECK(tc.state() == NmtChannel::State::MtRelease);
    CHECK(isLineSignal(pull(tc), 15));

    clock.advance(static_cast<int64_t>(kRate * 0.2));
    system.run();
    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);
    CHECK(pull(tc).mt == MessageType::Frame4);

    // a late guard changes nothing
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == NmtChannel::State::Idle);

    // a guard of another phone does not end the release
    seize(tc, phone);
    for (int i = 0; i < 2; i++) {
        pull(tc);
    }
    Phone other = phone;
    other.number = "123457";
    phoneSends(tc, lineSignalFrame(tc, other, 0x11111), 1);
    CHECK(tc.state() == NmtChannel::State::MtRelease);
    // guard with other signal, and one that is not a majority
    // (a first character of 2 would make the frame a 13b, so the values start with 3 or 1)
    phoneSends(tc, lineSignalFrame(tc, phone, 0x33333), 1);
    CHECK(tc.state() == NmtChannel::State::MtRelease);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x14325), 1);
    CHECK(tc.state() == NmtChannel::State::MtRelease);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11223), 1);       // two of five
    CHECK(tc.state() == NmtChannel::State::MtRelease);
    // three of five are enough
    phoneSends(tc, lineSignalFrame(tc, phone, 0x41151), 1);
    CHECK(tc.state() == NmtChannel::State::Idle);
}

static void testIdentityTimeout()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    const Phone phone;

    phoneSends(tc, roamingFrame(tc, phone), 40);
    // 8 identity requests and then the release
    for (int i = 0; i < 8; i++) {
        CHECK(pull(tc).mt == MessageType::Frame3b);
    }
    CHECK(tc.state() == NmtChannel::State::MtRelease);
    CHECK(isLineSignal(pull(tc), 15));
    CHECK(system.registeredCount() == 0);

    clock.advance(static_cast<int64_t>(kRate * 2.1));
    system.run();
    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);
}

static void testPhoneClears()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    const Phone phone;

    // nothing to clear on an idle channel
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == NmtChannel::State::Idle);

    phoneSends(tc, roamingFrame(tc, phone), 40);
    CHECK(tc.state() == NmtChannel::State::RoamingIdent);

    // another phone, or no majority, or another signal: not accepted
    Phone other = phone;
    other.country = '2';
    phoneSends(tc, lineSignalFrame(tc, other, 0x11111), 1);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x14325), 1);      // no majority
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11223), 1);      // two of five
    phoneSends(tc, lineSignalFrame(tc, phone, 0x33331), 1);      // L(3) wins
    CHECK(tc.state() == NmtChannel::State::RoamingIdent);

    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == NmtChannel::State::MoRelease);

    // L(15) four times, then the channel is idle again
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(tc), 15));
    }
    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);
    CHECK(pull(tc).mt == MessageType::Frame4);
    CHECK(system.registeredCount() == 0);
}

static void testSeizureFilter()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    const Phone phone;

    // calling channel: no seizure
    NmtChannel& cc = system.addChannel(config(System::Nmt450, ChannelType::Cc, 100), kRate, 1.0);
    phoneSends(cc, roamingFrame(cc, phone), 40);
    CHECK(cc.state() == NmtChannel::State::Idle);

    // test channel: no seizure
    NmtChannel& test = system.addChannel(config(System::Nmt450, ChannelType::Test, 101), kRate, 1.0);
    phoneSends(test, roamingFrame(test, phone), 40);
    CHECK(test.state() == NmtChannel::State::Idle);

    // another channel number, also with the upper bit of an interleaved channel dropped
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 102), kRate, 1.0);
    Frame f = roamingFrame(tc, phone);
    f.channelNo = static_cast<uint16_t>(encodeChannel(System::Nmt450, 103, 1));
    phoneSends(tc, f, 40);
    CHECK(tc.state() == NmtChannel::State::Idle);

    // area number and traffic area: configured area 2, traffic area 0x11
    ChannelConfig area = config(System::Nmt450, ChannelType::Tc, 104);
    area.areaNo = 2;
    NmtChannel& ac = system.addChannel(area, kRate, 1.0);
    f = roamingFrame(ac, phone);
    f.areaInfo = (3 << 2) | 1;              // area 3
    f.trafficArea = 0x01;
    phoneSends(ac, f, 40);
    CHECK(ac.state() == NmtChannel::State::Idle);
    f.areaInfo = (2 << 2) | 2;              // area 2, wrong traffic area
    phoneSends(ac, f, 40);
    CHECK(ac.state() == NmtChannel::State::Idle);
    f.areaInfo = (2 << 2) | 1;              // area 2, traffic area 0x11
    phoneSends(ac, f, 40);
    CHECK(ac.state() == NmtChannel::State::RoamingIdent);
    CHECK(pull(ac).additionalInfo == 0x3f4);

    // a phone without area information is accepted
    NmtChannel& open = system.addChannel(area, kRate, 1.0);
    Phone second;
    second.number = "654321";
    phoneSends(open, roamingFrame(open, second), 40);
    CHECK(open.state() == NmtChannel::State::RoamingIdent);

    // no area number is configured and the phone sends area 0 with a matching traffic area
    NmtChannel& plain = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 105), kRate, 1.0);
    Phone third;
    third.number = "111111";
    f = roamingFrame(plain, third);
    f.areaInfo = 1;
    f.trafficArea = 0x01;
    phoneSends(plain, f, 40);
    CHECK(plain.state() == NmtChannel::State::RoamingIdent);
}

static void testOneTransactionPerSubscriber()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& a = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    NmtChannel& b = system.addChannel(config(System::Nmt450, ChannelType::Tc, 101), kRate, 1.0);
    const Phone phone;
    Phone other = phone;
    other.number = "999999";

    phoneSends(a, roamingFrame(a, phone), 40);
    phoneSends(b, roamingFrame(b, phone), 40);
    CHECK(a.state() == NmtChannel::State::RoamingIdent);
    CHECK(b.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 1);

    phoneSends(b, roamingFrame(b, other), 40);
    CHECK(b.state() == NmtChannel::State::RoamingIdent);
    CHECK(system.transactionCount() == 2);

    // ending one does not touch the other
    phoneSends(a, lineSignalFrame(a, phone, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(a);
    }
    CHECK(a.state() == NmtChannel::State::Idle);
    CHECK(b.state() == NmtChannel::State::RoamingIdent);
    CHECK(system.transactionCount() == 1);
    CHECK(pull(b).msNumber == digitsToValue("999999"));

    // and the freed subscriber can seize again
    phoneSends(a, roamingFrame(a, phone), 40);
    CHECK(a.state() == NmtChannel::State::RoamingIdent);
}

static void testRoamingAgain()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt900, ChannelType::Tc, 500), kRate, 1.0);
    int events = 0;
    system.setRegistrationListener([&](const NmtSubscriber&) { events++; });
    Phone phone;

    for (int round = 0; round < 2; round++)
    {
        phone.password = round ? "321" : "123";
        seize(tc, phone);
        CHECK(tc.state() == NmtChannel::State::RoamingConfirm);
        for (int i = 0; i < 2; i++) {
            pull(tc);
        }
        phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
        CHECK(tc.state() == NmtChannel::State::Idle);
    }
    CHECK(events == 2);
    CHECK(system.registeredCount() == 1);
    CHECK(system.registeredSubscriber('1', "123456")->password == "321");       // the last update wins

    // NMT-900 identity request carries the area number as channel 1011..1014
    ChannelConfig area = config(System::Nmt900, ChannelType::AcTc, 501);
    area.areaNo = 3;
    NmtChannel& ac = system.addChannel(area, kRate, 1.0);
    Phone other = phone;
    other.number = "000001";
    phoneSends(ac, roamingFrame(ac, other), 40);
    CHECK(ac.state() == NmtChannel::State::RoamingIdent);
    const Frame ident = pull(ac);
    CHECK(ident.additionalInfo == 0x9f5);
}

static void testLoopbackAndDetached()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    ChannelConfig c = config(System::Nmt450, ChannelType::Tc, 100);
    c.loopback = true;
    NmtChannel& loop = system.addChannel(c, kRate, 1.0);
    const Phone phone;

    phoneSends(loop, roamingFrame(loop, phone), 40);
    CHECK(loop.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);

    // a channel without a base station only listens
    NmtChannel alone(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    phoneSends(alone, roamingFrame(alone, phone), 40);
    CHECK(alone.state() == NmtChannel::State::Idle);
}

// The release timer of an earlier transaction must not end a later one.
static void testStaleTimer()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    const Phone phone;

    seize(tc, phone);
    pull(tc);
    pull(tc);
    CHECK(tc.state() == NmtChannel::State::MtRelease);          // timer due at 2 s

    clock.advance(static_cast<int64_t>(kRate * 0.5));
    system.run();
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == NmtChannel::State::Idle);

    clock.advance(static_cast<int64_t>(kRate * 0.5));           // 1 s
    seize(tc, phone);
    CHECK(tc.state() == NmtChannel::State::RoamingConfirm);

    clock.advance(static_cast<int64_t>(kRate * 1.1));           // 2.1 s, past the first release timer
    system.run();
    CHECK(tc.state() == NmtChannel::State::RoamingConfirm);
    CHECK(system.transactionCount() == 1);
}

// A channel that stops sending does not keep the subscriber for ever.
static void testWatchdog()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& a = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    NmtChannel& b = system.addChannel(config(System::Nmt450, ChannelType::Tc, 101), kRate, 1.0);
    const Phone phone;
    auto wait = [&](double seconds) {
        clock.advance(static_cast<int64_t>(kRate * seconds));
        system.run();
    };

    // RoamingIdent: nothing is pulled, the transaction ends 5 s after the seizure
    phoneSends(a, roamingFrame(a, phone), 40);
    CHECK(a.state() == NmtChannel::State::RoamingIdent);
    phoneSends(b, roamingFrame(b, phone), 40);
    CHECK(b.state() == NmtChannel::State::Idle);                // refused while a has it
    wait(4.9);
    CHECK(a.state() == NmtChannel::State::RoamingIdent);
    wait(0.2);
    CHECK(a.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);

    // RoamingConfirm: the time starts again with the identity (3 s after the seizure)
    phoneSends(a, roamingFrame(a, phone), 40);
    wait(3.0);
    phoneSends(a, roamingFrame(a, phone), 1);
    phoneSends(a, roamingFrame(a, phone), 1);
    CHECK(a.state() == NmtChannel::State::RoamingConfirm);
    wait(4.9);
    CHECK(a.state() == NmtChannel::State::RoamingConfirm);      // a timer from the seizure would be over
    wait(0.2);
    CHECK(a.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);

    // MoRelease: the time starts again with the clearing of the phone
    phoneSends(b, roamingFrame(b, phone), 40);
    wait(3.0);
    phoneSends(b, lineSignalFrame(b, phone, 0x11111), 1);
    CHECK(b.state() == NmtChannel::State::MoRelease);
    wait(4.9);
    CHECK(b.state() == NmtChannel::State::MoRelease);
    wait(0.2);
    CHECK(b.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);

    // and the subscriber is not locked out
    phoneSends(a, roamingFrame(a, phone), 40);
    CHECK(a.state() == NmtChannel::State::RoamingIdent);
}

static void testClearingInOtherStates()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    const Phone phone;

    // clear in the confirmation: the registration stays
    seize(tc, phone);
    CHECK(tc.state() == NmtChannel::State::RoamingConfirm);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == NmtChannel::State::MoRelease);
    CHECK(system.registeredCount() == 1);

    // a second clearing does not extend the acknowledgement
    pull(tc);
    pull(tc);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    pull(tc);
    CHECK(tc.state() == NmtChannel::State::MoRelease);
    pull(tc);
    CHECK(tc.state() == NmtChannel::State::Idle);

    // a seizure of another phone while the channel is busy is not taken
    Phone intruder = phone;
    intruder.number = "777777";
    seize(tc, phone);
    phoneSends(tc, roamingFrame(tc, intruder), 5);
    CHECK(tc.state() == NmtChannel::State::RoamingConfirm);
    CHECK(system.transactionCount() == 1);
    pull(tc);
    pull(tc);
    CHECK(tc.state() == NmtChannel::State::MtRelease);
    phoneSends(tc, roamingFrame(tc, intruder), 5);
    CHECK(tc.state() == NmtChannel::State::MtRelease);
    CHECK(system.transactionCount() == 1);
    CHECK(!system.isRegistered('1', "777777"));

    // three of five characters are enough for a clearing, wherever they are
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == NmtChannel::State::Idle);
    seize(tc, phone);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11145), 1);
    CHECK(tc.state() == NmtChannel::State::MoRelease);
}

static void testDamagedFrames()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);

    // digit values that are no digit of a subscriber number (0 and 11 .. 15)
    Phone damaged;
    Frame f = roamingFrame(tc, damaged);
    f.msNumber = 0xb23456;
    phoneSends(tc, f, 40);
    CHECK(tc.state() == NmtChannel::State::Idle);
    f.msNumber = 0x023456;
    phoneSends(tc, f, 40);
    CHECK(tc.state() == NmtChannel::State::Idle);
    f = roamingFrame(tc, damaged);
    f.msCountry = 12;
    phoneSends(tc, f, 40);
    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);

    // the number 000000 and the country 0 are valid
    NmtChannel& other = system.addChannel(config(System::Nmt450, ChannelType::Tc, 101), kRate, 1.0);
    Phone zeros;
    zeros.country = '0';
    zeros.number = "000000";
    zeros.password = "000";
    phoneSends(other, roamingFrame(other, zeros), 40);
    CHECK(other.state() == NmtChannel::State::RoamingIdent);
    const Frame ident = pull(other);
    CHECK(ident.msNumber == digitsToValue("000000"));
    CHECK(ident.msCountry == digitsToValue("0"));
}

static void testChannelGone()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    const Phone phone;

    {
        // a channel that is not owned by the system, as an integration layer could create it
        NmtChannel lone(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        lone.attach(system);
        lone.attach(system);                // second attach changes nothing
        phoneSends(lone, roamingFrame(lone, phone), 40);
        CHECK(lone.state() == NmtChannel::State::RoamingIdent);
        CHECK(system.transactionCount() == 1);
    }
    CHECK(system.transactionCount() == 0);

    // a system that goes away with channels in a transaction and timers pending
    {
        NmtSystem inner(clock);
        NmtChannel& a = inner.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        NmtChannel& b = inner.addChannel(config(System::Nmt450, ChannelType::Tc, 101), kRate, 1.0);
        seize(a, phone);
        pull(a);
        pull(a);
        Phone other = phone;
        other.number = "222222";
        phoneSends(b, roamingFrame(b, other), 40);
        CHECK(inner.transactionCount() == 2);
    }
}

static void testConfigLimits()
{
    auto valid = [](System system, ChannelType type, int channel, double rate) {
        return NmtChannel(config(system, type, channel), rate, 1.0).valid();
    };

    CHECK(valid(System::Nmt450, ChannelType::Tc, 1, kRate));
    CHECK(valid(System::Nmt450, ChannelType::Tc, 400, kRate));
    CHECK(!valid(System::Nmt450, ChannelType::Tc, 401, kRate));
    CHECK(!valid(System::Nmt450, ChannelType::Tc, 0, kRate));
    CHECK(valid(System::Nmt900, ChannelType::Tc, 1000, kRate));
    CHECK(!valid(System::Nmt900, ChannelType::Tc, 1007, kRate));
    CHECK(!valid(System::Nmt900, ChannelType::Tc, 1008, kRate));
    CHECK(!valid(System::Nmt900, ChannelType::Tc, 1014, kRate));
    CHECK(!valid(System::Nmt900, ChannelType::Tc, 1015, kRate));
    CHECK(!valid(System::Nmt900, ChannelType::Tc, 2048, kRate));
    CHECK(!valid(System::Nmt450, ChannelType::Tc, 100, 0.0));
    CHECK(!valid(System::Nmt450, ChannelType::Tc, 100, 1000.0));
    CHECK(!NmtChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 0.0).valid());

    CHECK(valid(System::Nmt900, ChannelType::Tc, 1000, kRate));
    CHECK(!valid(System::Nmt900, ChannelType::Tc, 1001, kRate));
    CHECK(!valid(System::Nmt900, ChannelType::Tc, 1024, kRate));
    CHECK(valid(System::Nmt900, ChannelType::Tc, 1025, kRate));
    CHECK(valid(System::Nmt900, ChannelType::Tc, 2024, kRate));
    CHECK(!valid(System::Nmt900, ChannelType::Tc, 2025, kRate));
    CHECK(!valid(System::Nmt450, ChannelType::Cca, 100, kRate));
    CHECK(!valid(System::Nmt450, ChannelType::AcTc, 100, kRate));
    CHECK(valid(System::Nmt900, ChannelType::AcTc, 100, kRate));
    CHECK(!valid(System::Nmt450, ChannelType::Tc, 100, 1e12));

    // a band is both limits or none, in order, up to channel 999
    ChannelConfig band = config(System::Nmt900, ChannelType::Cc, 100);
    CHECK(NmtChannel(band, kRate, 1.0).valid());
    band.ccBandStart = 4;
    CHECK(!NmtChannel(band, kRate, 1.0).valid());
    band.ccBandEnd = 124;
    CHECK(NmtChannel(band, kRate, 1.0).valid());
    band.ccBandEnd = 4;
    CHECK(!NmtChannel(band, kRate, 1.0).valid());
    band.ccBandEnd = 1000;
    CHECK(!NmtChannel(band, kRate, 1.0).valid());
    band.ccBandEnd = 999;
    CHECK(NmtChannel(band, kRate, 1.0).valid());
    band.tcBandEnd = 12;
    CHECK(!NmtChannel(band, kRate, 1.0).valid());
    ChannelConfig power = config(System::Nmt450, ChannelType::Tc, 100);
    power.msPower = 4;
    CHECK(!NmtChannel(power, kRate, 1.0).valid());
    power.msPower = 3;
    power.areaNo = 5;
    CHECK(!NmtChannel(power, kRate, 1.0).valid());
}

static void testDetachedListener()
{
    NmtChannel alone(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    const Phone phone;
    int heard = 0;

    alone.setFrameListener([&](const NmtChannel::ReceivedFrame&) { heard++; });
    phoneSends(alone, roamingFrame(alone, phone), 40);
    CHECK(heard == 1);
    CHECK(alone.state() == NmtChannel::State::Idle);
    CHECK(alone.framesSent() == 0);

    // an empty render consumes no frame
    float none = 0.0f;
    CHECK(alone.render(&none, 0, false) == 0);
    CHECK(alone.framesSent() == 0);
}

// A phone that works on the audio of the channel.
class AudioPhone
{
public:
    AudioPhone(const NmtChannel& channel, double rate) :
        m_modem(rate, 1.0),
        m_channel(channel)
    {
        m_modem.setFrameSource([this](FrameBits& bits) { return next(bits); });
        m_modem.setFrameSink([this](const RxFrame& rx) { heard(rx); });
    }

    NmtModem& modem() { return m_modem; }
    bool done() const { return m_done; }
    bool sawConfirm() const { return m_confirm; }

private:
    bool next(FrameBits& bits)
    {
        if (m_done) {
            return false;
        }
        const Phone phone;
        bits = encodeFrame(m_guard ? lineSignalFrame(m_channel, phone, 0x11111) : roamingFrame(m_channel, phone));
        return true;
    }

    void heard(const RxFrame& rx)
    {
        const Frame f = decodeFrame(rx.bits, Direction::MtxToMs, false);

        if (isLineSignal(f, 3)) {
            m_confirm = true;
        }
        if (isLineSignal(f, 15)) {
            m_guard = true;
        }
        if (m_guard && f.mt == MessageType::Frame4) {
            m_done = true;
        }
    }

    NmtModem m_modem;
    const NmtChannel& m_channel;
    bool m_guard = false;
    bool m_confirm = false;
    bool m_done = false;
};

// Base station and phone connected through audio, no frame-level shortcuts.
static void testOverTheAir()
{
    for (System sys : {System::Nmt450, System::Nmt900})
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& tc = system.addChannel(config(sys, ChannelType::Tc, 100), kRate, 1.0);
        AudioPhone phone(tc, kRate);
        int events = 0;
        system.setRegistrationListener([&](const NmtSubscriber& s) {
            events++;
            CHECK(s.country == '1' && s.number == "123456" && s.password == "789");
        });

        const int block = 480;
        std::vector<float> toPhone(block), toBase(block);
        bool finished = false;

        for (int i = 0; i < 1500 && !finished; i++)       // up to 15 s
        {
            tc.render(toPhone.data(), block, false);
            phone.modem().receive(toPhone.data(), block);
            phone.modem().render(toBase.data(), block, false);
            tc.receive(toBase.data(), block);
            clock.advance(block);
            system.run();
            finished = phone.done() && tc.state() == NmtChannel::State::Idle;
        }

        CHECK(finished);
        CHECK(phone.sawConfirm());
        CHECK(events == 1);
        CHECK(system.isRegistered('1', "123456"));
        CHECK(system.transactionCount() == 0);
    }
}

int main()
{
    testRoamingUpdate();
    testReleaseGuardTimeout();
    testIdentityTimeout();
    testPhoneClears();
    testSeizureFilter();
    testOneTransactionPerSubscriber();
    testRoamingAgain();
    testLoopbackAndDetached();
    testStaleTimer();
    testWatchdog();
    testClearingInOtherStates();
    testDamagedFrames();
    testChannelGone();
    testConfigLimits();
    testDetachedListener();
    testOverTheAir();

    if (g_failures == 0) {
        std::printf("all roaming tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
