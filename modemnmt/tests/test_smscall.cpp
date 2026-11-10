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

// Short message calls (NMT Doc 450-3 / 900-3 Annex 24): the data link of the DMS on the speech
// path of the traffic channel, end to end through the audio of the channel. The phone side is a
// data link of its own behind a modulator and a demodulator.

#include <cmath>
#include <functional>

#include "ffsk.h"
#include "nmtmodem.h"
#include "phone.h"

using Bytes = std::vector<uint8_t>;

// The data link of the phone, on the audio of the channel.
struct PhoneLink
{
    NmtDmsLink link;
    FfskModulator modulator;
    FfskDemodulator demodulator;
    Bytes received;

    PhoneLink() :
        link(true),
        modulator(kRate, kBitRate, kFreqBit0, kFreqBit1, kTxPeakFsk),
        demodulator(kRate, kBitRate, kFreqBit0, kFreqBit1, kBitAdjust)
    {
        modulator.setBitSource([this]() {
            int bit = 0;
            return link.pullBit(bit) ? bit : -1;
        });
        demodulator.setBitSink([this](int bit, double quality, double level) {
            link.pushBit(bit, quality, level / kTxPeakFsk);
        });
        link.setDataHandler([this](const std::vector<uint8_t>& data) {
            received.insert(received.end(), data.begin(), data.end());
        });
    }
};

struct Station
{
    modemcellular::SampleClock clock;
    NmtSystem system;
    NmtChannel* cc;
    NmtChannel* tc;
    Calls calls;
    Phone phone;
    PhoneLink air;
    std::vector<SmsSubmit> submitted;
    uint8_t submitCause = 0;
    struct Result
    {
        NmtServiceEnd result;
        uint8_t cause;
    };
    std::vector<Result> results;

    Station() :
        clock(kRate),
        system(clock),
        cc(nullptr),
        tc(nullptr)
    {
        cc = &system.addChannel(config(System::Nmt450, ChannelType::Cc, 50), kRate, 1.0);
        ChannelConfig c = config(System::Nmt450, ChannelType::Tc, 100);
        c.supervisory = 2;
        tc = &system.addChannel(c, kRate, 1.0);
        connect(system, calls);
        system.setSmsHandlers(
            [this](const NmtSubscriber&, const SmsSubmit& message) {
                submitted.push_back(message);
                return submitCause;
            },
            [this](const NmtSubscriber&, NmtServiceEnd result, uint8_t cause) {
                results.push_back({result, cause});
            });
    }

    ~Station()
    {
        system.setCallHandler(nullptr);
        system.setClearHandler(nullptr);
        system.setSmsHandlers(nullptr, nullptr);
    }

    // The radio for some seconds: what the channel sends goes to the phone's link, what the
    // phone's link sends goes to the channel.
    void run(double seconds)
    {
        const int block = static_cast<int>(kRate / 100.0);
        std::vector<float> down(static_cast<size_t>(block)), up(static_cast<size_t>(block));
        const int blocks = static_cast<int>(seconds * 100.0 + 0.5);

        for (int b = 0; b < blocks; b++)
        {
            tc->render(down.data(), block, false);
            air.demodulator.process(down.data(), block);
            std::fill(up.begin(), up.end(), 0.0f);
            air.modulator.render(up.data(), block, false);
            tc->receive(up.data(), block);
            air.link.advance(block / kRate);
            clock.advance(block);
            system.run();
        }
    }

    // The phone calls the number and the call is through to the data link.
    void dialCentre(const std::string& number)
    {
        toDialTone(*tc, phone);
        dial(*tc, phone, number);
        phoneSends(*tc, idleFromPhone(), 1);
        for (int i = 0; i < 12 && tc->state() != State::Sms; i++)
        {
            FrameBits bits;
            tc->nextFrame(bits);
        }
    }

