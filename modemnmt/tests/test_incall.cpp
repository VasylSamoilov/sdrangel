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

// Procedures during a call: MFT converter, power change, register recall.

#include "phone.h"

struct Station
{
    modemcellular::SampleClock clock;
    NmtSystem system;
    NmtChannel* a;
    Calls calls;
    Phone phone;
    std::string mft;                    // digits of the MFT converter
    std::string recall;                 // digits of the register recall
    std::string recallEnded;
    int recallEnds = 0;
    size_t recallEnough = 0;            // the recall has enough digits at this number
    std::vector<std::pair<int, bool>> powers;

    explicit Station(System sys = System::Nmt450) :
        clock(kRate),
        system(clock)
    {
        ChannelConfig c = config(sys, ChannelType::Tc, 100);

        c.supervisory = 2;
        a = &system.addChannel(c, kRate, 1.0);
        connect(system, calls);
        system.setMftHandler([this](const NmtSubscriber&, char digit) { mft.push_back(digit); });
        system.setRecallHandlers(
            [this](const NmtSubscriber&, char digit) {
                recall.push_back(digit);
                return recallEnough != 0 && recall.size() >= recallEnough;
            },
            [this](const NmtSubscriber&, const std::string& digits) {
                recallEnded = digits;
                recallEnds++;
            });
        system.setPowerHandler([this](const NmtSubscriber&, int level, bool done) { powers.emplace_back(level, done); });
        toDialTone(*a, phone);
        dial(*a, phone, "99");
        phoneSends(*a, idleFromPhone(), 1);
        for (int i = 0; i < 12 && a->state() != State::Active; i++)
        {
            FrameBits bits;
            a->nextFrame(bits);
        }
        CHECK(a->state() == State::Active);
    }

    ~Station()
    {
        system.setCallHandler(nullptr);
        system.setClearHandler(nullptr);
        system.setMftHandler(nullptr);
        system.setRecallHandlers(nullptr, nullptr);
        system.setPowerHandler(nullptr);
    }

    void wait(double seconds)
    {
        clock.advance(static_cast<int64_t>(kRate * seconds));
        system.run();
    }

    // The phone presses digit keys: every digit for two frames, 14a for the 1st, 3rd, ... digit.
    void keys(const std::string& digits)
    {
        for (char digit : digits)
        {
            const bool odd = (keyCount % 2) == 0;

            phoneSends(*a, digitFrame(*a, phone, digit, odd), 1);
            phoneSends(*a, digitFrame(*a, phone, digit, odd), 1);
            keyCount++;
        }
    }

    size_t keyCount = 0;
};

static bool moreFrames(NmtChannel& channel)
{
    FrameBits bits;

    return channel.nextFrame(bits);
}

static void testMft()
{
    Station s;

    // L(8): the converter is asked for; 3 idle frames, L(4) four times, then the audio path
    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
    CHECK(s.a->state() == State::MftIn);
    for (int i = 0; i < 3; i++) {
        CHECK(pull(*s.a).mt == MessageType::Frame6);
    }
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.a), 4));
    }
    CHECK(!moreFrames(*s.a));
    CHECK(s.a->state() == State::Mft);
    CHECK(s.a->dspMode() == DspMode::Audio);

    // the digits, each for two frames, and every other one in the other kind of frame
    s.keys("5*#7");
    CHECK(s.mft == "5*#7");
    s.keys("1");
    CHECK(s.mft == "5*#71");

    // L(7): leaving; L(10) four times, 4 idle frames, the conversation
    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x77777), 1);
    CHECK(s.a->state() == State::MftOut);
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.a), 10));
    }
    for (int i = 0; i < 4; i++) {
        CHECK(pull(*s.a).mt == MessageType::Frame6);
    }
    CHECK(!moreFrames(*s.a));
    CHECK(s.a->state() == State::Active);
    CHECK(s.a->dspMode() == DspMode::Audio);
    CHECK(s.calls.cleared == 0);
}

