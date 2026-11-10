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

// Call from the phone: seizure, identity, dial tone, digits, address complete, clearing.

#include <cmath>

#include "phone.h"


static void testCallFlow()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    toDialTone(tc, phone);

    // dial tone: 425 Hz at speech level
    std::vector<float> tone(4800);
    CHECK(tc.render(tone.data(), static_cast<int>(tone.size()), false) == 0);
    float peak = 0.0f;
    int crossings = 0;
    for (size_t i = 0; i < tone.size(); i++)
    {
        peak = std::fmax(peak, std::fabs(tone[i]));
        if (i && ((tone[i - 1] < 0.0f) != (tone[i] < 0.0f))) {
            crossings++;
        }
    }
    CHECK(std::fabs(peak - 1.0f) < 0.01f);
    CHECK(crossings >= 84 && crossings <= 86);          // 0.1 s of 425 Hz = 85 crossings

    // the first digit ends the dial tone
    phoneSends(tc, digitFrame(tc, phone, '0', true), 1);
    CHECK(tc.dspMode() == DspMode::Audio);
    phoneSends(tc, digitFrame(tc, phone, '0', true), 1);
    dial(tc, phone, "1234");
    CHECK(tc.dialled() == "01234");
    CHECK(calls.dialled.empty());

    // idle frames end the number
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(calls.dialled.size() == 1 && calls.dialled[0] == "01234");
    CHECK(calls.subscribers[0].country == '1' && calls.subscribers[0].number == "123456");
    CHECK(calls.subscribers[0].password == "789" && !calls.subscribers[0].coinbox);
    CHECK(tc.state() == State::MoComplete);
    CHECK(tc.dspMode() == DspMode::Frame);

    // address complete four times, idle frames, then the call is connected
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(tc), 6));
    }
    for (int i = 0; i < 4; i++)
    {
        CHECK(pull(tc).mt == MessageType::Frame6);
        CHECK(tc.state() == State::MoComplete);
    }
    FrameBits bits;
    CHECK(!tc.nextFrame(bits));
    CHECK(tc.state() == State::Active);
    CHECK(tc.dspMode() == DspMode::Audio);

    // the phone clears
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(calls.cleared == 1);
    CHECK(tc.state() == State::MoRelease);
    CHECK(tc.dspMode() == DspMode::Frame);
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(tc), 15));
    }
    CHECK(tc.state() == State::Idle);
    CHECK(system.transactionCount() == 0);
    CHECK(pull(tc).mt == MessageType::Frame4);
}

static void testFarEndReleases()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    CHECK(!system.releaseCall('1', "123456"));          // no call

    toDialTone(tc, phone);
    dial(tc, phone, "5551");
    phoneSends(tc, idleFromPhone(), 1);
    for (int i = 0; i < 8; i++) {
        pull(tc);
    }
    FrameBits bits;
    CHECK(!tc.nextFrame(bits));
    CHECK(tc.state() == State::Active);

    CHECK(system.releaseCall('1', "123456"));
    CHECK(tc.state() == State::MtRelease);
    CHECK(tc.dspMode() == DspMode::Frame);
    CHECK(isLineSignal(pull(tc), 15));
    CHECK(!system.releaseCall('1', "123456"));          // already releasing
    CHECK(!system.releaseCall('1', "999999"));

    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == State::Idle);
    CHECK(calls.cleared == 0);                          // the far end ended it, no clearing to report
    CHECK(system.transactionCount() == 0);
}

static void testCallRefused()
{
    for (int variant = 0; variant < 2; variant++)
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
        Calls calls;
        calls.accept = false;
        if (variant == 0) {
            connect(system, calls);                     // handler says no
        }                                               // no handler at all
        const Phone phone;

        toDialTone(tc, phone);
        dial(tc, phone, "123");
        phoneSends(tc, idleFromPhone(), 1);
        CHECK(tc.state() == State::MtRelease);
        CHECK(tc.dspMode() == DspMode::Frame);
        CHECK(isLineSignal(pull(tc), 15));
        CHECK(calls.dialled.size() == (variant == 0 ? 1u : 0u));

        // the phone's clearing of a call that was never taken is not reported
        phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
        CHECK(tc.state() == State::Idle);
        CHECK(calls.cleared == 0);
    }
}

