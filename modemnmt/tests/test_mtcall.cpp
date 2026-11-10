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

// Call to the phone: paging on the calling channel, traffic channel assignment, queueing,
// identity, ringing, answer.

#include "phone.h"

// What the exchange hears about the call.
struct Events
{
    int alerting = 0;
    int answered = 0;
    std::vector<modemcellular::Cause> failed;
    int cleared = 0;
    NmtSystem* system = nullptr;

    // the handlers point at this object, and the system may report while it is destroyed
    ~Events()
    {
        if (system)
        {
            system->setCallToPhoneHandlers(nullptr, nullptr, nullptr);
            system->setClearHandler(nullptr);
        }
    }
};

static void listen(NmtSystem& system, Events& events)
{
    events.system = &system;
    system.setCallToPhoneHandlers(
        [&events](const NmtSubscriber&) { events.alerting++; },
        [&events](const NmtSubscriber&) { events.answered++; },
        [&events](const NmtSubscriber&, modemcellular::Cause cause) { events.failed.push_back(cause); });
    system.setClearHandler([&events](const NmtSubscriber&) { events.cleared++; });
}

static ChannelConfig callingConfig(int channel)
{
    return config(System::Nmt450, ChannelType::Cc, channel);
}

// The phone answers the page on the calling channel (call acknowledgement 10a).
static Frame acknowledgement(const NmtChannel& channel, const Phone& phone, MessageType type = MessageType::Frame10a)
{
    Frame f = seizureFrame(channel, phone, type);

    f.msPassword = 0;
    return f;
}

static const modemcellular::NumberType kNational = modemcellular::NumberType::National;
using Phase = NmtTransaction::CcPhase;

static void wait(modemcellular::SampleClock& clock, NmtSystem& system, double seconds)
{
    clock.advance(static_cast<int64_t>(kRate * seconds));
    system.run();
}

// The call up to the ringing phone on a traffic channel.
static void toRinging(NmtSystem& system, NmtChannel& cc, NmtChannel& tc, const Phone& phone)
{
    CHECK(system.startCall(phone.country, phone.number, "5551234", kNational));
    CHECK(pullCalling(cc).mt == MessageType::Frame2a);
    phoneSends(cc, acknowledgement(cc, phone), 1);
    CHECK(pullCalling(cc).mt == MessageType::Frame2b);
    pull(tc);
    phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 3);
}

static void testCallFlow()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    ChannelConfig tcConfig = config(System::Nmt450, ChannelType::Tc, 100);
    tcConfig.supervisory = 2;
    NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
    NmtChannel& tc = system.addChannel(tcConfig, kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    CHECK(system.transactionCount() == 1);
    CHECK(system.callPhase('1', "123456") == Phase::Page);
    CHECK(cc.state() == NmtChannel::State::Idle);
    CHECK(tc.state() == NmtChannel::State::Idle);

    // the page: frame 2a with the phone and the area, then the idle frames of the channel
    Frame f = pullCalling(cc);
    CHECK(f.mt == MessageType::Frame2a);
    CHECK(f.msCountry == digitsToValue("1") && f.msNumber == digitsToValue("123456"));
    CHECK(f.channelNo == encodeChannel(System::Nmt450, 50, 1));
    CHECK(pull(cc).mt == MessageType::Frame1a);
    CHECK(pull(cc).mt == MessageType::Frame1a);                 // one 2a per try

    // the phone answers: the calling channel assigns the traffic channel with one frame 2b
    phoneSends(cc, acknowledgement(cc, phone), 1);
    CHECK(system.callPhase('1', "123456") == Phase::Order);
    f = pullCalling(cc);
    CHECK(f.mt == MessageType::Frame2b);
    CHECK(f.msNumber == digitsToValue("123456"));
    CHECK(f.channelNo == encodeChannel(System::Nmt450, 50, 1));
    CHECK(f.tcNo == encodeTc(System::Nmt450, 100, 1));
    CHECK(cc.state() == NmtChannel::State::Idle);
    CHECK(tc.state() == NmtChannel::State::MtIdent);
    CHECK(pull(cc).mt == MessageType::Frame1a);

    // identity request on the traffic channel
    for (int i = 0; i < 3; i++)
    {
        f = pull(tc);
        CHECK(f.mt == MessageType::Frame3b);
        CHECK(f.msNumber == digitsToValue("123456"));
    }

    // the phone identifies itself: it rings
    CHECK(events.alerting == 0);
    phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 3);
    CHECK(tc.state() == NmtChannel::State::MtRinging);
    CHECK(events.alerting == 1);

    // ringing order L(9); idle frames in between (no caller id configured); repeated every 36 frames
    for (int i = 1; i <= 36; i++)
    {
        f = pull(tc);
        if (i <= 4) {
            CHECK(isLineSignal(f, 9));
        } else {
            CHECK(f.mt == MessageType::Frame6);
        }
    }
    CHECK(isLineSignal(pull(tc), 9));

    // answer: L(14)
    phoneSends(tc, lineSignalFrame(tc, phone, 0xeeeee), 1);
    CHECK(events.answered == 1);
    CHECK(tc.state() == NmtChannel::State::MtComplete);
    for (int i = 0; i < 3; i++) {
        CHECK(pull(tc).mt == MessageType::Frame6);
    }
    FrameBits bits;
    CHECK(!tc.nextFrame(bits));
    CHECK(tc.state() == NmtChannel::State::Active);
    CHECK(tc.dspMode() == DspMode::Audio);

    // the phone clears; the exchange is told
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(events.cleared == 1);
    CHECK(events.failed.empty());
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(tc), 15));
    }
    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);
}