static void testMftEnds()
{
    // a phone that goes quiet in the converter state is let out
    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        CHECK(s.a->state() == State::Mft);
        s.wait(9.0);
        CHECK(s.a->state() == State::Mft);
        s.wait(2.0);
        CHECK(s.a->state() == State::MftOut);
        for (int i = 0; i < 9; i++) {
            moreFrames(*s.a);
        }
        CHECK(s.a->state() == State::Active);
    }

    // the phone clears in the converter state
    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x11111), 1);
        CHECK(s.calls.cleared == 1);
        CHECK(s.a->state() == State::MoRelease);
    }

    // the far end hangs up in the converter state
    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
        CHECK(s.a->state() == State::MtRelease);
    }

    // the request is a wrong phone or channel: nothing happens
    {
        Station s;
        Phone other;

        other.number = "654321";
        phoneSends(*s.a, lineSignalFrame(*s.a, other, 0x88888), 1);
        CHECK(s.a->state() == State::Active);
    }
}

static void testPowerChange()
{
    Station s;

    CHECK(!s.system.changePower(s.phone.country, s.phone.number, 1));       // that level already
    CHECK(!s.system.changePower(s.phone.country, s.phone.number, 4));
    CHECK(!s.system.changePower('1', "654321", 2));
    CHECK(s.system.changePower(s.phone.country, s.phone.number, 2));
    CHECK(s.a->state() == State::PowerChange);

    // NMT-450: the channel with the old level in N1, the new level in Na
    for (int i = 0; i < 3; i++)
    {
        const Frame f = pull(*s.a);
        CHECK(f.mt == MessageType::Frame3a);
        CHECK(f.channelNo == encodeChannel(System::Nmt450, 100, 1));
        CHECK(f.tcNo == encodeTc(System::Nmt450, 100, 2));
        CHECK(f.msNumber == digitsToValue(s.phone.number));
    }

    // the identity of the phone on the channel is the answer, whatever level it carries; another
    // phone's identity is not
    Phone other = s.phone;
    other.number = "654321";
    phoneSends(*s.a, seizureFrame(*s.a, other, MessageType::Frame10b), 1);
    CHECK(s.a->state() == State::PowerChange);
    phoneSends(*s.a, seizureFrame(*s.a, s.phone, MessageType::Frame10b), 1);
    CHECK(s.a->state() == State::Active);
    CHECK(s.powers.size() == 1 && s.powers[0].first == 2 && s.powers[0].second);

    // the phone has the new level for the rest of the call
    CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
    CHECK(pull(*s.a).channelNo == encodeChannel(System::Nmt450, 100, 2));
}

static void testPowerChangeNoAnswer()
{
    Station s;

    CHECK(s.system.changePower(s.phone.country, s.phone.number, 3));
    for (int i = 0; i < 8; i++) {
        CHECK(pull(*s.a).mt == MessageType::Frame3a);
    }
    CHECK(!moreFrames(*s.a));                       // after T the speech is back
    CHECK(s.a->state() == State::Active);
    CHECK(s.powers.size() == 1 && s.powers[0].first == 3 && !s.powers[0].second);

    // the level is unchanged
    CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
    CHECK(pull(*s.a).channelNo == encodeChannel(System::Nmt450, 100, 1));
}

static void testPowerChange900()
{
    Station s(System::Nmt900);

    CHECK(s.system.changePower(s.phone.country, s.phone.number, 3));
    const Frame f = pull(*s.a);
    CHECK(f.mt == MessageType::Frame3a);
    CHECK(f.channelNo == encodeChannel(System::Nmt900, 100, 3));        // the new level is in N1
    CHECK(f.tcNo == encodeTc(System::Nmt900, 100, 0));

    Frame answer = seizureFrame(*s.a, s.phone, MessageType::Frame10b);
    answer.channelNo = static_cast<uint16_t>(encodeChannel(System::Nmt900, 100, 3));
    phoneSends(*s.a, answer, 1);
    CHECK(s.a->state() == State::Active);
    CHECK(s.powers.size() == 1 && s.powers[0].second);
}