static void testDiallingTimeout()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;
    auto wait = [&](double seconds) {
        clock.advance(static_cast<int64_t>(kRate * seconds));
        system.run();
    };

    // no digit at all
    toDialTone(tc, phone);
    wait(0.9);
    CHECK(tc.state() == State::MoDialling);
    wait(0.2);
    CHECK(tc.state() == State::MtRelease);
    CHECK(tc.dspMode() == DspMode::Frame);
    CHECK(isLineSignal(pull(tc), 15));
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == State::Idle);

    // every digit gives another second
    toDialTone(tc, phone);
    wait(0.8);
    dial(tc, phone, "12");
    wait(0.8);
    dial(tc, phone, "3");
    CHECK(tc.dialled() == "123");
    wait(0.9);
    CHECK(tc.state() == State::MoDialling);
    wait(0.2);
    CHECK(tc.state() == State::MtRelease);
    CHECK(tc.dspMode() == DspMode::Frame);
    CHECK(calls.dialled.empty());
}

static void testLostDigits()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    // the first digit is an even one
    toDialTone(tc, phone);
    phoneSends(tc, digitFrame(tc, phone, '1', false), 1);
    CHECK(tc.state() == State::MtRelease);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);

    // an odd digit more than three frame periods after the previous one: an even one was lost
    toDialTone(tc, phone);
    dial(tc, phone, "12");
    phoneSends(tc, digitFrame(tc, phone, '3', true), 3);
    CHECK(tc.state() == State::MtRelease);
    CHECK(tc.dialled().size() == 2);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);

    // an even digit repeated more than one period after: an odd one was lost
    toDialTone(tc, phone);
    phoneSends(tc, digitFrame(tc, phone, '1', true), 1);
    phoneSends(tc, digitFrame(tc, phone, '2', false), 1);
    phoneSends(tc, digitFrame(tc, phone, '2', false), 3);
    CHECK(tc.state() == State::MtRelease);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);

    // slow but complete: three periods between digits are fine
    toDialTone(tc, phone);
    phoneSends(tc, digitFrame(tc, phone, '1', true), 1);
    phoneSends(tc, digitFrame(tc, phone, '2', false), 3);
    phoneSends(tc, digitFrame(tc, phone, '3', true), 3);
    CHECK(tc.state() == State::MoDialling);
    CHECK(tc.dialled() == "123");
}

static void testDamagedDigits()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;
    Phone other = phone;
    other.number = "654321";

    toDialTone(tc, phone);

    // wrong position marker (second nibble), digit not repeated, another subscriber: all
    // ignored. (A marker with a different first nibble would be another frame type.)
    Frame f = digitFrame(tc, phone, '1', true);
    f.digit = 0x01111;
    phoneSends(tc, f, 1);
    f = digitFrame(tc, phone, '1', true);
    f.digit = 0x00121;
    phoneSends(tc, f, 1);
    phoneSends(tc, digitFrame(tc, other, '1', true), 1);
    f = digitFrame(tc, phone, '1', true);
    f.channelNo = static_cast<uint16_t>(encodeChannel(System::Nmt450, 101, 1));
    phoneSends(tc, f, 1);
    CHECK(tc.dialled().empty());
    CHECK(tc.dspMode() == DspMode::DialTone);

    // international prefix and service numbers become '+'
    phoneSends(tc, digitFrame(tc, phone, 'A', true), 1);
    f = digitFrame(tc, phone, '2', false);
    f.digit = 0xf0222;                                  // wrong marker of an even digit
    phoneSends(tc, f, 1);
    CHECK(tc.dialled() == "A");
    phoneSends(tc, digitFrame(tc, phone, '4', false), 1);
    phoneSends(tc, digitFrame(tc, phone, '9', true), 1);
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(calls.dialled.size() == 1 && calls.dialled[0] == "+49");
    for (int i = 0; i < 8; i++) {
        pull(tc);
    }
    {
        FrameBits ninth;
        CHECK(!tc.nextFrame(ninth));                    // the call is connected
    }
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(tc);
    }

    toDialTone(tc, phone);
    phoneSends(tc, digitFrame(tc, phone, 'B', true), 1);
    phoneSends(tc, digitFrame(tc, phone, '1', false), 1);
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(calls.dialled.size() == 2 && calls.dialled[1] == "+1");

    // idle frames without a digit do nothing
    for (int i = 0; i < 8; i++) {
        pull(tc);
    }
    {
        FrameBits ninth;
        CHECK(!tc.nextFrame(ninth));                    // the call is connected
    }
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(tc);
    }
    toDialTone(tc, phone);
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(tc.state() == State::MoDialling);
    CHECK(calls.dialled.size() == 2);
}

