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

// Message waiting indicators: the codec, a call that only carries them, and the indicators at the
// clearing of a call.

#include "phone.h"

struct Station
{
    modemcellular::SampleClock clock;
    NmtSystem system;
    NmtChannel* cc = nullptr;
    NmtChannel* tc = nullptr;
    Calls calls;
    Phone phone;
    int alerting = 0;
    int answered = 0;
    int failed = 0;
    struct Report { uint8_t flags; NmtServiceEnd result; };
    std::vector<Report> reports;

    explicit Station(bool withTc = true, bool withCc = true) :
        clock(kRate),
        system(clock)
    {
        if (withCc) {
            cc = &system.addChannel(config(System::Nmt450, ChannelType::Cc, 50), kRate, 1.0);
        }
        if (withTc)
        {
            ChannelConfig c = config(System::Nmt450, ChannelType::Tc, 100);

            c.supervisory = 2;
            tc = &system.addChannel(c, kRate, 1.0);
        }
        connect(system, calls);
        system.setCallToPhoneHandlers(
            [this](const NmtSubscriber&) { alerting++; },
            [this](const NmtSubscriber&) { answered++; },
            [this](const NmtSubscriber&, modemcellular::Cause) { failed++; });
        system.setMwiHandler([this](const NmtSubscriber&, uint8_t flags, NmtServiceEnd result) {
            reports.push_back({flags, result});
        });
    }

    ~Station()
    {
        system.setCallHandler(nullptr);
        system.setClearHandler(nullptr);
        system.setCallToPhoneHandlers(nullptr, nullptr, nullptr);
        system.setMwiHandler(nullptr);
    }

    void wait(double seconds)
    {
        clock.advance(static_cast<int64_t>(kRate * seconds));
        system.run();
    }

    Frame acknowledgement()
    {
        Frame f = seizureFrame(*cc, phone, MessageType::Frame10a);

        f.msPassword = 0;
        return f;
    }

    // Page, acknowledgement, traffic channel, identity: the call is in the state of the automatic answer.
    void toAutoanswer()
    {
        CHECK(system.sendMwi(phone.country, phone.number, MwiSms));
        CHECK(cc->state() == State::MtPaging);
        pull(*cc);
        phoneSends(*cc, acknowledgement(), 1);
        CHECK(pull(*cc).mt == MessageType::Frame2b);
        CHECK(tc->state() == State::MtIdent);
        pull(*tc);
        phoneSends(*tc, seizureFrame(*tc, phone, MessageType::Frame10b), 3);
        CHECK(tc->state() == State::MtAutoanswer);
    }

    // The idle frames the phone sends after its identity.
    void phoneIdle()
    {
        for (int i = 0; i < 4; i++) {
            phoneSends(*tc, idleFromPhone(), 1);
        }
    }

    // The phone calls and the call is connected.
    void connectCall()
    {
        toDialTone(*tc, phone);
        dial(*tc, phone, "99");
        phoneSends(*tc, idleFromPhone(), 1);
        for (int i = 0; i < 12 && tc->state() != State::Active; i++)
        {
            FrameBits bits;
            tc->nextFrame(bits);
        }
        CHECK(tc->state() == State::Active);
    }
};

static bool moreFrames(NmtChannel& channel)
{
    FrameBits bits;

    return channel.nextFrame(bits);
}

static void checkIndicators(const Frame& f, const Phone& phone, uint8_t flags)
{
    CHECK(f.mt == MessageType::Frame5c);
    CHECK(f.msNumber == digitsToValue(phone.number));
    CHECK(f.waitingInfo == mwiWireInfo(flags));
}

