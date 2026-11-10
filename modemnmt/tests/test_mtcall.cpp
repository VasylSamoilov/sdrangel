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

static void wait(modemcellular::SampleClock& clock, NmtSystem& system, double seconds)
{
    clock.advance(static_cast<int64_t>(kRate * seconds));
    system.run();
}

// The call up to the ringing phone on a traffic channel.
static void toRinging(NmtSystem& system, NmtChannel& cc, NmtChannel& tc, const Phone& phone)
{
    CHECK(system.startCall(phone.country, phone.number, "5551234", kNational));
    pull(cc);
    phoneSends(cc, acknowledgement(cc, phone), 1);
    pull(cc);
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
    CHECK(cc.state() == NmtChannel::State::MtPaging);
    CHECK(tc.state() == NmtChannel::State::Idle);

    // the page: frame 2a with the phone and the area, then the idle frames of the channel
    Frame f = pull(cc);
    CHECK(f.mt == MessageType::Frame2a);
    CHECK(f.msCountry == digitsToValue("1") && f.msNumber == digitsToValue("123456"));
    CHECK(f.channelNo == encodeChannel(System::Nmt450, 50, 1));
    CHECK(pull(cc).mt == MessageType::Frame1a);

    // the phone answers: the calling channel assigns the traffic channel with one frame 2b
    phoneSends(cc, acknowledgement(cc, phone), 1);
    CHECK(cc.state() == NmtChannel::State::MtChannel);
    f = pull(cc);
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
    // index 1 with the rest; the reference starts the cycle at index 1.
    Frame f;
    for (int i = 0; i < 6; i++)
    {
        f = pull(tc);
        CHECK(f.mt == MessageType::Frame8);
        CHECK(f.seqNumber == ((i & 1) ? 0 : 1));
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

    // then the ringing order for 4 frames, and the number again between the rings
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(tc), 9));
    }
    CHECK(pull(tc).mt == MessageType::Frame8);
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
    CHECK(pull(cc).mt == MessageType::Frame2a);
    pull(cc);

    // three tries of one second, each starts the page again
    wait(clock, system, 1.05);
    CHECK(cc.state() == NmtChannel::State::MtPaging);
    CHECK(pull(cc).mt == MessageType::Frame2a);
    wait(clock, system, 1.0);
    CHECK(pull(cc).mt == MessageType::Frame2a);
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
    Events events;
    listen(system, events);
    const Phone phone;

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    CHECK(first.state() == NmtChannel::State::MtPaging);
    CHECK(second.state() == NmtChannel::State::MtPaging);

    // the phone answers on the second one; the first one stops paging
    phoneSends(second, acknowledgement(second, phone), 1);
    CHECK(first.state() == NmtChannel::State::Idle);
    CHECK(second.state() == NmtChannel::State::MtChannel);
    const Frame f = pull(second);
    CHECK(f.mt == MessageType::Frame2b);
    CHECK(f.channelNo == encodeChannel(System::Nmt450, 51, 1));
    CHECK(tc.state() == NmtChannel::State::MtIdent);

    // an answer on the first channel now is not heard
    phoneSends(first, acknowledgement(first, phone), 1);
    CHECK(first.state() == NmtChannel::State::Idle);

    // the answer on the wrong channel number or from another phone does nothing
    Phone other = phone;
    other.number = "999999";
    CHECK(system.startCall('1', "999999", "5551234", kNational));
    phoneSends(first, acknowledgement(first, phone), 1);
    CHECK(first.state() == NmtChannel::State::MtPaging);                // paging the other phone
    phoneSends(first, acknowledgement(second, other), 1);               // channel number of the other channel
    CHECK(first.state() == NmtChannel::State::MtPaging);
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

    // the only traffic channel is taken by a roaming update
    phoneSends(tc, roamingFrame(tc, busy), 40);
    CHECK(tc.state() == NmtChannel::State::RoamingIdent);

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    pull(cc);
    phoneSends(cc, acknowledgement(cc, phone), 1);

    // no traffic channel: queueing information 2f, the codes of the ordinary phone
    Frame f = pull(cc);
    CHECK(f.mt == MessageType::Frame2f);
    CHECK(f.additionalInfo == 0x3f0);
    CHECK(cc.state() == NmtChannel::State::MtQueue);
    CHECK(pull(cc).mt == MessageType::Frame2f);

    // the traffic channel is free again: the next frame is the allocation
    phoneSends(tc, lineSignalFrame(tc, busy, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(tc);
    }
    CHECK(tc.state() == NmtChannel::State::Idle);
    f = pull(cc);
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

    phoneSends(tc, roamingFrame(tc, busy), 40);
    CHECK(system.startCall('1', "123456", "5551234", kNational));
    pull(cc);
    // a priority phone (11b) is queued with 2c
    phoneSends(cc, acknowledgement(cc, phone, MessageType::Frame11b), 1);
    const Frame f = pull(cc);
    CHECK(f.mt == MessageType::Frame2c);
    CHECK(f.additionalInfo == 0x3f1);

    wait(clock, system, 59.0);
    CHECK(cc.state() == NmtChannel::State::MtQueue);
    CHECK(events.failed.empty());
    wait(clock, system, 1.5);
    CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::NoChannel);
    CHECK(cc.state() == NmtChannel::State::MtRelease);
    CHECK(isLineSignal(pull(cc), 15));
    phoneSends(cc, lineSignalFrame(cc, phone, 0x11111), 1);
    CHECK(cc.state() == NmtChannel::State::Idle);
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
        pull(cc);
        phoneSends(cc, acknowledgement(cc, phone), 1);
        pull(cc);
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
            pull(cc);
            phoneSends(cc, acknowledgement(cc, phone), 1);
            pull(cc);
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
    CHECK(pull(both).mt == MessageType::Frame2a);
    phoneSends(both, acknowledgement(both, phone), 1);
    const Frame f = pull(both);
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
    Events events;
    listen(system, events);

    // a phone of the group waits for its wake-up window (4 s)
    CHECK(system.startCall('1', "123450", "5551234", kNational));
    CHECK(cc.state() == NmtChannel::State::Idle);
    wait(clock, system, 3.9);
    CHECK(cc.state() == NmtChannel::State::Idle);
    wait(clock, system, 0.2);
    CHECK(cc.state() == NmtChannel::State::MtPaging);
    CHECK(pull(cc).mt == MessageType::Frame2a);
    CHECK(events.failed.empty());

    // another group is paged at once
    CHECK(system.releaseCall('1', "123450"));
    CHECK(system.startCall('1', "123451", "5551234", kNational));
    CHECK(cc.state() == NmtChannel::State::MtPaging);
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

    phoneSends(tc, roamingFrame(tc, busy), 40);
    for (int priority = 0; priority < 2; priority++)
    {
        Phone caller = phone;
        caller.number = priority ? "222222" : "111111";

        CHECK(system.startCall('1', caller.number, "5551234", kNational));
        pull(cc);
        phoneSends(cc, acknowledgement(cc, caller, priority ? MessageType::Frame11b : MessageType::Frame10a), 1);
        const Frame f = pull(cc);
        CHECK(f.mt == (priority ? MessageType::Frame2c : MessageType::Frame2f));
        CHECK(f.additionalInfo == (priority ? 0x9f1u : 0x9f0u));
        CHECK(system.releaseCall('1', caller.number));
        for (int i = 0; i < 4; i++) {
            pull(cc);
        }
        phoneSends(cc, lineSignalFrame(cc, caller, 0x11111), 1);
        CHECK(cc.state() == NmtChannel::State::Idle);
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
        pull(cc);
        phoneSends(cc, acknowledgement(cc, phone), 1);
        pull(cc);
        pull(tc);
        phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 3);

        // 6 frames of the number before the first ring; the cycle starts at index 1
        for (int i = 0; i < 6; i++)
        {
            const Frame f = pull(tc);
            CHECK(f.mt == MessageType::Frame8);
            CHECK(f.seqNumber == static_cast<uint8_t>((i + 1) % c.frames));
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
        // between the rings the number goes on from the index of the frame count
        const Frame f = pull(tc);
        CHECK(f.mt == MessageType::Frame8);
        CHECK(f.seqNumber == static_cast<uint8_t>(1 % c.frames));
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
        pull(cc);
        phoneSends(cc, acknowledgement(cc, phone), 1);
        pull(cc);
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
    CHECK(pull(cc).tcNo == encodeTc(System::Nmt900, 70, 1));
    CHECK(access.state() == NmtChannel::State::MtIdent);
    CHECK(both.state() == NmtChannel::State::MtPaging || both.state() == NmtChannel::State::Idle);

    // the next one gets the traffic channel
    CHECK(system.startCall('1', "222222", "5551234", kNational));
    phoneSends(cc, acknowledgement(cc, other), 1);
    CHECK(pull(cc).tcNo == encodeTc(System::Nmt900, 80, 1));
    CHECK(traffic.state() == NmtChannel::State::MtIdent);

    // and only then the combined channel, on which the call stays
    CHECK(system.startCall('1', "333333", "5551234", kNational));
    phoneSends(cc, acknowledgement(cc, third), 1);
    CHECK(pull(cc).tcNo == encodeTc(System::Nmt900, 60, 1));
    CHECK(both.state() == NmtChannel::State::MtIdent);
}