static void testCallerId()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    ChannelConfig tcConfig = config(System::Nmt450, ChannelType::Tc, 100);
    tcConfig.sendCallerId = true;
    NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
    NmtChannel& tc = system.addChannel(tcConfig, kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;

    toRinging(system, cc, tc, phone);
    CHECK(tc.state() == NmtChannel::State::MtRinging);

    // The number of the caller comes before the first ring: 6 frames of frame 8. The number
    // "5551234" (7 digits) takes two frames: index 0 with the type and the first five digits,
    // index 1 with the rest; every pass starts at index 0.
    Frame f;
    for (int i = 0; i < 6; i++)
    {
        f = pull(tc);
        CHECK(f.mt == MessageType::Frame8);
        CHECK(f.seqNumber == ((i & 1) ? 1 : 0));
        if (f.seqNumber == 0)
        {
            CHECK(((f.additionalInfo >> 20) & 0xf) == 1);               // type national
            CHECK(((f.additionalInfo >> 24) & 0xf) == 1);               // one more frame follows
            CHECK(((f.additionalInfo >> 16) & 0xf) == 5);               // first digit
        }
        else {
            CHECK(((f.additionalInfo >> 24) & 0xf) == 3);               // the digits 6 and 7: "34"
            CHECK(((f.additionalInfo >> 20) & 0xf) == 4);
        }
    }

    // then the ringing order for 4 frames, and the number again between the rings from index 0
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(tc), 9));
    }
    f = pull(tc);
    CHECK(f.mt == MessageType::Frame8);
    CHECK(f.seqNumber == 0);
}

static void testPagingFails()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
    system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Events events;
    listen(system, events);

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    CHECK(!system.startCall('1', "123456", "5551234", kNational));      // the phone has a call
    CHECK(pullCalling(cc).mt == MessageType::Frame2a);
    pull(cc);

    // three tries of one second, each starts the page again
    wait(clock, system, 1.05);
    CHECK(system.callPhase('1', "123456") == Phase::Page);
    CHECK(pullCalling(cc).mt == MessageType::Frame2a);
    wait(clock, system, 1.0);
    CHECK(pullCalling(cc).mt == MessageType::Frame2a);
    CHECK(events.failed.empty());
    wait(clock, system, 0.9);
    CHECK(events.failed.empty());
    wait(clock, system, 0.2);

    CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::OutOfOrder);
    CHECK(cc.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 0);
    CHECK(pull(cc).mt == MessageType::Frame1a);
}

static void testTwoCallingChannels()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& first = system.addChannel(callingConfig(50), kRate, 1.0);
    NmtChannel& second = system.addChannel(callingConfig(51), kRate, 1.0);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    system.addChannel(config(System::Nmt450, ChannelType::Tc, 102), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    CHECK(pullCalling(first).mt == MessageType::Frame2a);
    CHECK(pullCalling(second).mt == MessageType::Frame2a);

    // the phone answers on the second one; the first one stops paging
    phoneSends(second, acknowledgement(second, phone), 1);
    CHECK(system.callPhase('1', "123456") == Phase::Order);
    CHECK(isIdleCallingFrame(pull(first)) && isIdleCallingFrame(pull(first)));
    const Frame f = pullCalling(second);
    CHECK(f.mt == MessageType::Frame2b);
    CHECK(f.channelNo == encodeChannel(System::Nmt450, 51, 1));
    CHECK(tc.state() == NmtChannel::State::MtIdent);

    // an answer on the first channel now is not heard
    phoneSends(first, acknowledgement(first, phone), 1);
    CHECK(first.state() == NmtChannel::State::Idle);
    CHECK(tc.state() == NmtChannel::State::MtIdent);

    // the answer on the wrong channel number or from another phone does nothing
    Phone other = phone;
    other.number = "999999";
    CHECK(system.startCall('1', "999999", "5551234", kNational));
    phoneSends(first, acknowledgement(first, phone), 1);
    CHECK(system.callPhase('1', "999999") == Phase::Page);             // paging the other phone
    phoneSends(first, acknowledgement(second, other), 1);               // channel number of the other channel
    CHECK(system.callPhase('1', "999999") == Phase::Page);
}

static void testQueue()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;
    Phone busy = phone;
    busy.number = "777777";

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    pullCalling(cc);

    // the only traffic channel is taken by a roaming update while the phone is paged
    phoneSends(tc, roamingFrame(tc, busy), 40);
    CHECK(tc.state() == NmtChannel::State::RoamingIdent);
    phoneSends(cc, acknowledgement(cc, phone), 1);

    // no traffic channel: queueing information 2f twice, the codes of the ordinary phone, with the
    // idle frames of the calling channel (1a) in between; then the phone is standby
    CHECK(system.callPhase('1', "123456") == Phase::Queue);
    Frame f = pullCalling(cc);
    CHECK(f.mt == MessageType::Frame2f);
    CHECK(f.additionalInfo == 0x3f0);
    CHECK(pull(cc).mt == MessageType::Frame1a);
    CHECK(pull(cc).mt == MessageType::Frame2f);
    for (int i = 0; i < 6; i++) {
        CHECK(pull(cc).mt == MessageType::Frame1a);
    }

    // the traffic channel is free again: the phone is paged again (it is standby, NMT Doc 450-3
    // 5.4.3), and its new answer gets the allocation
    phoneSends(tc, lineSignalFrame(tc, busy, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(tc);
    }
    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(pullCalling(cc).mt == MessageType::Frame2a);
    CHECK(system.callPhase('1', "123456") == Phase::Page);
    phoneSends(cc, acknowledgement(cc, phone), 1);
    f = pullCalling(cc);
    CHECK(f.mt == MessageType::Frame2b);
    CHECK(f.tcNo == encodeTc(System::Nmt450, 100, 1));
    CHECK(tc.state() == NmtChannel::State::MtIdent);
    CHECK(cc.state() == NmtChannel::State::Idle);
}