static void testCodec()
{
    // the 12 bits: i1..i6 and the same again, SMS first
    CHECK(mwiWireInfo(MwiSms) == 0x820);
    CHECK(mwiWireInfo(MwiVoice) == 0x410);
    CHECK(mwiWireInfo(MwiFax) == 0x208);
    CHECK(mwiWireInfo(MwiEmail) == 0x104);
    CHECK(mwiWireInfo(MwiData) == 0x082);
    CHECK(mwiWireInfo(MwiAll) == 0xfbe);
    CHECK(mwiWireInfo(0) == 0);

    for (unsigned flags = 0; flags <= MwiAll; flags++)
    {
        uint8_t back = 0xff;

        CHECK(mwiFlagsFromWire(mwiWireInfo(static_cast<uint8_t>(flags)), back));
        CHECK(back == flags);
    }

    // the phone does not accept two copies that differ, or more than 12 bits
    uint8_t flags = 0;
    CHECK(!mwiFlagsFromWire(0x830, flags));
    CHECK(!mwiFlagsFromWire(0x821, flags));
    CHECK(!mwiFlagsFromWire(0x1820, flags));
    CHECK(mwiFlagsFromWire(0x041, flags) && flags == 0);        // the spare bit carries nothing

    // text
    CHECK(mwiParseFlags("sms", flags) && flags == MwiSms);
    CHECK(mwiParseFlags("SMS,Voice", flags) && flags == (MwiSms | MwiVoice));
    CHECK(mwiParseFlags("fax email data", flags) && flags == (MwiFax | MwiEmail | MwiData));
    CHECK(mwiParseFlags("e-mail", flags) && flags == MwiEmail);
    CHECK(mwiParseFlags("5", flags) && flags == 5);
    CHECK(mwiParseFlags("31", flags) && flags == MwiAll);
    CHECK(!mwiParseFlags("", flags));
    CHECK(!mwiParseFlags("0", flags));
    CHECK(!mwiParseFlags("32", flags));
    CHECK(!mwiParseFlags("3 fax", flags));
    CHECK(!mwiParseFlags("sms,pigeon", flags));
    CHECK(mwiFlagsText(MwiSms | MwiData) == "sms,data");
    CHECK(mwiFlagsText(0).empty());

    // the frame: P(2), the phone, the 12 bits
    Frame f;

    f.mt = MessageType::Frame5c;
    f.msCountry = 1;
    f.msNumber = static_cast<uint32_t>(digitsToValue("123456"));
    f.waitingInfo = mwiWireInfo(MwiSms | MwiFax);
    const FrameBits bits = encodeFrame(f);
    EncodedBits encoded;

    for (int i = 0; i < kEncodedBits; i++) {
        encoded[static_cast<size_t>(i)] = bits[static_cast<size_t>(kBitSyncBits + kFrameSyncBits + i)];
    }
    const Frame back = decodeFrame(encoded, Direction::MtxToMs, false);
    CHECK(back.mt == MessageType::Frame5c);
    CHECK(back.waitingInfo == 0xa28);
    CHECK(back.msNumber == f.msNumber);
}

static void testMessageCall()
{
    Station s;

    s.toAutoanswer();
    CHECK(s.alerting == 0);

    // the phone is quiet first; the order to answer goes out four times
    s.phoneIdle();
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.tc), 12));
    }

    // it answers: 3 idle frames, the indicators twice, the clearing signal four times
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0xccccc), 1);
    CHECK(s.tc->state() == State::MwiClear);
    for (int i = 0; i < 3; i++) {
        CHECK(pull(*s.tc).mt == MessageType::Frame6);
    }
    for (int i = 0; i < 2; i++) {
        checkIndicators(pull(*s.tc), s.phone, MwiSms);
    }
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.tc), 15));
    }
    CHECK(s.tc->state() == State::MtRelease);
    CHECK(s.reports.empty());

    // the phone clears: delivered
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
    CHECK(s.system.transactionCount() == 0);
    CHECK(s.tc->state() == State::Idle);
    CHECK(s.reports.size() == 1 && s.reports[0].flags == MwiSms && s.reports[0].result == NmtServiceEnd::Ok);
    CHECK(s.alerting == 0 && s.answered == 0 && s.failed == 0 && s.calls.cleared == 0);
}