static void testRecall()
{
    Station s;

    s.recallEnough = 2;
    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
    CHECK(s.a->state() == State::RecallProceed);
    for (int i = 0; i < 3; i++) {
        CHECK(pull(*s.a).mt == MessageType::Frame6);
    }
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.a), 3));
    }
    CHECK(!moreFrames(*s.a));
    CHECK(s.a->state() == State::RecallDigits);
    CHECK(s.a->dspMode() == DspMode::DialTone);

    // the first digit stops the dial tone
    s.keys("4");
    CHECK(s.a->dspMode() == DspMode::Audio);
    CHECK(s.recall == "4");
    CHECK(s.a->state() == State::RecallDigits);

    // enough digits: L(6) four times, 4 idle frames, the conversation
    s.keys("2");
    CHECK(s.recall == "42");
    CHECK(s.a->state() == State::RecallEnd);
    CHECK(s.recallEnds == 1 && s.recallEnded == "42");
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.a), 6));
    }
    for (int i = 0; i < 4; i++) {
        CHECK(pull(*s.a).mt == MessageType::Frame6);
    }
    CHECK(!moreFrames(*s.a));
    CHECK(s.a->state() == State::Active);
    CHECK(s.calls.cleared == 0);
}

static void testRecallTimeouts()
{
    // the digits stop: the recall ends with what was dialled
    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        s.keys("7");
        s.wait(7.0);
        CHECK(s.a->state() == State::RecallDigits);
        s.wait(2.0);
        CHECK(s.a->state() == State::RecallEnd);
        CHECK(s.recallEnds == 1 && s.recallEnded == "7");
    }

    // no digit at all
    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        s.wait(29.0);
        CHECK(s.a->state() == State::RecallDigits);
        s.wait(2.0);
        CHECK(s.a->state() == State::RecallEnd);
        CHECK(s.recallEnds == 1 && s.recallEnded.empty());
        CHECK(s.a->dspMode() == DspMode::Frame);
    }

    // a phone that starts with the second kind of frame (the first digit got lost) is still taken
    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        s.keyCount = 1;
        s.keys("9");
        CHECK(s.recall == "9");
        CHECK(s.a->dspMode() == DspMode::Audio);                // the dial tone ended
    }
}

static void testWatchdogs()
{
    // the channel is not rendered: the conversation goes on after the watchdog
    Station s;

    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
    CHECK(s.a->state() == State::MftIn);
    s.wait(5.1);
    CHECK(s.a->state() == State::Active);

    CHECK(s.system.changePower(s.phone.country, s.phone.number, 2));
    s.wait(5.1);
    CHECK(s.a->state() == State::Active);
    CHECK(s.powers.size() == 1 && !s.powers[0].second);
}

#include <cmath>

static double toneLevel(NmtChannel& channel)
{
    std::vector<float> x(4800);
    double c = 0.0, sn = 0.0;

    channel.render(x.data(), static_cast<int>(x.size()), false);
    for (size_t i = 0; i < x.size(); i++)
    {
        const double phase = 2.0 * 3.14159265358979 * 3985.0 * static_cast<double>(i) / kRate;

        c += x[i] * std::cos(phase);
        sn += x[i] * std::sin(phase);
    }

    return std::sqrt(c * c + sn * sn) / static_cast<double>(x.size());
}

// The supervisory tone of the base station is on air in every step of the procedures.
static void testToneInProcedures()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem quietSystem(clock);
    ChannelConfig cfg = config(System::Nmt450, ChannelType::Tc, 100);

    cfg.supervisory = 2;
    NmtChannel& quiet = quietSystem.addChannel(cfg, kRate, 1.0);
    const double floorLevel = toneLevel(quiet);

    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
        CHECK(s.a->state() == State::MftIn);
        CHECK(toneLevel(*s.a) > 10.0 * floorLevel + 1e-4);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        CHECK(s.a->state() == State::Mft);
        CHECK(toneLevel(*s.a) > 10.0 * floorLevel + 1e-4);
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x77777), 1);
        CHECK(s.a->state() == State::MftOut);
        CHECK(toneLevel(*s.a) > 10.0 * floorLevel + 1e-4);
    }
    {
        Station s;

        CHECK(s.system.changePower(s.phone.country, s.phone.number, 2));
        CHECK(toneLevel(*s.a) > 10.0 * floorLevel + 1e-4);
    }
    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
        CHECK(toneLevel(*s.a) > 10.0 * floorLevel + 1e-4);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        CHECK(s.a->state() == State::RecallDigits);
        CHECK(toneLevel(*s.a) > 10.0 * floorLevel + 1e-4);          // the dial tone carries it too
        s.keys("1");
        s.wait(9.0);
        CHECK(s.a->state() == State::RecallEnd);
        CHECK(toneLevel(*s.a) > 10.0 * floorLevel + 1e-4);
    }
}