static void testLongNumber()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    // exactly 32 digits are taken, in order
    const std::string number = "12345678901234567890123456789012";
    toDialTone(tc, phone);
    dial(tc, phone, number);
    CHECK(tc.dialled() == number);
    CHECK(tc.state() == State::MoDialling);
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(calls.dialled.size() == 1 && calls.dialled[0] == number);
    for (int i = 0; i < 8; i++) {
        pull(tc);
    }
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(tc);
    }

    // a 33rd digit is ignored: the call goes on with the 32 digits taken
    toDialTone(tc, phone);
    dial(tc, phone, number);
    phoneSends(tc, digitFrame(tc, phone, '9', true), 1);
    CHECK(tc.state() == State::MoDialling);
    CHECK(tc.dialled() == number);
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(tc.state() == State::MoComplete);
    CHECK(calls.dialled.size() == 2 && calls.dialled[1] == number);
}

static void testSeizureVariants()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    // a coin-box phone seizes with frame 12 and is passed on as such
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    toDialTone(tc, phone, MessageType::Frame12);
    dial(tc, phone, "9");
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(calls.subscribers.size() == 1 && calls.subscribers[0].coinbox);

    // a calling channel, a test channel and a wrong channel do not take a call
    Phone stranger = phone;
    stranger.number = "888888";
    NmtChannel& cc = system.addChannel(config(System::Nmt450, ChannelType::Cc, 101), kRate, 1.0);
    phoneSends(cc, callFrame(cc, stranger), 40);
    CHECK(cc.state() == State::Idle);
    NmtChannel& test = system.addChannel(config(System::Nmt450, ChannelType::Test, 102), kRate, 1.0);
    phoneSends(test, callFrame(test, stranger), 40);
    CHECK(test.state() == State::Idle);
    NmtChannel& wrong = system.addChannel(config(System::Nmt450, ChannelType::Tc, 105), kRate, 1.0);
    Frame f = callFrame(wrong, stranger);
    f.channelNo = static_cast<uint16_t>(encodeChannel(System::Nmt450, 106, 1));
    phoneSends(wrong, f, 40);
    CHECK(wrong.state() == State::Idle);
    CHECK(system.transactionCount() == 1);

    // a call seizure is answered with an identity request only after the second frame period
    Phone other = phone;
    other.number = "222222";
    NmtChannel& second = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 103), kRate, 1.0);
    phoneSends(second, callFrame(second, other), 40);
    CHECK(second.state() == State::MoIdent);
    phoneSends(second, callFrame(second, other), 1);
    CHECK(second.state() == State::MoIdent);
    phoneSends(second, callFrame(second, other), 1);
    CHECK(second.state() == State::MoConfirm);

    // no reply to the identity request: 8 requests, then the release
    Phone third = phone;
    third.number = "333333";
    NmtChannel& lonely = system.addChannel(config(System::Nmt450, ChannelType::Tc, 104), kRate, 1.0);
    phoneSends(lonely, callFrame(lonely, third), 40);
    for (int i = 0; i < 8; i++) {
        CHECK(pull(lonely).mt == MessageType::Frame3b);
    }
    CHECK(lonely.state() == State::MtRelease);
}