// The phone answers while the order is still going out: the order is sent to its end first, then
// 3 idle frames count from its end (what the phone answered in the tail shortens them).
static void testEarlyAnswer()
{
    for (int pulled = 1; pulled <= 5; pulled++)
    {
        Station s;

        s.toAutoanswer();
        // fewer than four idle frames of the phone: the order does not start
        for (int i = 0; i < 3; i++) {
            phoneSends(*s.tc, idleFromPhone(), 1);
        }
        CHECK(pull(*s.tc).mt == MessageType::Frame6);
        phoneSends(*s.tc, idleFromPhone(), 1);

        // pulled frames of the order (1..4) or the order and one idle frame (5)
        for (int i = 0; i < pulled; i++) {
            CHECK(i < 4 ? isLineSignal(pull(*s.tc), 12) : pull(*s.tc).mt == MessageType::Frame6);
        }
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0xccccc), 1);

        for (int i = pulled; i < 4; i++) {
            CHECK(isLineSignal(pull(*s.tc), 12));
        }
        int idleFrames = 0;
        Frame f = pull(*s.tc);

        while (f.mt == MessageType::Frame6 && idleFrames < 8)
        {
            idleFrames++;
            f = pull(*s.tc);
        }
        CHECK(idleFrames == ((pulled == 5) ? 2 : 3));
        checkIndicators(f, s.phone, MwiSms);
        checkIndicators(pull(*s.tc), s.phone, MwiSms);
        for (int i = 0; i < 4; i++) {
            CHECK(isLineSignal(pull(*s.tc), 15));
        }
    }
}

static void testNoAcknowledgement()
{
    // no acknowledgement of the clearing: sent, not confirmed
    {
        Station s;

        s.toAutoanswer();
        s.phoneIdle();
        for (int i = 0; i < 4; i++) {
            pull(*s.tc);
        }
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0xccccc), 1);
        for (int i = 0; i < 9; i++) {
            pull(*s.tc);
        }
        CHECK(s.tc->state() == State::MtRelease);
        s.wait(2.1);
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::NoAck);
    }

    // the phone clears while the indicators go out: that is the acknowledgement
    {
        Station s;

        s.toAutoanswer();
        s.phoneIdle();
        for (int i = 0; i < 4; i++) {
            pull(*s.tc);
        }
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0xccccc), 1);
        for (int i = 0; i < 5; i++) {
            pull(*s.tc);
        }
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.tc->state() == State::MwiClear);
        for (int i = 0; i < 4; i++) {
            pull(*s.tc);
        }
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::Ok);
    }
}

static void testPhoneRings()
{
    // a phone that does not know the order is rung; the exchange hears nothing about it
    {
        Station s;

        s.toAutoanswer();
        s.phoneIdle();
        for (int i = 0; i < 8; i++) {
            pull(*s.tc);
        }
        pull(*s.tc);
        CHECK(s.tc->state() == State::MtRinging);
        CHECK(s.alerting == 0);

        // the user answers: nothing to hear, the indicators and the release
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0xeeeee), 1);
        CHECK(s.tc->state() == State::MtComplete);
        CHECK(s.answered == 0);
        for (int i = 0; i < 4; i++) {
            pull(*s.tc);
        }
        CHECK(s.tc->state() == State::MwiClear);
        for (int i = 0; i < 2; i++) {
            checkIndicators(pull(*s.tc), s.phone, MwiSms);
        }
    }

    // nobody answers
    {
        Station s;

        s.toAutoanswer();
        s.phoneIdle();
        for (int i = 0; i < 9; i++) {
            pull(*s.tc);
        }
        CHECK(s.tc->state() == State::MtRinging);
        s.wait(14.5);
        CHECK(s.tc->state() == State::MtRinging);
        s.wait(1.0);
        CHECK(s.tc->state() == State::MtRelease);
        s.wait(2.5);
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::Undelivered);
        CHECK(s.alerting == 0 && s.failed == 0 && s.answered == 0);
    }

    // the phone does not send an idle frame: the order goes out after a while
    {
        Station s;

        s.toAutoanswer();
        for (int i = 0; i < 6; i++) {
            CHECK(pull(*s.tc).mt == MessageType::Frame6);
        }
        CHECK(isLineSignal(pull(*s.tc), 12));
    }
}