static void testQueueTimeoutAndPriority()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;
    Phone busy = phone;
    busy.number = "777777";

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    pullCalling(cc);
    phoneSends(tc, roamingFrame(tc, busy), 40);
    // a priority phone (11b) is queued with 2c
    phoneSends(cc, acknowledgement(cc, phone, MessageType::Frame11b), 1);
    const Frame f = pullCalling(cc);
    CHECK(f.mt == MessageType::Frame2c);
    CHECK(f.additionalInfo == 0x3f1);

    // no traffic channel for a minute: the call fails as congestion; the phone is standby on the
    // calling channel, nothing is sent to it
    wait(clock, system, 59.0);
    CHECK(system.callPhase('1', "123456") == Phase::Queue);
    CHECK(events.failed.empty());
    wait(clock, system, 1.5);
    CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::NoChannel);
    CHECK(system.callPhase('1', "123456") == Phase::None);
    CHECK(cc.state() == NmtChannel::State::Idle);
    CHECK(isIdleCallingFrame(pull(cc)) && isIdleCallingFrame(pull(cc)));
}

static void testIdentityAndRingTimeouts()
{
    // no identity: eight requests, then the call fails and the channel is free, without a release
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall('1', "123456", "5551234", kNational));
        pullCalling(cc);
        phoneSends(cc, acknowledgement(cc, phone), 1);
        CHECK(pullCalling(cc).mt == MessageType::Frame2b);
        for (int i = 0; i < 8; i++) {
            CHECK(pull(tc).mt == MessageType::Frame3b);
        }
        CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::TempFail);
        CHECK(tc.state() == NmtChannel::State::Idle);
        CHECK(system.transactionCount() == 0);
    }

    // the phone rings for a minute
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        toRinging(system, cc, tc, phone);
        wait(clock, system, 59.0);
        CHECK(tc.state() == NmtChannel::State::MtRinging);
        wait(clock, system, 1.5);
        CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::Normal);
        CHECK(tc.state() == NmtChannel::State::MtRelease);
        CHECK(isLineSignal(pull(tc), 15));
        phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
        CHECK(tc.state() == NmtChannel::State::Idle);
        CHECK(events.cleared == 0);
    }

    // the phone clears while it rings: the exchange is told
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        toRinging(system, cc, tc, phone);
        phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
        CHECK(events.cleared == 1);
        CHECK(events.failed.empty());
        CHECK(tc.state() == NmtChannel::State::MoRelease);
    }
}

static void testFarEndReleases()
{
    const Phone phone;

    // while paging: the page just stops
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);

        CHECK(system.startCall('1', "123456", "5551234", kNational));
        CHECK(system.releaseCall('1', "123456"));
        CHECK(cc.state() == NmtChannel::State::Idle);
        CHECK(system.transactionCount() == 0);
        wait(clock, system, 5.0);
        CHECK(events.failed.empty());
    }

    // while ringing, and while the phone is on the traffic channel before ringing
    for (int stage = 0; stage < 2; stage++)
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);

        if (stage == 0) {
            toRinging(system, cc, tc, phone);
        } else {
            CHECK(system.startCall('1', "123456", "5551234", kNational));
            pullCalling(cc);
            phoneSends(cc, acknowledgement(cc, phone), 1);
            pullCalling(cc);
            CHECK(tc.state() == NmtChannel::State::MtIdent);
        }
        CHECK(system.releaseCall('1', "123456"));
        CHECK(tc.state() == NmtChannel::State::MtRelease);
        CHECK(isLineSignal(pull(tc), 15));
        CHECK(events.failed.empty() && events.cleared == 0);
        phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
        CHECK(tc.state() == NmtChannel::State::Idle);
        CHECK(system.transactionCount() == 0);
    }
}

static void testCombinedChannel()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& both = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 60), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    CHECK(pullCalling(both).mt == MessageType::Frame2a);
    phoneSends(both, acknowledgement(both, phone), 1);
    const Frame f = pullCalling(both);
    CHECK(f.mt == MessageType::Frame2b);
    CHECK(f.tcNo == encodeTc(System::Nmt450, 60, 1));
    CHECK(both.state() == NmtChannel::State::MtIdent);
    CHECK(pull(both).mt == MessageType::Frame3b);
    phoneSends(both, seizureFrame(both, phone, MessageType::Frame10b), 3);
    CHECK(both.state() == NmtChannel::State::MtRinging);
    CHECK(events.alerting == 1);
}