// A call that ends in the middle of a power change or a recall: the host is told.
static void testEndedProcedures()
{
    {
        Station s;

        CHECK(s.system.changePower(s.phone.country, s.phone.number, 2));
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x11111), 1);
        CHECK(s.calls.cleared == 1);
        CHECK(s.powers.size() == 1 && s.powers[0].first == 2 && !s.powers[0].second);
    }
    {
        Station s;

        CHECK(s.system.changePower(s.phone.country, s.phone.number, 2));
        CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
        CHECK(s.powers.size() == 1 && !s.powers[0].second);
    }
    {
        Station s;

        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        s.keys("4");
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x11111), 1);
        CHECK(s.recallEnds == 1 && s.recallEnded == "4");
        CHECK(s.calls.cleared == 1);
    }
}

// The phone is heard by its idle frames; one that is not heard is let out.
static void testMftPresence()
{
    Station s;

    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
    for (int i = 0; i < 8; i++) {
        moreFrames(*s.a);
    }
    for (int i = 0; i < 20; i++)
    {
        s.wait(5.0);
        phoneSends(*s.a, idleFromPhone(), 36);
    }
    CHECK(s.a->state() == State::Mft);
    s.wait(11.0);
    CHECK(s.a->state() == State::MftOut);
}

// L(7) again from a phone that did not hear the answer; the tail of an answer is not that.
static void testRepeatedMftOut()
{
    Station s;

    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
    for (int i = 0; i < 8; i++) {
        moreFrames(*s.a);
    }
    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x77777), 1);
    for (int i = 0; i < 9; i++) {
        moreFrames(*s.a);
    }
    CHECK(s.a->state() == State::Active);

    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x77777), 1);       // right after: the tail
    CHECK(s.a->state() == State::Active);
    phoneSends(*s.a, idleFromPhone(), 5);                               // it waits in the converter state
    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x77777), 1);       // and tries again
    CHECK(s.a->state() == State::MftOut);
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.a), 10));
    }
}

// A damaged frame does not disturb the count of the digits.
static void testDamagedMftDigit()
{
    Station s;

    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
    for (int i = 0; i < 8; i++) {
        moreFrames(*s.a);
    }
    s.keys("5");
    Frame bad = digitFrame(*s.a, s.phone, '8', true);
    bad.digit ^= 0x10;
    phoneSends(*s.a, bad, 3);
    s.keys("6");
    CHECK(s.mft == "56");
}

// The level of the phone is the one of the channel it was on, also after a switch.
static void testPowerAcrossSwitch()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    Calls calls;
    Phone phone;
    ChannelConfig c1 = config(System::Nmt450, ChannelType::Tc, 100);
    ChannelConfig c2 = config(System::Nmt450, ChannelType::Tc, 102);

    c1.msPower = 1;
    c2.msPower = 2;
    NmtChannel& a = system.addChannel(c1, kRate, 1.0);
    NmtChannel& b = system.addChannel(c2, kRate, 1.0);
    connect(system, calls);
    toDialTone(a, phone);
    dial(a, phone, "99");
    phoneSends(a, idleFromPhone(), 1);
    for (int i = 0; i < 12 && a.state() != State::Active; i++) {
        moreFrames(a);
    }
    CHECK(system.moveCall(phone.country, phone.number));
    for (int i = 0; i < 9; i++) {
        moreFrames(a);
    }
    Frame answer = seizureFrame(b, phone, MessageType::Frame10b);
    phoneSends(b, answer, 1);
    CHECK(b.state() == State::Active);
    CHECK(system.releaseCall(phone.country, phone.number));
    CHECK(pull(b).channelNo == encodeChannel(System::Nmt450, 102, 1));      // still the level it had
    system.setCallHandler(nullptr);
    system.setClearHandler(nullptr);
}