static void testNotDelivered()
{
    // the phone does not answer the page
    {
        Station s;

        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiVoice));
        for (int i = 0; i < 12 && s.system.transactionCount(); i++) {
            s.wait(1.1);
        }
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].flags == MwiVoice && s.reports[0].result == NmtServiceEnd::Unreachable);
        CHECK(s.failed == 0);
    }

    // no traffic channel for 60 s
    {
        Station s(false);

        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        pull(*s.cc);
        phoneSends(*s.cc, s.acknowledgement(), 1);
        pull(*s.cc);
        CHECK(s.cc->state() == State::MtQueue);
        s.wait(60.5);
        s.wait(2.5);
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::NoChannel);
    }

    // refused: bad flags, no calling channel, a phone that is busy
    {
        Station s(true, false);

        CHECK(!s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));      // no calling channel
    }
    {
        Station s;

        CHECK(!s.system.sendMwi(s.phone.country, s.phone.number, 0));
        CHECK(!s.system.sendMwi(s.phone.country, s.phone.number, 0x20));
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        CHECK(!s.system.sendMwi(s.phone.country, s.phone.number, MwiFax));      // paged already
    }
}

static void testIndicatorsAtClearing()
{
    // the far end clears: 5c twice, then the clearing signal, then the phone's answer
    {
        Station s;

        s.connectCall();
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms | MwiFax));
        CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
        CHECK(s.tc->state() == State::MwiClear);
        for (int i = 0; i < 2; i++) {
            checkIndicators(pull(*s.tc), s.phone, MwiSms | MwiFax);
        }
        for (int i = 0; i < 4; i++) {
            CHECK(isLineSignal(pull(*s.tc), 15));
        }
        CHECK(s.tc->state() == State::MtRelease);
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].flags == (MwiSms | MwiFax) && s.reports[0].result == NmtServiceEnd::Ok);
    }

    // the phone clears first: its clearing signal is the acknowledgement, the indicators follow
    {
        Station s;

        s.connectCall();
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiVoice));
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.calls.cleared == 1);
        CHECK(s.tc->state() == State::MwiClear);
        for (int i = 0; i < 2; i++) {
            checkIndicators(pull(*s.tc), s.phone, MwiVoice);
        }
        for (int i = 0; i < 4; i++) {
            CHECK(isLineSignal(pull(*s.tc), 15));
        }
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.tc->state() == State::Idle);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::Ok);
    }

    // flags added twice are sent once, together
    {
        Station s;

        s.connectCall();
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiEmail));
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        checkIndicators(pull(*s.tc), s.phone, MwiSms | MwiEmail);
    }

    // a call that ends in a procedure state still carries them
    {
        Station s;

        s.connectCall();
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x88888), 1);
        CHECK(s.tc->state() == State::MftIn);
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiFax));
        CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
        CHECK(s.tc->state() == State::MwiClear);
        checkIndicators(pull(*s.tc), s.phone, MwiSms | MwiFax);
        checkIndicators(pull(*s.tc), s.phone, MwiSms | MwiFax);
        for (int i = 0; i < 4; i++) {
            CHECK(isLineSignal(pull(*s.tc), 15));
        }
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::Ok);
    }

    // a phone that is not in a conversation is busy
    {
        Station s;

        s.toAutoanswer();
        CHECK(!s.system.sendMwi(s.phone.country, s.phone.number, MwiFax));
    }

    // a call without flags is cleared as before
    {
        Station s;

        s.connectCall();
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.tc->state() == State::MoRelease);
        CHECK(isLineSignal(pull(*s.tc), 15));
        CHECK(s.reports.empty());
    }
}