    // A short message to the phone, up to the data link: page, answer, traffic channel,
    // identity, the order to answer by itself and the phone's acknowledgement.
    void toPhone()
    {
        Frame ack = seizureFrame(*cc, phone, MessageType::Frame10a);

        ack.msPassword = 0;
        CHECK(pullCalling(*cc).mt == MessageType::Frame2a);
        phoneSends(*cc, ack, 1);
        CHECK(pullCalling(*cc).mt == MessageType::Frame2b);
        CHECK(tc->state() == State::MtIdent);
        pull(*tc);
        phoneSends(*tc, seizureFrame(*tc, phone, MessageType::Frame10b), 3);
        CHECK(tc->state() == State::MtAutoanswer);
        for (int i = 0; i < 6; i++) {
            CHECK(pull(*tc).mt == MessageType::Frame6);
        }
        for (int i = 0; i < 4; i++) {
            CHECK(isLineSignal(pull(*tc), 12));
        }
        phoneSends(*tc, lineSignalFrame(*tc, phone, 0xccccc), 1);
        for (int i = 0; i < 6 && tc->state() != State::Sms; i++)
        {
            FrameBits bits;
            tc->nextFrame(bits);
        }
        CHECK(tc->state() == State::Sms);
        CHECK(tc->dspMode() == DspMode::Audio);
    }
};

// An SMS-SUBMIT "HELLO" to +358400654321 (Annex 24 page 29).
static const Bytes kSubmit = {
    0x00, 0x01,
    0x0c, 0x91, 0x3d, 0x85, 0x94, 0x21, 0x43, 0x65,
    0x41, 0x13,
    0x11, 0x01,
    0x0d, 0x91, 0x3d, 0x85, 0xa4, 0x6a, 0x45, 0x23, 0xf1,
    0x00, 0x00, 0x8f,
    0x05, 0xc8, 0x22, 0x93, 0xf9, 0x04
};

static Bytes withHeader(const Bytes& rp)
{
    Bytes out = smsHeader();

    out.insert(out.end(), rp.begin(), rp.end());
    return out;
}

static SmsDeliver hello()
{
    SmsDeliver m;

    m.originator = "+35849654321";
    m.text = "HELLO";
    m.time.year = 96;
    m.time.month = 12;
    m.time.day = 24;
    m.time.hour = 13;
    m.time.minute = 35;
    m.time.second = 4;
    return m;
}

// A message from the phone: the phone calls the service centre, the message goes to the
// handler, the phone gets the acknowledgement; 5 s later the call is released.
static void testFromPhone()
{
    Station s;

    s.system.setSmscNumber("1234");
    s.dialCentre("1234");
    CHECK(s.tc->state() == State::Sms);
    CHECK(s.calls.dialled.empty());                     // the exchange is not called

    s.air.link.startAsCaller();
    s.air.link.queueData(kSubmit.data(), kSubmit.size());
    const Bytes answer = withHeader(smsRpAckToPhone(0x01));
    int steps = 0;

    while (s.air.received != answer && steps++ < 100) {
        s.run(0.1);
    }
    CHECK(s.submitted.size() == 1);
    if (!s.submitted.empty())
    {
        CHECK(s.submitted[0].text == "HELLO");
        CHECK(s.submitted[0].destination == "+358400654321");
    }
    CHECK(s.air.received == answer);
    CHECK(s.tc->state() == State::Sms);

    // the release comes 5 s after the answer is acknowledged
    steps = 0;
    while (s.tc->state() == State::Sms && steps++ < 100) {
        s.run(0.1);
    }
    CHECK(steps >= 45 && steps <= 55);
    CHECK(s.tc->state() == State::MtRelease);
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
    CHECK(s.system.transactionCount() == 0);
    CHECK(s.calls.cleared == 0);
}

// The handler refuses the message, or there is no handler: the phone gets an RP-ERROR.
static void testRefused()
{
    {
        Station s;

        s.submitCause = 22;
        s.system.setSmscNumber("1234");
        s.dialCentre("1234");
        s.air.link.startAsCaller();
        s.air.link.queueData(kSubmit.data(), kSubmit.size());
        s.run(4.0);
        CHECK(s.air.received == withHeader(smsRpErrorToPhone(0x01, 22)));
    }
    {
        Station s;

        s.system.setSmsHandlers(nullptr, nullptr);
        s.system.setSmscNumber("1234");
        s.dialCentre("1234");
        s.air.link.startAsCaller();
        s.air.link.queueData(kSubmit.data(), kSubmit.size());
        s.run(4.0);
        CHECK(s.air.received == withHeader(smsRpErrorToPhone(0x01, 41)));
    }
}