// A digit is new when its kind of frame differs from the last digit's, however long it is repeated.
static void testDigitRepeats()
{
    Station s;

    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
    for (int i = 0; i < 8; i++) {
        moreFrames(*s.a);
    }
    for (int i = 0; i < 5; i++) {
        phoneSends(*s.a, digitFrame(*s.a, s.phone, '5', true), 1);
    }
    CHECK(s.mft == "5");

    // idle frames in between, a pause, then the digits of the manual scheme
    phoneSends(*s.a, idleFromPhone(), 1);
    phoneSends(*s.a, idleFromPhone(), 40);
    phoneSends(*s.a, digitFrame(*s.a, s.phone, '6', false), 1);
    phoneSends(*s.a, digitFrame(*s.a, s.phone, '6', false), 1);
    phoneSends(*s.a, idleFromPhone(), 1);
    phoneSends(*s.a, idleFromPhone(), 1);
    phoneSends(*s.a, digitFrame(*s.a, s.phone, 'D', true), 1);
    phoneSends(*s.a, digitFrame(*s.a, s.phone, 'D', true), 1);
    phoneSends(*s.a, digitFrame(*s.a, s.phone, 'C', false), 1);
    phoneSends(*s.a, digitFrame(*s.a, s.phone, 'C', false), 1);
    CHECK(s.mft == "56DC");
}

// A handler may end the call from inside any of the callbacks.
static void testHandlerReleases()
{
    {
        Station s;
        bool released = false;

        s.system.setMftHandler([&s, &released](const NmtSubscriber& sub, char digit) {
            s.mft.push_back(digit);
            released = s.system.releaseCall(sub.country, sub.number);
        });
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        s.keys("5");
        CHECK(released && s.mft == "5");
        CHECK(s.a->state() == State::MtRelease);
        CHECK(isLineSignal(pull(*s.a), 15));
        s.keys("6");
        CHECK(s.mft == "5");
        CHECK(s.calls.cleared == 0);
    }
    {
        Station s;
        bool released = false;

        s.recallEnough = 1;
        s.system.setRecallHandlers(
            [&s, &released](const NmtSubscriber& sub, char digit) {
                s.recall.push_back(digit);
                released = s.system.releaseCall(sub.country, sub.number);
                return true;
            },
            [&s](const NmtSubscriber&, const std::string& digits) {
                s.recallEnded = digits;
                s.recallEnds++;
            });
        phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.a);
        }
        s.keys("4");
        CHECK(released);
        CHECK(s.recallEnds == 1 && s.recallEnded == "4");
        CHECK(s.a->state() == State::MtRelease);
        CHECK(isLineSignal(pull(*s.a), 15));
    }
    {
        Station s;
        bool released = false;

        s.system.setPowerHandler([&s, &released](const NmtSubscriber& sub, int level, bool done) {
            s.powers.emplace_back(level, done);
            released = s.system.releaseCall(sub.country, sub.number);
        });
        CHECK(s.system.changePower(s.phone.country, s.phone.number, 2));
        Frame answer = seizureFrame(*s.a, s.phone, MessageType::Frame10b);
        answer.channelNo = static_cast<uint16_t>(encodeChannel(System::Nmt450, 100, 2));
        phoneSends(*s.a, answer, 1);
        CHECK(released);
        CHECK(s.powers.size() == 1 && s.powers[0].second);
        CHECK(s.a->state() == State::MtRelease);
        CHECK(pull(*s.a).channelNo == encodeChannel(System::Nmt450, 100, 2));
    }
}

// The R button again starts a new recall.
static void testRecallRestart()
{
    Station s;

    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
    for (int i = 0; i < 8; i++) {
        moreFrames(*s.a);
    }
    s.keys("4");
    phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
    CHECK(s.a->state() == State::RecallProceed);
    CHECK(s.recallEnds == 1 && s.recallEnded == "4");           // the digits so far
    for (int i = 0; i < 3; i++) {
        CHECK(pull(*s.a).mt == MessageType::Frame6);
    }
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.a), 3));
    }
    CHECK(!moreFrames(*s.a));
    CHECK(s.a->state() == State::RecallDigits);
    CHECK(s.a->dspMode() == DspMode::DialTone);
    s.keyCount = 0;
    s.keys("7");
    CHECK(s.recall == "47");                                    // the test handler keeps one buffer
    s.wait(9.0);
    CHECK(s.recallEnds == 2 && s.recallEnded == "7");
}