// The channel renders frames, then the dial tone, then silence, in one buffer sequence.
static void testRenderModes()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    const Phone phone;
    std::vector<float> block(480);
    int blocks = 0;

    // frames until the dial tone comes: the phone's frames arrive between the blocks
    phoneSends(tc, callFrame(tc, phone), 40);
    phoneSends(tc, callFrame(tc, phone), 1);
    phoneSends(tc, callFrame(tc, phone), 1);
    CHECK(tc.state() == State::MoConfirm);
    while (tc.dspMode() == DspMode::Frame && blocks < 400)
    {
        tc.render(block.data(), static_cast<int>(block.size()), false);
        blocks++;
    }
    CHECK(tc.state() == State::MoDialling);
    CHECK(blocks >= 27 && blocks <= 29);                // two frames of 138.3 ms in blocks of 10 ms

    // the buffer in which the mode changed ends in the dial tone, with no gap
    float tail = 0.0f;
    for (size_t i = block.size() - 48; i < block.size(); i++) {
        tail = std::fmax(tail, std::fabs(block[i]));
    }
    CHECK(tail > 0.5f);

    tc.render(block.data(), static_cast<int>(block.size()), false);
    float peak = 0.0f;
    for (float v : block) {
        peak = std::fmax(peak, std::fabs(v));
    }
    CHECK(peak > 0.99f && peak < 1.01f);

    // add mode adds to what is there
    std::vector<float> base(480, 0.25f);
    tc.render(base.data(), static_cast<int>(base.size()), true);
    float high = 0.0f;
    for (float v : base) {
        high = std::fmax(high, v);
    }
    CHECK(high > 1.2f && high < 1.26f);

    // the first digit: silence
    phoneSends(tc, digitFrame(tc, phone, '1', true), 1);
    std::vector<float> loud(480, 3.0f);
    tc.render(loud.data(), static_cast<int>(loud.size()), false);
    for (float v : loud) {
        CHECK(v == 0.0f);
    }
}

// The tone is one continuous sine over buffers of any size, and scales with the deviation factor.
static void testToneContinuity()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& a = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 0.5);
    NmtChannel& b = system.addChannel(config(System::Nmt450, ChannelType::Tc, 101), kRate, 0.5);
    Phone second;
    second.number = "654321";
    const Phone phone;

    toDialTone(a, phone);
    phoneSends(b, callFrame(b, second), 40);
    phoneSends(b, callFrame(b, second), 1);
    phoneSends(b, callFrame(b, second), 1);
    pull(b);
    pull(b);
    FrameBits bits;
    CHECK(!b.nextFrame(bits));
    CHECK(b.dspMode() == DspMode::DialTone);

    std::vector<float> whole(480), parts(480);
    a.render(whole.data(), 480, false);
    b.render(parts.data(), 100, false);
    b.render(parts.data() + 100, 1, false);
    b.render(parts.data() + 101, 379, false);
    float peak = 0.0f;
    for (size_t i = 0; i < 480; i++)
    {
        CHECK(std::fabs(whole[i] - parts[i]) < 1e-6f);
        peak = std::fmax(peak, std::fabs(whole[i]));
    }
    CHECK(std::fabs(peak - 0.5f) < 0.01f);
    CHECK(whole[0] == 0.0f);                            // every dial tone starts at phase 0
}