// Without a service centre number, or another number, it is an ordinary call; a phone that
// says nothing on the data link is released after 10 s.
static void testNumbersAndSilence()
{
    {
        Station s;

        s.dialCentre("1234");
        CHECK(s.calls.dialled.size() == 1 && s.tc->state() == State::Active);
    }
    {
        Station s;

        s.system.setSmscNumber("1234");
        s.dialCentre("1235");
        CHECK(s.calls.dialled.size() == 1 && s.tc->state() == State::Active);
    }
    {
        Station s;

        s.system.setSmscNumber("1234");
        s.dialCentre("1234");
        CHECK(s.tc->state() == State::Sms);
        s.run(9.8);
        CHECK(s.tc->state() == State::Sms);
        s.run(0.3);
        CHECK(s.tc->state() == State::MtRelease);
    }
}

// A message to the phone: called without ringing, the exchange opens the data link and sends the
// message; the phone's acknowledgement is the result; 5 s later the call is released.
static void testToPhone()
{
    Station s;
    const SmsDeliver m = hello();

    CHECK(s.system.sendSms(s.phone.country, s.phone.number, m));
    CHECK(!s.system.sendSms(s.phone.country, s.phone.number, m));        // busy
    CHECK(!s.system.startCall(s.phone.country, s.phone.number, "5551234", modemcellular::NumberType::National));
    s.toPhone();

    s.run(5.0);
    Bytes rp;
    CHECK(smsBuildDeliver(m, 0, rp));
    CHECK(s.air.received == withHeader(rp));
    CHECK(s.air.link.eightBits());

    const Bytes ack = {0x02, 0x00};
    s.air.link.queueData(ack.data(), ack.size());
    s.run(2.0);
    CHECK(s.tc->state() == State::Sms);
    CHECK(s.results.empty());
    s.run(4.0);
    CHECK(s.tc->state() == State::MtRelease);
    phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
    CHECK(s.system.transactionCount() == 0);
    CHECK(s.results.size() == 1 && s.results[0].result == NmtServiceEnd::Ok);
}

// The phone refuses the message, or does not answer at all.
static void testToPhoneFails()
{
    {
        Station s;

        CHECK(s.system.sendSms(s.phone.country, s.phone.number, hello()));
        s.toPhone();
        s.run(5.0);
        const Bytes error = {0x04, 0x00, 0x42, 0x01, 0x16};
        s.air.link.queueData(error.data(), error.size());
        s.run(8.0);
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.results.size() == 1 && s.results[0].result == NmtServiceEnd::Rejected && s.results[0].cause == 0x16);
    }
    {
        // the data link takes it, but no answer comes: 20 s
        Station s;

        CHECK(s.system.sendSms(s.phone.country, s.phone.number, hello()));
        s.toPhone();
        s.run(19.0);
        CHECK(s.tc->state() == State::Sms);
        s.run(1.5);
        CHECK(s.tc->state() == State::MtRelease);
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        CHECK(s.results.size() == 1 && s.results[0].result == NmtServiceEnd::NoAck);
    }
    {
        // the phone hangs up before
        Station s;

        CHECK(s.system.sendSms(s.phone.country, s.phone.number, hello()));
        s.toPhone();
        phoneSends(*s.tc, lineSignalFrame(*s.tc, s.phone, 0x11111), 1);
        for (int i = 0; i < 4; i++) {
            pull(*s.tc);
        }
        CHECK(s.system.transactionCount() == 0);
        CHECK(s.results.size() == 1 && s.results[0].result == NmtServiceEnd::Undelivered);
    }
    {
        // a message that cannot be coded is refused at once
        Station s;
        SmsDeliver bad = hello();

        bad.originator = "";
        CHECK(!s.system.sendSms(s.phone.country, s.phone.number, bad));
        CHECK(s.system.transactionCount() == 0);
    }
}

int main()
{
    testFromPhone();
    testRefused();
    testNumbersAndSilence();
    testToPhone();
    testToPhoneFails();

    if (g_failures)
    {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("short message call tests passed\n");
    return 0;
}