// A clear or a release in every step of the procedures.
static void testEndsInEveryState()
{
    for (int step = 0; step < 7; step++)
    {
        for (int how = 0; how < 2; how++)
        {
            Station s;

            switch (step)
            {
            case 0:     // MftIn
                phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
                CHECK(s.a->state() == State::MftIn);
                break;
            case 1:     // Mft
                phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
                for (int i = 0; i < 8; i++) { moreFrames(*s.a); }
                CHECK(s.a->state() == State::Mft);
                break;
            case 2:     // MftOut
                phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x88888), 1);
                for (int i = 0; i < 8; i++) { moreFrames(*s.a); }
                phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x77777), 1);
                CHECK(s.a->state() == State::MftOut);
                break;
            case 3:     // PowerChange
                CHECK(s.system.changePower(s.phone.country, s.phone.number, 2));
                break;
            case 4:     // RecallProceed
                phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
                CHECK(s.a->state() == State::RecallProceed);
                break;
            case 5:     // RecallDigits
                phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
                for (int i = 0; i < 8; i++) { moreFrames(*s.a); }
                s.keys("4");
                CHECK(s.a->state() == State::RecallDigits);
                break;
            default:    // RecallEnd
                s.recallEnough = 1;
                phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x55555), 1);
                for (int i = 0; i < 8; i++) { moreFrames(*s.a); }
                s.keys("4");
                CHECK(s.a->state() == State::RecallEnd);
                break;
            }

            if (how == 0)
            {
                phoneSends(*s.a, lineSignalFrame(*s.a, s.phone, 0x11111), 1);
                CHECK(s.calls.cleared == 1);
                CHECK(s.a->state() == State::MoRelease);
            }
            else
            {
                CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
                CHECK(s.calls.cleared == 0);
                CHECK(s.a->state() == State::MtRelease);
                CHECK(!s.system.releaseCall(s.phone.country, s.phone.number));
            }
            CHECK(s.a->dspMode() == DspMode::Frame);
            CHECK(isLineSignal(pull(*s.a), 15));
            // a power change or recall that was going on was reported once
            CHECK(s.powers.size() == (step == 3 ? 1u : 0u));
            CHECK(s.recallEnds == ((step >= 4) ? 1 : 0));
            s.wait(5.5);
            CHECK(s.system.transactionCount() == 0);
            CHECK(s.a->state() == State::Idle);
        }
    }
}

// NMT-900: the power order in the coding of the standard (900-1 p33-34, 4.4.1.8).
static void testPowerOrder900Table()
{
    struct Case { int channel; unsigned area80; unsigned n[4]; unsigned na; };
    static const Case cases[] = {
        {100,  0, {0x064, 0x264, 0x464, 0x664}, 0x064},
        {600,  0, {0x858, 0xa58, 0xc58, 0xe58}, 0x258},
        {1030, 1, {0x006, 0x206, 0x406, 0x606}, 0x406},
    };

    for (const Case& c : cases)
    {
        for (int level = 0; level < 4; level++)
        {
            modemcellular::SampleClock clock(kRate);
            NmtSystem system(clock);
            Calls calls;
            Phone phone;
            ChannelConfig cfg = config(System::Nmt900, ChannelType::Tc, c.channel);

            cfg.msPower = (level == 0) ? 1 : 0;
            NmtChannel& a = system.addChannel(cfg, kRate, 1.0);
            connect(system, calls);
            toDialTone(a, phone);
            dial(a, phone, "99");
            phoneSends(a, idleFromPhone(), 1);
            for (int i = 0; i < 12 && a.state() != State::Active; i++) {
                moreFrames(a);
            }

            CHECK(system.changePower(phone.country, phone.number, level));
            const Frame f = pull(a);
            CHECK(f.mt == MessageType::Frame3a);
            CHECK(f.channelNo == c.n[level]);
            CHECK(f.tcNo == c.na);
            CHECK(((f.trafficArea & 0x80) != 0) == (c.area80 != 0));
            system.setCallHandler(nullptr);
            system.setClearHandler(nullptr);
        }
    }
}

int main()
{
    testDigitRepeats();
    testHandlerReleases();
    testRecallRestart();
    testEndsInEveryState();
    testPowerOrder900Table();
    testToneInProcedures();
    testEndedProcedures();
    testMftPresence();
    testRepeatedMftOut();
    testDamagedMftDigit();
    testPowerAcrossSwitch();
    testMft();
    testMftEnds();
    testPowerChange();
    testPowerChangeNoAnswer();
    testPowerChange900();
    testRecall();
    testRecallTimeouts();
    testWatchdogs();

    if (g_failures) {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("in-call procedure tests passed\n");
    return 0;
}