// The far end releases a call in every state; only a call that is up can be released.
static void testNetworkReleaseStates()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    // no call before the number is complete: nothing to release
    phoneSends(tc, callFrame(tc, phone), 40);
    CHECK(!system.releaseCall('1', "123456"));
    CHECK(tc.state() == State::MoIdent);
    phoneSends(tc, callFrame(tc, phone), 1);
    phoneSends(tc, callFrame(tc, phone), 1);
    CHECK(!system.releaseCall('1', "123456"));
    CHECK(tc.state() == State::MoConfirm);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    for (int i = 0; i < 4; i++) {
        pull(tc);
    }

    // in MoComplete, after two address-complete frames
    toDialTone(tc, phone);
    dial(tc, phone, "12");
    phoneSends(tc, idleFromPhone(), 1);
    pull(tc);
    pull(tc);
    CHECK(tc.state() == State::MoComplete);
    CHECK(system.releaseCall('1', "123456"));
    CHECK(tc.state() == State::MtRelease);
    CHECK(isLineSignal(pull(tc), 15));
    CHECK(calls.cleared == 0);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(tc.state() == State::Idle);

    // in MoComplete the phone clears: the exchange is told once
    toDialTone(tc, phone);
    dial(tc, phone, "12");
    phoneSends(tc, idleFromPhone(), 1);
    pull(tc);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(calls.cleared == 1);
    CHECK(tc.state() == State::MoRelease);
    CHECK(tc.dspMode() == DspMode::Frame);
    CHECK(!system.releaseCall('1', "123456"));
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(tc), 15));
    }
    CHECK(tc.state() == State::Idle);

    // a phone that clears while the dial tone plays
    toDialTone(tc, phone);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(calls.cleared == 1);                          // no call, nothing to tell
    CHECK(tc.dspMode() == DspMode::Frame);
    std::vector<float> block(480);
    CHECK(tc.render(block.data(), 480, false) > 0);     // frames again, not the tone
    for (int i = 0; i < 4; i++) {
        pull(tc);
    }
    CHECK(tc.state() == State::Idle);
}

// What a handler may do from inside the call.
static void testHandlerReentrancy()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    const Phone phone;
    bool answer = true;
    int released = -1;

    // the far end is busy: the handler releases at once
    system.setCallHandler([&](const NmtSubscriber& s, const std::string&) {
        released = system.releaseCall(s.country, s.number) ? 1 : 0;
        return answer;
    });
    int cleared = 0;
    system.setClearHandler([&](const NmtSubscriber& s) {
        cleared++;
        CHECK(!system.releaseCall(s.country, s.number));        // the call is gone already
    });

    for (int variant = 0; variant < 2; variant++)
    {
        answer = (variant == 0);
        released = -1;
        toDialTone(tc, phone);
        dial(tc, phone, "12");
        phoneSends(tc, idleFromPhone(), 1);
        CHECK(released == 1);
        CHECK(tc.state() == State::MtRelease);
        CHECK(tc.dspMode() == DspMode::Frame);
        CHECK(isLineSignal(pull(tc), 15));
        phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
        CHECK(tc.state() == State::Idle);
        CHECK(system.transactionCount() == 0);
    }
    CHECK(cleared == 0);

    // the phone clears; the handler's releaseCall finds nothing
    system.setCallHandler([](const NmtSubscriber&, const std::string&) { return true; });
    toDialTone(tc, phone);
    dial(tc, phone, "12");
    phoneSends(tc, idleFromPhone(), 1);
    phoneSends(tc, lineSignalFrame(tc, phone, 0x11111), 1);
    CHECK(cleared == 1);
    CHECK(tc.state() == State::MoRelease);
}

// A connected call setup that nobody renders ends by itself and is reported.
static void testCompleteWatchdog()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    NmtChannel& other = system.addChannel(config(System::Nmt450, ChannelType::Tc, 101), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    toDialTone(tc, phone);
    dial(tc, phone, "12");
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(tc.state() == State::MoComplete);

    // the subscriber has one transaction: no second channel for it
    phoneSends(other, callFrame(other, phone), 40);
    CHECK(other.state() == State::Idle);

    clock.advance(static_cast<int64_t>(kRate * 4.9));
    system.run();
    CHECK(tc.state() == State::MoComplete);
    clock.advance(static_cast<int64_t>(kRate * 0.2));
    system.run();
    CHECK(tc.state() == State::Idle);
    CHECK(calls.cleared == 1);
    CHECK(system.transactionCount() == 0);
    phoneSends(other, callFrame(other, phone), 40);
    CHECK(other.state() == State::MoIdent);
}