static void testBatterySavingPage()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    ChannelConfig c = callingConfig(50);
    c.batteryPeriod = 1;            // 24 frames = 3.32 s
    c.batteryGroup = 7;             // phones with a last digit of 9 and 0
    NmtChannel& cc = system.addChannel(c, kRate, 1.0);
    system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Events events;
    listen(system, events);

    // a phone of the group waits for its wake-up window (4 s)
    CHECK(system.startCall('1', "123450", "5551234", kNational));
    CHECK(system.callPhase('1', "123450") == Phase::Deferred);
    CHECK(isIdleCallingFrame(pull(cc)) && isIdleCallingFrame(pull(cc)));
    wait(clock, system, 3.9);
    CHECK(system.callPhase('1', "123450") == Phase::Deferred);
    wait(clock, system, 0.2);
    CHECK(system.callPhase('1', "123450") == Phase::Page);
    CHECK(pullCalling(cc).mt == MessageType::Frame2a);
    CHECK(events.failed.empty());

    // another group is paged at once
    CHECK(system.releaseCall('1', "123450"));
    CHECK(system.startCall('1', "123451", "5551234", kNational));
    CHECK(system.callPhase('1', "123451") == Phase::Page);
}

// The queue codes of NMT-900 are the fictitious channels 1008 and 1009 coded as channel numbers.
static void testQueueCodes900()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    ChannelConfig ccConfig = config(System::Nmt900, ChannelType::Cc, 50);
    NmtChannel& cc = system.addChannel(ccConfig, kRate, 1.0);
    NmtChannel& tc = system.addChannel(config(System::Nmt900, ChannelType::Tc, 100), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;
    Phone busy = phone;
    busy.number = "777777";

    for (int priority = 0; priority < 2; priority++)
    {
        Phone caller = phone;
        caller.number = priority ? "222222" : "111111";

        CHECK(system.startCall('1', caller.number, "5551234", kNational));
        pullCalling(cc);
        phoneSends(tc, roamingFrame(tc, busy), 40);
        phoneSends(cc, acknowledgement(cc, caller, priority ? MessageType::Frame11b : MessageType::Frame10a), 1);
        const Frame f = pullCalling(cc);
        CHECK(f.mt == (priority ? MessageType::Frame2c : MessageType::Frame2f));
        CHECK(f.additionalInfo == (priority ? 0x9f1u : 0x9f0u));
        CHECK(system.callPhase('1', caller.number) == Phase::Queue);
        // the far end gives up: the queued call just ends (the phone is standby)
        CHECK(system.releaseCall('1', caller.number));
        CHECK(system.callPhase('1', caller.number) == Phase::None);
        CHECK(cc.state() == NmtChannel::State::Idle);

        // the roaming update ends: the traffic channel is free for the next call
        phoneSends(tc, lineSignalFrame(tc, busy, 0x11111), 1);
        for (int i = 0; i < 4; i++) {
            pull(tc);
        }
        CHECK(tc.state() == NmtChannel::State::Idle);
    }
}

// The number of the caller in frames of 5 digits first and 7 digits after: 1, 2 and 3 frames.
static void testCallerIdLengths()
{
    struct Case { std::string number; int frames; };
    const Case cases[] = {{"5", 1}, {"555123", 2}, {"1234567890123", 3}};

    for (const Case& c : cases)
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        ChannelConfig tcConfig = config(System::Nmt450, ChannelType::Tc, 100);
        tcConfig.sendCallerId = true;
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(tcConfig, kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall('1', "123456", c.number, kNational));
        pullCalling(cc);
        phoneSends(cc, acknowledgement(cc, phone), 1);
        pullCalling(cc);
        pull(tc);
        phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 3);

        // 6 frames of the number before the first ring; the sequence starts at index 0
        for (int i = 0; i < 6; i++)
        {
            const Frame f = pull(tc);
            CHECK(f.mt == MessageType::Frame8);
            CHECK(f.seqNumber == static_cast<uint8_t>(i % c.frames));
            if (f.seqNumber == 0)
            {
                CHECK(((f.additionalInfo >> 24) & 0xf) == static_cast<uint64_t>(c.frames - 1));
                // the first digit: '1' is 1, '5' is 5
                CHECK(((f.additionalInfo >> 16) & 0xf) == static_cast<uint64_t>(c.number[0] - '0'));
            }
        }
        for (int i = 0; i < 4; i++) {
            CHECK(isLineSignal(pull(tc), 9));
        }
        // between the rings the sequence starts again at index 0
        const Frame f = pull(tc);
        CHECK(f.mt == MessageType::Frame8);
        CHECK(f.seqNumber == 0);
    }

    // an international number gets its '+' (digit 13) from us
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        ChannelConfig tcConfig = config(System::Nmt450, ChannelType::Tc, 100);
        tcConfig.sendCallerId = true;
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(tcConfig, kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall('1', "123456", "4915", modemcellular::NumberType::International));
        pullCalling(cc);
        phoneSends(cc, acknowledgement(cc, phone), 1);
        pullCalling(cc);
        pull(tc);
        phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 3);
        const Frame f = pull(tc);                                       // index 1 of 1 frame: 0
        CHECK(f.mt == MessageType::Frame8);
        CHECK(((f.additionalInfo >> 20) & 0xf) == 2);                   // international
        CHECK(((f.additionalInfo >> 16) & 0xf) == 13);                  // '+'
        CHECK(((f.additionalInfo >> 12) & 0xf) == 4);
    }
}