// A message call that is not on a traffic channel yet gives way to the phone and to calls.
static void testPreemption()
{
    // a call to the phone
    {
        Station s;

        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        CHECK(s.system.startCall(s.phone.country, s.phone.number, "5551234", modemcellular::NumberType::National));
        CHECK(s.reports.empty());                       // the indicators go with the call
        CHECK(s.system.transactionCount() == 1);
        CHECK(s.cc->state() == State::MtPaging);
        CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
        CHECK(s.reports.size() == 1 && s.reports[0].flags == MwiSms && s.reports[0].result == NmtServiceEnd::Undelivered);
    }
    // the phone calls itself
    {
        Station s;

        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        phoneSends(*s.tc, seizureFrame(*s.tc, s.phone, MessageType::Frame10b), 40);
        CHECK(s.tc->state() == State::MoIdent);
        CHECK(s.reports.empty());
        CHECK(s.cc->state() == State::Idle);
    }
    // the far end of the exchange stops it
    {
        Station s;

        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiVoice));
        CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].flags == MwiVoice);
        CHECK(!s.system.releaseCall(s.phone.country, s.phone.number));
    }
    // once the phone is on the traffic channel the call runs to its end
    {
        Station s;

        s.toAutoanswer();
        CHECK(!s.system.startCall(s.phone.country, s.phone.number, "5551234", modemcellular::NumberType::National));
        CHECK(!s.system.releaseCall(s.phone.country, s.phone.number));
        CHECK(s.reports.empty());
    }
}

// The indicators of a cancelled message call go out with the call that took its place.
static void testIndicatorsCarried()
{
    // a call to the phone: answered, then cleared by the far end
    {
        Station s;

        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms | MwiFax));
        CHECK(s.system.startCall(s.phone.country, s.phone.number, "5551234", modemcellular::NumberType::National));
        CHECK(s.reports.empty());
        pull(*s.cc);
        phoneSends(*s.cc, s.acknowledgement(), 1);
        pull(*s.cc);
        pull(*s.tc);
        phoneSends(*s.tc, seizureFrame(*s.tc, s.phone, MessageType::Frame10b), 3);
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0xeeeee), 1);
        for (int i = 0; i < 4; i++) {
            moreFrames(*s.tc);
        }
        CHECK(s.tc->state() == State::Active);
        CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
        checkIndicators(pull(*s.tc), s.phone, MwiSms | MwiFax);
        checkIndicators(pull(*s.tc), s.phone, MwiSms | MwiFax);
        for (int i = 0; i < 4; i++) {
            CHECK(isLineSignal(pull(*s.tc), 15));
        }
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.reports.size() == 1 && s.reports[0].flags == (MwiSms | MwiFax) && s.reports[0].result == NmtServiceEnd::Ok);
    }

    // a handler that sends the flags again does not stop a call
    {
        Station s;
        int resends = 0;

        s.system.setMwiHandler([&s, &resends](const NmtSubscriber& sub, uint8_t flags, NmtServiceEnd result) {
            s.reports.push_back({flags, result});
            if (result == NmtServiceEnd::Undelivered) {
                resends++;
                s.system.sendMwi(sub.country, sub.number, flags);
            }
        });
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        CHECK(s.system.startCall(s.phone.country, s.phone.number, "5551234", modemcellular::NumberType::National));
        CHECK(s.system.transactionCount() == 1);
        CHECK(resends == 0);
    }

    // the same on a combined channel that receives the phone's own call: no crash, the call is taken
    {
        modemcellular::SampleClock clock(kRate);
        NmtSystem system(clock);
        NmtChannel& cctc = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 60), kRate, 1.0);
        Calls calls;
        const Phone phone;
        int reports = 0;

        connect(system, calls);
        system.setMwiHandler([&](const NmtSubscriber& sub, uint8_t flags, NmtServiceEnd) {
            reports++;
            system.sendMwi(sub.country, sub.number, flags);
        });
        CHECK(system.sendMwi(phone.country, phone.number, MwiSms));
        CHECK(cctc.state() == State::MtPaging);
        // the phone's call comes in on another channel: here a second combined channel
        NmtChannel& other = system.addChannel(config(System::Nmt450, ChannelType::CcTc, 62), kRate, 1.0);
        phoneSends(other, seizureFrame(other, phone, MessageType::Frame10b), 40);
        CHECK(other.state() == State::MoIdent);
        CHECK(system.transactionCount() == 1);
        CHECK(reports == 0);
        pull(other);
        system.setMwiHandler(nullptr);
        system.setCallHandler(nullptr);
        system.setClearHandler(nullptr);
    }
}