// A digit during the proceed to send is taken, and there is no dial tone after it.
static void testDigitsBeforeDialTone()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    phoneSends(tc, callFrame(tc, phone), 40);
    phoneSends(tc, callFrame(tc, phone), 1);
    phoneSends(tc, callFrame(tc, phone), 1);
    CHECK(isLineSignal(pull(tc), 3));
    phoneSends(tc, digitFrame(tc, phone, '1', true), 1);
    CHECK(tc.dialled() == "1");
    CHECK(tc.state() == State::MoDialling);
    CHECK(tc.dspMode() == DspMode::Audio);
    phoneSends(tc, idleFromPhone(), 1);
    CHECK(tc.state() == State::MoComplete);
    CHECK(calls.dialled.size() == 1 && calls.dialled[0] == "1");
}

// Digits of another phone do not count and do not keep the call alive.
static void testOtherPhoneDuringDialling()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;
    Phone other = phone;
    other.number = "654321";
    auto wait = [&](double seconds) {
        clock.advance(static_cast<int64_t>(kRate * seconds));
        system.run();
    };

    toDialTone(tc, phone);
    dial(tc, phone, "12");
    wait(0.8);
    phoneSends(tc, digitFrame(tc, other, '9', true), 1);
    phoneSends(tc, digitFrame(tc, other, '9', true), 5);
    CHECK(tc.dialled() == "12");
    CHECK(tc.state() == State::MoDialling);
    wait(0.3);
    CHECK(tc.state() == State::MtRelease);
}

// A phone that is not on this channel number is not heard.
static void testInvalidChannel()
{
    NmtChannel bad(config(System::Nmt450, ChannelType::Tc, 401), kRate, 1.0);
    std::vector<float> buffer(480, 7.0f);
    FrameBits bits;

    CHECK(!bad.valid());
    CHECK(!bad.nextFrame(bits));
    CHECK(bad.render(buffer.data(), 480, false) == 0);
    for (float v : buffer) {
        CHECK(v == 0.0f);
    }
    std::vector<float> untouched(480, 7.0f);
    CHECK(bad.render(untouched.data(), 480, true) == 0);
    for (float v : untouched) {
        CHECK(v == 7.0f);
    }
}

// The seizure and the identity reply of a call from the phone both have to carry the area of the
// base station (NMT Doc 450-1 para 4.1.1.2, 4.3.3.10.2).
static void testIdentityArea()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    NmtChannel& tc = system.addChannel(config(System::Nmt450, ChannelType::Tc, 100), kRate, 1.0);
    Calls calls;
    connect(system, calls);
    const Phone phone;

    // traffic area 0x11: area info 1 and Y 1
    Frame seizure = seizureFrame(tc, phone, MessageType::Frame10b);

    seizure.areaInfo = 1;
    seizure.trafficArea = 2;
    phoneSends(tc, seizure, 40);
    CHECK(tc.state() == State::Idle);                   // not the area of this station

    seizure.trafficArea = 1;
    phoneSends(tc, seizure, 40);
    CHECK(tc.state() == State::MoIdent);
    seizure.trafficArea = 2;
    phoneSends(tc, seizure, 3);
    CHECK(tc.state() == State::MoIdent);                // a reply from another area is not taken
    seizure.trafficArea = 1;
    phoneSends(tc, seizure, 3);
    CHECK(tc.state() == State::MoConfirm);
}

int main()
{
    testIdentityArea();
    testCallFlow();
    testFarEndReleases();
    testCallRefused();
    testDiallingTimeout();
    testLostDigits();
    testDamagedDigits();
    testLongNumber();
    testSeizureVariants();
    testRenderModes();
    testToneContinuity();
    testNetworkReleaseStates();
    testHandlerReentrancy();
    testCompleteWatchdog();
    testDigitsBeforeDialTone();
    testOtherPhoneDuringDialling();
    testInvalidChannel();

    if (g_failures == 0) {
        std::printf("all call tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