// Which traffic channel is taken: the first free one, a combined one only if nothing else is.
static void testTrafficChannelChoice()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& cc = system.addChannel(config(System::Nmt900, ChannelType::Cc, 50), kRate, 1.0);
    NmtChannel& both = system.addChannel(config(System::Nmt900, ChannelType::CcTc, 60), kRate, 1.0);
    NmtChannel& access = system.addChannel(config(System::Nmt900, ChannelType::AcTc, 70), kRate, 1.0);
    NmtChannel& traffic = system.addChannel(config(System::Nmt900, ChannelType::Tc, 80), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;
    Phone other = phone;
    other.number = "222222";
    Phone third = phone;
    third.number = "333333";

    // the first AC/TC or TC in the order of the channels, not the combined one
    CHECK(system.startCall('1', "123456", "5551234", kNational));
    phoneSends(cc, acknowledgement(cc, phone), 1);
    CHECK(pullCalling(cc).tcNo == encodeTc(System::Nmt900, 70, 1));
    CHECK(access.state() == NmtChannel::State::MtIdent);
    CHECK(both.state() == NmtChannel::State::Idle);

    // the next one gets the traffic channel
    CHECK(system.startCall('1', "222222", "5551234", kNational));
    phoneSends(cc, acknowledgement(cc, other), 1);
    CHECK(pullCalling(cc).tcNo == encodeTc(System::Nmt900, 80, 1));
    CHECK(traffic.state() == NmtChannel::State::MtIdent);

    // and only then the combined channel, on which the call stays
    CHECK(system.startCall('1', "333333", "5551234", kNational));
    phoneSends(cc, acknowledgement(cc, third), 1);
    CHECK(pullCalling(cc).tcNo == encodeTc(System::Nmt900, 60, 1));
    CHECK(both.state() == NmtChannel::State::MtIdent);
}

// One calling channel is busy with something else: the other one pages.
static void testBusyCallingChannel()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& busy = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 60), kRate, 1.0);
    NmtChannel& idle = system.addChannel(callingConfig(50), kRate, 1.0);
    system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;
    Phone caller = phone;
    caller.number = "999999";

    phoneSends(busy, roamingFrame(busy, caller), 40);
    CHECK(busy.state() == NmtChannel::State::RoamingIdent);

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    CHECK(busy.state() == NmtChannel::State::RoamingIdent);             // untouched
    CHECK(pullCalling(idle).mt == MessageType::Frame2a);

    // the roaming ends, the next try pages on both
    phoneSends(busy, lineSignalFrame(busy, caller, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(busy);
    }
    CHECK(busy.state() == NmtChannel::State::Idle);
    CHECK(isIdleCallingFrame(pull(busy)) && isIdleCallingFrame(pull(busy)));     // not in this try
    wait(clock, system, 1.05);
    CHECK(pullCalling(busy).mt == MessageType::Frame2a);
    CHECK(pullCalling(idle).mt == MessageType::Frame2a);
}

// A phone that has a call or a roaming update is busy, and its seizure is not taken while it is called.
static void testPhoneIsBusy()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;

    // roaming: the phone is busy
    phoneSends(tc, roamingFrame(tc, phone), 40);
    CHECK(!system.startCall('1', "123456", "5551234", kNational));
    CHECK(cc.state() == NmtChannel::State::Idle);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(tc);
    }
    CHECK(system.startCall('1', "123456", "5551234", kNational));

    // paged: its own seizure on the traffic channel is not taken
    phoneSends(tc, roamingFrame(tc, phone), 40);
    CHECK(tc.state() == NmtChannel::State::Idle);
    phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 40);
    CHECK(tc.state() == NmtChannel::State::Idle);
    CHECK(system.transactionCount() == 1);

    // a station without a calling channel cannot call at all
    modemcellular::SampleClock clock2(kRate);
    NmtSystem none(clock2);
    modemcellular::Cause cause = modemcellular::Cause::Normal;
    none.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    CHECK(!none.canCall());
    CHECK(!none.startCall('1', "123456", "5551234", kNational, &cause));
    CHECK(cause == modemcellular::Cause::NoChannel);
    CHECK(none.transactionCount() == 0);
}