// One calling channel is busy with something else: the other one pages.
static void testBusyCallingChannel()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& busy = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 60), kRate, 1.0);
    NmtChannel& idle = system.addChannel(callingConfig(50), kRate, 1.0);
    Events events;
    listen(system, events);
    const Phone phone;
    Phone caller = phone;
    caller.number = "999999";

    phoneSends(busy, roamingFrame(busy, caller), 40);
    CHECK(busy.state() == NmtChannel::State::RoamingIdent);

    CHECK(system.startCall('1', "123456", "5551234", kNational));
    CHECK(busy.state() == NmtChannel::State::RoamingIdent);             // untouched
    CHECK(idle.state() == NmtChannel::State::MtPaging);

    // the roaming ends, the next try pages on both
    phoneSends(busy, lineSignalFrame(busy, caller, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(busy);
    }
    CHECK(busy.state() == NmtChannel::State::Idle);
    wait(clock, system, 1.05);
    CHECK(busy.state() == NmtChannel::State::MtPaging);
    CHECK(idle.state() == NmtChannel::State::MtPaging);
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
    none.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    CHECK(!none.canCall());
    CHECK(!none.startCall('1', "123456", "5551234", kNational));
    CHECK(none.transactionCount() == 0);
}

// A call that loses its channel or its channel's frames: the exchange hears it once.
static void testCallIsNotLost()
{
    // the calling channel is not rendered after the phone answered: the watchdog fails the call
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
        CHECK(cc.state() == NmtChannel::State::MtChannel);
        wait(clock, system, 4.9);
        CHECK(events.failed.empty());
        wait(clock, system, 0.2);
        CHECK(events.failed.size() == 1 && events.failed[0] == modemcellular::Cause::TempFail);
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
        pull(cc);
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
        pull(both);
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
        Events events;
        listen(system, events);
        const Phone phone;

        CHECK(system.startCall('1', "123456", "5551234", kNational));
        phoneSends(cc, lineSignalFrame(cc, phone, 0x11111), 1);
        CHECK(events.cleared == 1);
        CHECK(events.failed.empty());
    }
}

int main()
{
    testQueueCodes900();
    testCallerIdLengths();
    testTrafficChannelChoice();
    testBusyCallingChannel();
    testPhoneIsBusy();
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