// A call (not a message call) is cleared while the phone hangs up at the same moment: the indicators go.
static void testClearingRace()
{
    Station s;

    s.connectCall();
    CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
    CHECK(s.system.releaseCall(s.phone.country, s.phone.number));
    CHECK(s.tc->state() == State::MwiClear);
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
    CHECK(s.tc->state() == State::MwiClear);
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);     // and again
    CHECK(s.tc->state() == State::MwiClear);
    for (int i = 0; i < 2; i++) {
        checkIndicators(pull(*s.tc), s.phone, MwiSms);
    }
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.tc), 15));
    }
    CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::Ok);
}

// The phone does not show up on the traffic channel; the queue of a message call is short.
static void testNotThere()
{
    {
        Station s;

        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        pull(*s.cc);
        phoneSends(*s.cc, s.acknowledgement(), 1);
        pull(*s.cc);
        for (int i = 0; i < 8; i++) {
            moreFrames(*s.tc);
        }
        CHECK(s.tc->state() == State::Idle);
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::Unreachable);
    }
    {
        Station s(false);

        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        pull(*s.cc);
        phoneSends(*s.cc, s.acknowledgement(), 1);
        pull(*s.cc);
        CHECK(s.cc->state() == State::MtQueue);
        s.wait(9.5);
        CHECK(s.cc->state() == State::MtQueue);
        s.wait(1.0);
        CHECK(s.cc->state() == State::Idle);                    // nothing to release
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::NoChannel);
    }
}

// A switch to another channel does not keep the indicators back.
static void testIndicatorsDuringSwitch()
{
    modemcellular::SampleClock clock(kRate);
    NmtSystem system(clock);
    Calls calls;
    Phone phone;
    ChannelConfig c1 = config(System::Nmt450, ChannelType::Tc, 100);
    ChannelConfig c2 = config(System::Nmt450, ChannelType::Tc, 102);
    std::vector<Station::Report> reports;

    c1.supervisory = 2;
    c2.supervisory = 2;
    NmtChannel& a = system.addChannel(c1, kRate, 1.0);
    NmtChannel& b = system.addChannel(c2, kRate, 1.0);
    connect(system, calls);
    system.setMwiHandler([&reports](const NmtSubscriber&, uint8_t flags, NmtServiceEnd result) {
        reports.push_back({flags, result});
    });
    toDialTone(a, phone);
    dial(a, phone, "99");
    phoneSends(a, idleFromPhone(), 1);
    for (int i = 0; i < 12 && a.state() != State::Active; i++) {
        moreFrames(a);
    }
    CHECK(system.moveCall(phone.country, phone.number));
    CHECK(system.sendMwi(phone.country, phone.number, MwiSms));
    for (int i = 0; i < 9; i++) {
        moreFrames(a);
    }
    phoneSends(b, seizureFrame(b, phone, MessageType::Frame10b), 1);
    CHECK(b.state() == State::Active);
    phoneSends(b, lineSignalFrame(b, phone, 0x11111), 1);
    checkIndicators(pull(b), phone, MwiSms);
    checkIndicators(pull(b), phone, MwiSms);
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(b), 15));
    }
    CHECK(reports.size() == 1 && reports[0].result == NmtServiceEnd::Ok);
    system.setMwiHandler(nullptr);
    system.setCallHandler(nullptr);
    system.setClearHandler(nullptr);
}