// A priority phone seizes the calling channel (11b): it is paged, assigned a traffic channel and
// rung; when it answers it dials like any phone that places a call. The exchange hears nothing
// before the number is dialled.
static void testPriorityPhoneCall()
{
    const Phone phone;
    Phone busy = phone;
    busy.number = "777777";

    // the traffic channel is free
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        Calls calls;
        connect(system, calls);

        phoneSends(cc, seizureFrame(cc, phone, MessageType::Frame11b), 40);
        CHECK(system.callPhase(phone.country, phone.number) == Phase::Page);
        CHECK(system.transactionCount() == 1);
        CHECK(pullCalling(cc).mt == MessageType::Frame2a);

        phoneSends(cc, seizureFrame(cc, phone, MessageType::Frame11b), 1);
        CHECK(system.callPhase(phone.country, phone.number) == Phase::Order);
        CHECK(pullCalling(cc).mt == MessageType::Frame2b);
        CHECK(tc.state() == NmtChannel::State::MtIdent);
        CHECK(pull(tc).mt == MessageType::Frame3b);
        phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 3);
        CHECK(tc.state() == NmtChannel::State::MtRinging);
        CHECK(isLineSignal(pull(tc), 9));
        CHECK(events.alerting == 0);

        // the handset is lifted: proceed to send, the dial tone, the digits
        phoneSends(tc, lineSignalFrame(tc, phone, 0xeeeee), 1);
        CHECK(tc.state() == NmtChannel::State::MoConfirm);
        CHECK(events.answered == 0);
        CHECK(isLineSignal(pull(tc), 3));
        CHECK(isLineSignal(pull(tc), 3));
        FrameBits bits;
        CHECK(!tc.nextFrame(bits));
        CHECK(tc.state() == NmtChannel::State::MoDialling);
        dial(tc, phone, "12");
        phoneSends(tc, idleFromPhone(), 1);
        CHECK(tc.state() == NmtChannel::State::MoComplete);
        CHECK(calls.dialled.size() == 1 && calls.dialled[0] == "12");
        CHECK(events.failed.empty());
    }

    // no answer to the page: the phone is not reported to the exchange
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);

        phoneSends(cc, seizureFrame(cc, phone, MessageType::Frame11b), 40);
        for (int i = 0; i < 4; i++) {
            wait(clock, system, 1.05);
        }
        CHECK(cc.state() == NmtChannel::State::Idle);
        CHECK(system.transactionCount() == 0);
        CHECK(events.failed.empty());
    }

    // the traffic channel is busy: the phone is queued with 2c, and when the channel is free it
    // is paged again before the allocation
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);

        phoneSends(tc, roamingFrame(tc, busy), 40);
        phoneSends(cc, seizureFrame(cc, phone, MessageType::Frame11b), 40);
        CHECK(system.callPhase(phone.country, phone.number) == Phase::Page);
        pullCalling(cc);
        phoneSends(cc, seizureFrame(cc, phone, MessageType::Frame11b), 1);
        CHECK(pullCalling(cc).mt == MessageType::Frame2c);
        CHECK(system.callPhase(phone.country, phone.number) == Phase::Queue);

        phoneSends(tc, lineSignalFrame(tc, busy, 0x11111), 1);
        for (int i = 0; i < 4; i++) {
            pull(tc);
        }
        // the second 2c is still due before the page
        CHECK(pullCalling(cc).mt == MessageType::Frame2c);
        CHECK(pullCalling(cc).mt == MessageType::Frame2a);
        CHECK(system.callPhase(phone.country, phone.number) == Phase::Page);
        phoneSends(cc, seizureFrame(cc, phone, MessageType::Frame11b), 1);
        CHECK(pullCalling(cc).mt == MessageType::Frame2b);
        CHECK(tc.state() == NmtChannel::State::MtIdent);
    }

    // 11b on a traffic channel is no seizure
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);

        phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame11b), 40);
        CHECK(tc.state() == NmtChannel::State::Idle);
        CHECK(system.transactionCount() == 0);
    }
}

// The called phone found a traffic channel by itself (10c): the call is bound to that channel and
// the identity is requested there.
static void testCalledPhoneSeizesTrafficChannel()
{
    const Phone phone;
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Events events;
    listen(system, events);

    // nobody is called: ignored
    phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10c), 40);
    CHECK(tc.state() == NmtChannel::State::Idle);

    CHECK(system.startCall(phone.country, phone.number, "5551234", kNational));
    CHECK(system.callPhase(phone.country, phone.number) == Phase::Page);
    phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10c), 40);
    CHECK(tc.state() == NmtChannel::State::MtIdent);
    CHECK(system.callPhase(phone.country, phone.number) == Phase::None);
    CHECK(pull(tc).mt == MessageType::Frame3b);

    phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10c), 3);
    CHECK(tc.state() == NmtChannel::State::MtRinging);
    CHECK(events.alerting == 1);

    // the paging does not come back
    wait(clock, system, 3.5);
    CHECK(cc.state() == NmtChannel::State::Idle);
    CHECK(events.failed.empty());
}

// A call is rejected at once when the carousel has no room: every calling channel pages as many
// phones as it may, or no traffic channel is free and the queue is full.
static void testRejectedAtOnce()
{
    const Phone phone;
    Phone other = phone;
    other.number = "222222";
    modemcellular::Cause cause = modemcellular::Cause::Normal;

    // the only calling channel pages three phones already
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        system.addChannel(callingConfig(50), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);

        for (const char* number : {"111111", "222222", "333333"}) {
            CHECK(system.startCall('1', number, "5551234", kNational));
        }
        CHECK(system.capacity().freePageSlots == 0);
        CHECK(!system.canCall());
        CHECK(!system.startCall(phone.country, phone.number, "5551234", kNational, &cause));
        CHECK(cause == modemcellular::Cause::NoChannel);
        CHECK(system.transactionCount() == 3);
        for (int i = 0; i < 5; i++) {
            wait(clock, system, 1.05);
        }
        CHECK(events.failed.size() == 3);               // only the calls that were paged
        CHECK(system.canCall());
    }

    // no traffic channel and the queue is full: answered and paged calls count
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);

        system.setMaxQueued(2);
        phoneSends(tc, roamingFrame(tc, other), 40);
        Phone first = phone;
        first.number = "111111";
        CHECK(system.startCall(first.country, first.number, "5551234", kNational));
        phoneSends(cc, acknowledgement(cc, first), 1);
        CHECK(system.callPhase(first.country, first.number) == Phase::Queue);
        CHECK(system.capacity().freeQueueSlots == 1);
        CHECK(system.startCall('1', "333333", "5551234", kNational));       // paged towards the queue
        CHECK(!system.startCall(phone.country, phone.number, "5551234", kNational, &cause));
        CHECK(cause == modemcellular::Cause::NoChannel);
        CHECK(system.capacity().idleTrafficChannels == 0);
    }

    // the only traffic channel is busy: the phone is paged all the same, and queued when it answers
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);

        phoneSends(tc, roamingFrame(tc, other), 40);
        CHECK(system.canCall());
        CHECK(system.startCall(phone.country, phone.number, "5551234", kNational, &cause));
        CHECK(pullCalling(cc).mt == MessageType::Frame2a);
        phoneSends(cc, acknowledgement(cc, phone), 1);
        CHECK(pullCalling(cc).mt == MessageType::Frame2f);
        CHECK(system.callPhase(phone.country, phone.number) == Phase::Queue);
        CHECK(system.transactionCount() == 2);
    }

    // a phone that has a transaction is busy, whatever the channels are
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        system.addChannel(callingConfig(50), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);

        CHECK(system.startCall(phone.country, phone.number, "5551234", kNational, &cause));
        CHECK(!system.startCall(phone.country, phone.number, "5551234", kNational, &cause));
        CHECK(cause == modemcellular::Cause::Busy);
    }
}

// A call that loses its channel or its channel's frames: the exchange hears it once.
static void testCallIsNotLost()
{
    // the calling channel is not rendered after the phone answered: after 2T the phone is standby
    // again (NMT Doc 450-3 5.4.3) and is paged with the next try; with no frame going out the
    // tries run out
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall('1', "123456", "5551234", kNational));
        phoneSends(cc, acknowledgement(cc, phone), 1);
        CHECK(system.callPhase('1', "123456") == Phase::Order);
        wait(clock, system, 2.1);
        CHECK(system.callPhase('1', "123456") == Phase::Order);
        wait(clock, system, 0.2);
        CHECK(system.callPhase('1', "123456") == Phase::Page);
        CHECK(events.failed.empty());
        wait(clock, system, 1.0);
        wait(clock, system, 1.0);
        CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::OutOfOrder);
        CHECK(events.cleared == 0);
        CHECK(cc.state() == NmtChannel::State::Idle);
        CHECK(system.transactionCount() == 0);
    }

    // the same on the traffic channel before the identity
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall('1', "123456", "5551234", kNational));
        phoneSends(cc, acknowledgement(cc, phone), 1);
        pullCalling(cc);
        CHECK(tc.state() == NmtChannel::State::MtIdent);
        wait(clock, system, 5.2);
        CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::TempFail);
        CHECK(tc.state() == NmtChannel::State::Idle);
    }

    // a channel that goes away while it pages
    {
        modemcellular::SampleClock clock(kRate);
        Events events;
        {
            NmtSystem system(clock);
            system.addChannel(callingConfig(50), kRate, 1.0);
            system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
            listen(system, events);
            CHECK(system.startCall('1', "123456", "5551234", kNational));
            events.system = nullptr;                // the system goes away first
        }
        CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::TempFail);
    }

    // a repeated acknowledgement on a combined channel is not the identity
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& both = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 60), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall('1', "123456", "5551234", kNational));
        phoneSends(both, acknowledgement(both, phone), 1);
        pullCalling(both);
        CHECK(both.state() == NmtChannel::State::MtIdent);
        phoneSends(both, seizureFrame(both, phone, MessageType::Frame10b), 1);        // the repeat
        CHECK(both.state() == NmtChannel::State::MtIdent);
        CHECK(events.alerting == 0);
        phoneSends(both, seizureFrame(both, phone, MessageType::Frame10b), 1);        // the reply
        CHECK(both.state() == NmtChannel::State::MtRinging);
        CHECK(events.alerting == 1);
    }

    // the phone clears while it is paged on a calling channel: no failure is reported too
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall('1', "123456", "5551234", kNational));
        phoneSends(cc, lineSignalFrame(cc, phone, 0x11111), 1);
        CHECK(events.cleared == 1);
        CHECK(events.failed.empty());
    }
}