// The phone hangs up before the indicators went out; the phone is gone, so the host is told so.
static void testPhoneHangsUp()
{
    {
        Station s;

        s.toAutoanswer();
        s.phoneIdle();
        for (int i = 0; i < 4; i++) {
            pull(*s.tc);
        }
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0xccccc), 1);
        CHECK(s.tc->state() == State::MwiClear);
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.tc->state() == State::MoRelease);
        CHECK(isLineSignal(pull(*s.tc), 15));
        s.wait(5.5);
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::Undelivered);
    }
    // the tone of the phone is lost during a call: no indicators into the void
    {
        Station s;

        s.connectCall();
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        s.wait(3.1);
        CHECK(s.tc->state() == State::MtRelease);
        s.wait(2.5);
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.reports.size() == 1 && s.reports[0].result == NmtServiceEnd::Unreachable);
        CHECK(s.calls.cleared == 1);
    }
}

// Flags survive a procedure that leaves the call as it was.
static void testFlagsAcrossProcedures()
{
    Station s;

    s.connectCall();
    CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiData));
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x88888), 1);
    for (int i = 0; i < 8; i++) {
        moreFrames(*s.tc);
    }
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x77777), 1);
    for (int i = 0; i < 9; i++) {
        moreFrames(*s.tc);
    }
    CHECK(s.tc->state() == State::Active);
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
    checkIndicators(pull(*s.tc), s.phone, MwiData);
}

static void testTeardown()
{
    // the system goes away in a message call, in a clearing with indicators and while paging
    {
        std::vector<Station::Report> reports;
        {
            modemcellular::SampleClock clock(kRate);
            NmtSystem system(clock);
            NmtChannel& cc = system.addChannel(config(System::Nmt450, ChannelType::Cc, 50), kRate, 1.0);
            ChannelConfig tcConfig = config(System::Nmt450, ChannelType::Tc, 100);
            const Phone phone;

            NmtChannel& tc = system.addChannel(tcConfig, kRate, 1.0);
            system.setMwiHandler([&reports](const NmtSubscriber&, uint8_t flags, NmtServiceEnd result) {
                reports.push_back({flags, result});
            });
            CHECK(system.sendMwi(phone.country, phone.number, MwiSms));
            pull(cc);
            Frame ack = seizureFrame(cc, phone, MessageType::Frame10a);
            ack.msPassword = 0;
            phoneSends(cc, ack, 1);
            pull(cc);
            pull(tc);
            phoneSends(tc, seizureFrame(tc, phone, MessageType::Frame10b), 3);
            CHECK(tc.state() == State::MtAutoanswer);
        }
        CHECK(reports.size() == 1 && reports[0].flags == MwiSms && reports[0].result == NmtServiceEnd::Undelivered);
    }
    {
        std::vector<Station::Report> reports;
        {
            modemcellular::SampleClock clock(kRate);
            NmtSystem system(clock);
            ChannelConfig cc = config(System::Nmt450, ChannelType::Cc, 50);
            const Phone phone;

            cc.batteryPeriod = 3;               // the phone sleeps: nothing is on air yet, no channel is bound
            cc.batteryGroup = 8;
            system.addChannel(cc, kRate, 1.0);
            system.setMwiHandler([&reports](const NmtSubscriber&, uint8_t flags, NmtServiceEnd result) {
                reports.push_back({flags, result});
            });
            CHECK(system.sendMwi(phone.country, phone.number, MwiFax));
            CHECK(reports.empty());
            // the handler lives on, the system is destroyed here
        }
        CHECK(reports.size() == 1 && reports[0].flags == MwiFax && reports[0].result == NmtServiceEnd::Undelivered);
    }
    {
        Station s;

        s.connectCall();
        CHECK(s.system.sendMwi(s.phone.country, s.phone.number, MwiSms));
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.tc->state() == State::MwiClear);
        (void) moreFrames(*s.tc);
    }
}

int main()
{
    testCodec();
    testMessageCall();
    testEarlyAnswer();
    testNoAcknowledgement();
    testPhoneRings();
    testNotDelivered();
    testIndicatorsAtClearing();
    testPreemption();
    testIndicatorsCarried();
    testClearingRace();
    testNotThere();
    testIndicatorsDuringSwitch();
    testPhoneHangsUp();
    testFlagsAcrossProcedures();
    testTeardown();

    if (g_failures) {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("message waiting tests passed\n");
    return 0;
}