// A phone that clears while it is paged is reported once; the identity can be the first frame the
// modem sees.
static void testBusyCallingChannel2()
{
    // the calling channel is free again after the phone answered: the next call is accepted
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 102), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone first;
        Phone second;
        second.number = "123457";

        // two phones are paged on one calling channel at the same time; the answers are told apart
        // by the number
        CHECK(system.startCall(first.country, first.number, "5551234", kNational));
        CHECK(system.startCall(second.country, second.number, "5551234", kNational));
        CHECK(pullCalling(cc).msNumber == digitsToValue(first.number));
        CHECK(isIdleCallingFrame(pull(cc)));
        CHECK(pull(cc).msNumber == digitsToValue(second.number));
        CHECK(isIdleCallingFrame(pull(cc)));

        phoneSends(cc, acknowledgement(cc, second), 1);
        Frame f = pullCalling(cc);
        CHECK(f.mt == MessageType::Frame2b && f.msNumber == digitsToValue(second.number));
        CHECK(system.callPhase(first.country, first.number) == Phase::Page);
        phoneSends(cc, acknowledgement(cc, first), 1);
        f = pullCalling(cc);
        CHECK(f.mt == MessageType::Frame2b && f.msNumber == digitsToValue(first.number));
        CHECK(f.tcNo != 0);
        CHECK(cc.state() == NmtChannel::State::Idle);
        CHECK(events.failed.empty());
    }

    // the phone clears while it is paged: one report, the page does not come back
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall(phone.country, phone.number, "5551234", kNational));
        phoneSends(cc, lineSignalFrame(cc, phone, 0x11111), 1);
        wait(clock, system, 3.5);
        CHECK(events.cleared == 1);
        CHECK(events.failed.empty());
        CHECK(cc.state() == NmtChannel::State::MoRelease);
        for (int i = 0; i < 4; i++) {
            pull(cc);
        }
        CHECK(cc.state() == NmtChannel::State::Idle);
        CHECK(system.transactionCount() == 0);
    }

    // the identity is the first frame the modem has seen on a traffic channel
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall(phone.country, phone.number, "5551234", kNational));
        phoneSends(cc, acknowledgement(cc, phone), 1);
        pullCalling(cc);
        phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 0);
        CHECK(tc.state() == NmtChannel::State::MtRinging);
    }
}

// The call stays on the calling channel when it is a free combined channel; no number to show.
static void testChoicesAndCleanup()
{
    // two combined channels: the one that was paged takes the call
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& a = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 60), kRate, 1.0);
        NmtChannel& b = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 62), kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall(phone.country, phone.number, "5551234", kNational));
        phoneSends(a, acknowledgement(a, phone), 1);
        CHECK(pullCalling(a).mt == MessageType::Frame2b);
        CHECK(a.state() == NmtChannel::State::MtIdent);
        CHECK(b.state() == NmtChannel::State::Idle);
    }

    // no number of the caller: type 3
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        ChannelConfig tcConfig = config(System::Nmt450, ChannelType::Tc, 100);
        tcConfig.sendCallerId = true;
        NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
        NmtChannel& tc = system.addChannel(tcConfig, kRate, 1.0);
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall(phone.country, phone.number, "", kNational));
        pullCalling(cc);
        phoneSends(cc, acknowledgement(cc, phone), 1);
        pullCalling(cc);
        pull(tc);
        phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 3);
        const Frame f = pull(tc);
        CHECK(f.mt == MessageType::Frame8);
        CHECK(((f.additionalInfo >> 20) & 0xf) == 3);
    }

    // the station goes away with an answered call: it ended, it did not fail
    {
        modemcellular::SampleClock clock(kRate);
        Events events;
        {
            NmtSystem system(clock);
            NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
            NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
            listen(system, events);
            const Phone phone;

            toRinging(system, cc, tc, phone);
            phoneSends(tc, lineSignalFrame(tc, phone, 0xeeeee), 1);
            CHECK(tc.state() == NmtChannel::State::MtComplete);
            events.system = nullptr;                // the handlers stay: the system reports while it goes
        }
        CHECK(events.cleared == 1);
        CHECK(events.failed.empty());
    }
}

// A combined channel that is taken as a traffic channel lets go of the calls it carried as a
// calling channel: they go on with another calling channel.
static void testCombinedChannelTaken()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& cc = system.addChannel(callingConfig(50), kRate, 1.0);
    NmtChannel& both = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 60), kRate, 1.0);
    Events events;
    listen(system, events);
    Calls calls;
    connect(system, calls);
    const Phone phone;
    Phone caller = phone;
    caller.number = "777777";

    CHECK(system.startCall(phone.country, phone.number, "5551234", kNational));
    CHECK(system.capacity().freePageSlots == 2 * 3 - 2);
    CHECK(system.capacity().idleTrafficChannels == 1);

    // the phone answers on the combined channel; before the order goes out another phone seizes it
    phoneSends(both, acknowledgement(both, phone), 1);
    CHECK(system.callPhase(phone.country, phone.number) == Phase::Order);
    phoneSends(both, seizureFrame(both, caller, MessageType::Frame10b), 40);
    CHECK(both.state() == NmtChannel::State::MoIdent);

    // the call goes on with the other calling channel; no traffic channel is left: queued
    const Frame f = pullCalling(cc);
    CHECK(f.mt == MessageType::Frame2f);
    CHECK(f.msNumber == digitsToValue(phone.number));
    CHECK(system.callPhase(phone.country, phone.number) == Phase::Queue);
    CHECK(system.capacity().idleTrafficChannels == 0);
    CHECK(events.failed.empty());
}

int main()
{
    testCombinedChannelTaken();
    testBusyCallingChannel2();
    testChoicesAndCleanup();
    testQueueCodes900();
    testCallerIdLengths();
    testTrafficChannelChoice();
    testBusyCallingChannel();
    testPhoneIsBusy();
    testRejectedAtOnce();
    testPriorityPhoneCall();
    testCalledPhoneSeizesTrafficChannel();
    testCallIsNotLost();
    testCallFlow();
    testCallerId();
    testPagingFails();
    testTwoCallingChannels();
    testQueue();
    testQueueTimeoutAndPriority();
    testIdentityAndRingTimeouts();
    testFarEndReleases();
    testCombinedChannel();
    testBatterySavingPage();

    if (g_failures == 0) {
        std::printf("all call-to-phone tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
