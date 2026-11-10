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

// NMT base station and call router together: phones roam in, call the echo or music endpoint,
// call each other, and the far end ends calls.

#include <cmath>
#include <functional>
#include <memory>

#include "nmtcellularbridge.h"
#include "phone.h"

static const double kPi = 3.14159265358979323846;

static double toneAmplitude(const std::vector<float>& x, size_t from, size_t to, double hz, double rate)
{
    double re = 0.0, im = 0.0, sum = 0.0;
    const double n = static_cast<double>(to - from);

    for (size_t i = from; i < to; i++)
    {
        const double w = 0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(i - from) / n);
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / rate;

        re += w * x[i] * std::cos(phase);
        im -= w * x[i] * std::sin(phase);
        sum += w;
    }

    return 2.0 * std::sqrt(re * re + im * im) / sum;
}

// The phone's microphone: speech from a to b seconds (all the time if b is 0) and the
// supervisory tone of tone number 2.
using Bursts = std::vector<std::pair<double, double>>;

static std::function<void(std::vector<float>&, size_t)> microphone(const Bursts& bursts, double speechHz = 1000.0)
{
    return [=](std::vector<float>& in, size_t first) {
        for (size_t i = 0; i < in.size(); i++)
        {
            const double t = static_cast<double>(first + i) / kRate;
            double v = kTxPeakSupervisory * std::sin(2.0 * kPi * kSupervisoryHz[1] * t);

            for (const auto& burst : bursts)
            {
                if (t >= burst.first && t < burst.second) {
                    v += 0.4 * std::sin(2.0 * kPi * speechHz * t);
                }
            }
            in[i] += static_cast<float>(v);
        }
    };
}

static void phoneAudio(std::vector<float>& in, size_t first)
{
    microphone({{0.0, 1e9}})(in, first);
}

// One base station with two traffic channels and the router.
struct Station
{
    modemcellular::SampleClock clock;
    NmtSystem nmt;
    modemcellular::CellularSystem cellular;
    std::unique_ptr<NmtCellularBridge> bridge;
    NmtChannel* a;
    NmtChannel* b;
    NmtChannel* cc;

    Station(bool callingChannel = false) :
        clock(kRate), nmt(clock), cellular(clock, modemcellular::ToneSet::Denmark), a(nullptr), b(nullptr), cc(nullptr)
    {
        ChannelConfig c = config(System::Nmt450, ChannelType::Tc, 100);
        c.supervisory = 2;
        a = &nmt.addChannel(c, kRate, 1.0);
        c.channel = 101;
        b = &nmt.addChannel(c, kRate, 1.0);
        if (callingChannel) {
            cc = &nmt.addChannel(config(System::Nmt450, ChannelType::Cc, 50), kRate, 1.0);
        }
        bridge = std::make_unique<NmtCellularBridge>(nmt, cellular);
    }

    // The phone roams in on a channel.
    void roam(NmtChannel& channel, const Phone& phone)
    {
        seize(channel, phone);
        pull(channel);
        pull(channel);
        phoneSends(channel, lineSignalFrame(channel, phone, 0x11111), 1);
        CHECK(channel.state() == NmtChannel::State::Idle);
    }

    // The phone dials; the call is connected on the channel (signalling only).
    void callFrom(NmtChannel& channel, const Phone& phone, const std::string& number)
    {
        toDialTone(channel, phone);
        dial(channel, phone, number);
        phoneSends(channel, idleFromPhone(), 1);
        for (int i = 0; i < 8; i++) {
            pull(channel);
        }
        FrameBits bits;
        CHECK(!channel.nextFrame(bits));
        CHECK(channel.state() == NmtChannel::State::Active);
    }

    // Runs the radio for some seconds. The phone sends what the function adds to the silence
    // (given the sample number); without a function it talks.
    std::vector<float> air(double seconds, NmtChannel& channel,
                           const std::function<void(std::vector<float>&, size_t)>& audio = phoneAudio)
    {
        const int block = static_cast<int>(kRate / 100.0);
        std::vector<float> in(static_cast<size_t>(block)), out(static_cast<size_t>(block)), sent;
        const size_t blocks = static_cast<size_t>(seconds * 100.0 + 0.5);

        for (size_t k = 0; k < blocks; k++)
        {
            std::fill(in.begin(), in.end(), 0.0f);
            if (audio) {
                audio(in, m_samples);
            }
            channel.receive(in.data(), block);
            channel.render(out.data(), block, false);
            sent.insert(sent.end(), out.begin(), out.end());
            m_samples += static_cast<size_t>(block);
            clock.advance(block);
            nmt.run();
            if (bridge) {
                bridge->run();
            }
        }
        return sent;
    }

    // Two channels at once; the result is what each of them sent.
    std::pair<std::vector<float>, std::vector<float>> airBoth(
        double seconds, NmtChannel& first, const std::function<void(std::vector<float>&, size_t)>& audioFirst,
        NmtChannel& second, const std::function<void(std::vector<float>&, size_t)>& audioSecond)
    {
        const int block = static_cast<int>(kRate / 100.0);
        std::vector<float> in(static_cast<size_t>(block)), out(static_cast<size_t>(block));
        std::pair<std::vector<float>, std::vector<float>> sent;
        const size_t blocks = static_cast<size_t>(seconds * 100.0 + 0.5);

        for (size_t k = 0; k < blocks; k++)
        {
            for (int which = 0; which < 2; which++)
            {
                NmtChannel& channel = which ? second : first;
                const auto& audio = which ? audioSecond : audioFirst;

                std::fill(in.begin(), in.end(), 0.0f);
                if (audio) {
                    audio(in, m_samples);
                }
                channel.receive(in.data(), block);
                channel.render(out.data(), block, false);
                (which ? sent.second : sent.first).insert((which ? sent.second : sent.first).end(), out.begin(), out.end());
            }
            m_samples += static_cast<size_t>(block);
            clock.advance(block);
            nmt.run();
            if (bridge) {
                bridge->run();
            }
        }
        return sent;
    }

    size_t m_samples = 0;
};

static void testRoamingRegisters()
{
    Station s;
    const Phone phone;

    CHECK(!s.cellular.isRegistered("1123456"));
    s.roam(*s.a, phone);
    CHECK(s.cellular.isRegistered("1123456"));
    CHECK(s.nmt.isRegistered('1', "123456"));
}

static void testCallToEcho()
{
    Station s;
    const Phone phone;

    s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
    s.cellular.setAnswerDelay(0.5);
    s.roam(*s.a, phone);
    s.callFrom(*s.a, phone, "5551234");
    CHECK(s.cellular.callCount() == 1);

    // The phone speaks twice: 0.1 s to 0.4 s, before the router connects the call at 0.5 s,
    // and 1.6 s to 2.6 s. The first burst is dropped by the router; the second comes back one
    // second later (the echo queue holds one second of silence).
    const std::vector<float> sent = s.air(8.0, *s.a, microphone({{0.1, 0.4}, {1.6, 2.6}}));

    CHECK(s.a->state() == NmtChannel::State::Active);
    CHECK(s.a->supervisoryDetected());

    auto slot = [&](double from, double to, double hz) {
        return toneAmplitude(sent, static_cast<size_t>(kRate * from), static_cast<size_t>(kRate * to), hz, kRate);
    };

    CHECK(slot(0.0, 2.5, 1000.0) < 0.01);                  // nothing of the first burst, nothing of the second yet
    CHECK(slot(0.0, 2.5, 425.0) < 0.01);                   // no ringback for an endpoint
    CHECK(std::fabs(slot(2.8, 3.4, 1000.0) - 0.4) < 0.02); // the second burst, one second late
    CHECK(slot(3.8, 8.0, 1000.0) < 0.01);

    // the phone clears: the call in the router ends
    phoneSends(*s.a, lineSignalFrame(*s.a, phone, 0x11111), 1);
    CHECK(s.cellular.callCount() == 0);
    for (int i = 0; i < 4; i++) {
        pull(*s.a);
    }
    CHECK(s.a->state() == NmtChannel::State::Idle);
}

static void testCallToMusic()
{
    Station s;
    const Phone phone;

    s.cellular.setFallback(modemcellular::CallRouter::Fallback::Music);
    s.cellular.setAnswerDelay(0.2);
    s.callFrom(*s.a, phone, "99");             // the phone never roamed: the call works anyway
    CHECK(!s.cellular.isRegistered("1123456"));

    const std::vector<float> sent = s.air(5.0, *s.a);
    auto slot = [&](double from, double to, double hz) {
        return toneAmplitude(sent, static_cast<size_t>(kRate * from), static_cast<size_t>(kRate * to), hz, kRate);
    };

    // the first note, 523.25 Hz at 0.3, starts when the call is answered (0.2 s)
    CHECK(slot(0.0, 0.18, 523.25) < 0.01);
    CHECK(std::fabs(slot(0.25, 0.45, 523.25) - 0.3) < 0.02);
    CHECK(std::fabs(slot(0.55, 0.75, 659.25) - 0.3) < 0.02);       // 0.2 s + 0.3 s later
    CHECK(slot(0.25, 0.45, 1000.0) < 0.01);                         // not the phone's own voice

    // and it goes on
    double energy = 0.0;
    for (size_t i = static_cast<size_t>(kRate * 2.0); i < sent.size(); i++) {
        energy += static_cast<double>(sent[i]) * sent[i];
    }
    energy /= static_cast<double>(sent.size() - static_cast<size_t>(kRate * 2.0));
    CHECK(energy > 0.02 && energy < 0.06);                          // 0.3^2 / 2 * 6/7 = 0.039
    CHECK(s.a->state() == NmtChannel::State::Active);
}

// A phone calls another phone that is registered: the second phone cannot be paged yet, the
// caller hears the congestion tone and is released after the tone timeout.
static void testCallToRegisteredPhone()
{
    Station s;
    const Phone first;
    Phone second;
    second.number = "654321";

    s.cellular.setToneTimeout(2.0);
    s.roam(*s.a, first);
    s.roam(*s.b, second);
    CHECK(s.cellular.isRegistered("1654321"));

    s.callFrom(*s.a, first, "1654321");
    CHECK(s.cellular.callCount() == 1);

    // The station has no calling channel, so the second phone cannot be paged: no channel at
    // once, the congestion tone of Denmark (425 Hz, 0.25 s on and 0.25 s off) for the tone
    // timeout of 2 s.
    std::vector<float> sent = s.air(1.9, *s.a);
    CHECK(s.a->state() == NmtChannel::State::Active);
    CHECK(s.cellular.callCount() == 1);
    auto level = [&](double from, double to) {
        // rectangular window power -> amplitude, the supervisory tone is at 4 kHz and small
        double sum = 0.0;
        const size_t a = static_cast<size_t>(kRate * from), b = static_cast<size_t>(kRate * to);
        for (size_t i = a; i < b; i++) {
            sum += static_cast<double>(sent[i]) * sent[i];
        }
        return std::sqrt(2.0 * sum / static_cast<double>(b - a));
    };
    // the tone starts in the first block
    CHECK(std::fabs(level(0.05, 0.22) - 0.25) < 0.03);              // on
    CHECK(level(0.30, 0.47) < 0.05);                                // off
    CHECK(std::fabs(level(0.55, 0.72) - 0.25) < 0.03);              // on again

    s.air(0.2, *s.a);                                               // 2.1 s: tone timeout of 2 s
    CHECK(s.a->state() == NmtChannel::State::MtRelease);
    CHECK(s.cellular.callCount() == 0);
    CHECK(isLineSignal(pull(*s.a), 15));
    CHECK(s.b->state() == NmtChannel::State::Idle);                 // the other phone never rang

    phoneSends(*s.a, lineSignalFrame(*s.a, first, 0x11111), 1);
    CHECK(s.a->state() == NmtChannel::State::Idle);
}

// The router refuses the call: the phone is released.
static void testRouterRefuses()
{
    Station s;
    const Phone phone;

    s.cellular.setFallbackFactory([]() { return std::unique_ptr<modemcellular::AudioEndpoint>(); });
    toDialTone(*s.a, phone);
    dial(*s.a, phone, "99");
    phoneSends(*s.a, idleFromPhone(), 1);

    CHECK(s.cellular.callCount() == 0);
    CHECK(s.a->state() == NmtChannel::State::MtRelease);
    CHECK(s.a->dspMode() == DspMode::Frame);
    CHECK(isLineSignal(pull(*s.a), 15));
    phoneSends(*s.a, lineSignalFrame(*s.a, phone, 0x11111), 1);
    CHECK(s.a->state() == NmtChannel::State::Idle);
}

// A phone that dials its own number hears the busy tone, then is released.
static void testCallToSelf()
{
    Station s;
    const Phone phone;

    s.cellular.setToneTimeout(2.0);
    s.roam(*s.a, phone);
    s.callFrom(*s.a, phone, "1123456");
    const std::vector<float> sent = s.air(1.9, *s.a);
    CHECK(s.a->state() == NmtChannel::State::Active);
    CHECK(toneAmplitude(sent, static_cast<size_t>(kRate * 0.05), static_cast<size_t>(kRate * 0.45), 425.0, kRate) > 0.05);
    s.air(0.2, *s.a);
    CHECK(s.a->state() == NmtChannel::State::MtRelease);
    CHECK(s.cellular.callCount() == 0);
    CHECK(s.cellular.isRegistered("1123456"));
}

// Roaming again, on the other channel, and when the bridge is made later.
static void testRegistrationLifetime()
{
    const Phone phone;

    {
        Station s;
        s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
        s.cellular.setAnswerDelay(0.2);
        s.roam(*s.a, phone);
        s.roam(*s.b, phone);
        CHECK(s.cellular.isRegistered("1123456"));
        s.callFrom(*s.b, phone, "5551234");
        const std::vector<float> sent = s.air(4.0, *s.b);
        CHECK(std::fabs(toneAmplitude(sent, static_cast<size_t>(kRate * 2.5), sent.size(), 1000.0, kRate) - 0.4) < 0.02);
    }

    // phones that roamed in before the bridge existed
    {
        Station s;
        s.roam(*s.a, phone);
        CHECK(s.cellular.isRegistered("1123456"));
        s.bridge.reset();
        CHECK(!s.cellular.isRegistered("1123456"));
        s.bridge = std::make_unique<NmtCellularBridge>(s.nmt, s.cellular);
        CHECK(s.cellular.isRegistered("1123456"));
    }
}

// One phone calls another: the second phone is paged, rings, answers, and they talk.
static void testPhoneToPhone()
{
    Station s(true);
    Phone caller, callee;
    callee.number = "654321";

    s.roam(*s.a, caller);
    s.roam(*s.b, callee);

    // the caller dials; the call is in the router and the callee is paged on the calling channel
    s.callFrom(*s.a, caller, "1654321");
    s.air(0.05, *s.a, microphone({}));
    CHECK(s.cellular.callCount() == 1);
    CHECK(s.cc->state() == NmtChannel::State::MtPaging);
    CHECK(pull(*s.cc).mt == MessageType::Frame2a);

    // the callee answers the page and is moved to the free traffic channel (b, as a has a call)
    Frame ack = seizureFrame(*s.cc, callee, MessageType::Frame10a);
    ack.msPassword = 0;
    phoneSends(*s.cc, ack, 1);
    const Frame allocation = pull(*s.cc);
    CHECK(allocation.mt == MessageType::Frame2b);
    CHECK(allocation.tcNo == encodeTc(System::Nmt450, 101, 1));
    CHECK(s.b->state() == NmtChannel::State::MtIdent);

    // the callee identifies itself and rings: the caller hears the ringback tone of Denmark
    phoneSends(*s.b, seizureFrame(*s.b, callee, MessageType::Frame10b), 3);
    CHECK(s.b->state() == NmtChannel::State::MtRinging);
    auto both = s.airBoth(0.9, *s.a, microphone({}), *s.b, microphone({}));
    CHECK(toneAmplitude(both.first, static_cast<size_t>(kRate * 0.1), static_cast<size_t>(kRate * 0.9), 425.0, kRate) > 0.1);

    // the callee answers
    phoneSends(*s.b, lineSignalFrame(*s.b, callee, 0xeeeee), 1);
    CHECK(s.b->state() == NmtChannel::State::MtComplete);
    s.airBoth(1.0, *s.a, microphone({}), *s.b, microphone({}));       // 4 frames of 138 ms, then audio
    CHECK(s.b->state() == NmtChannel::State::Active);
    CHECK(s.a->state() == NmtChannel::State::Active);

    // they talk: the caller at 1000 Hz from 0.5 s, the callee at 700 Hz from 1.5 s, one second each
    const double start = static_cast<double>(s.m_samples) / kRate;
    both = s.airBoth(3.0, *s.a, microphone({{start + 0.5, start + 1.5}}, 1000.0),
                     *s.b, microphone({{start + 1.5, start + 2.5}}, 700.0));
    auto slot = [&](const std::vector<float>& x, double from, double to, double hz) {
        return toneAmplitude(x, static_cast<size_t>(kRate * from), static_cast<size_t>(kRate * to), hz, kRate);
    };

    CHECK(slot(both.second, 0.0, 0.45, 1000.0) < 0.01);                        // not before the caller speaks
    CHECK(std::fabs(slot(both.second, 0.7, 1.3, 1000.0) - 0.4) < 0.03);        // the callee hears the caller
    CHECK(slot(both.second, 0.7, 1.3, 700.0) < 0.01);
    CHECK(std::fabs(slot(both.first, 1.7, 2.3, 700.0) - 0.4) < 0.03);          // the caller hears the callee
    CHECK(slot(both.first, 1.7, 2.3, 1000.0) < 0.01);
    CHECK(slot(both.first, 0.0, 0.4, 700.0) < 0.01);                           // and not itself
    CHECK(s.a->supervisoryDetected() && s.b->supervisoryDetected());

    // the callee hangs up: the caller is released
    phoneSends(*s.b, lineSignalFrame(*s.b, callee, 0x11111), 1);
    CHECK(s.cellular.callCount() == 0);
    s.airBoth(0.1, *s.a, microphone({}), *s.b, microphone({}));
    CHECK(s.a->state() == NmtChannel::State::MtRelease);
    CHECK(s.b->state() == NmtChannel::State::MoRelease);
}

// The callee does not answer the page: the caller hears congestion and is released.
static void testCalleeDoesNotAnswer()
{
    Station s(true);
    Phone caller, callee;
    callee.number = "654321";

    s.cellular.setToneTimeout(2.0);
    s.roam(*s.a, caller);
    s.roam(*s.b, callee);
    s.callFrom(*s.a, caller, "1654321");

    s.air(3.2, *s.a, microphone({}));               // three pages of 1 s, then the congestion tone
    CHECK(s.a->state() == NmtChannel::State::Active);
    CHECK(s.cc->state() == NmtChannel::State::Idle);
    s.air(2.1, *s.a, microphone({}));               // the tone timeout of 2 s
    CHECK(s.a->state() == NmtChannel::State::MtRelease);
    CHECK(s.cellular.callCount() == 0);
    CHECK(s.nmt.transactionCount() == 1);           // the caller's, until it answers the release
}

static void testBridgeGoesAway()
{
    const Phone phone;

    // the bridge is gone while a call is up: the router forgets the leg and the phone is
    // released
    {
        Station s;
        s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
        s.roam(*s.a, phone);
        s.callFrom(*s.a, phone, "5551234");
        CHECK(s.cellular.callCount() == 1);
        s.bridge.reset();
        CHECK(s.cellular.callCount() == 0);
        CHECK(!s.cellular.isRegistered("1123456"));
        CHECK(s.a->state() == NmtChannel::State::MtRelease);
        CHECK(isLineSignal(pull(*s.a), 15));
        phoneSends(*s.a, lineSignalFrame(*s.a, phone, 0x11111), 1);
        CHECK(s.a->state() == NmtChannel::State::Idle);
    }

    // the whole station goes away during a call (the bridge is declared last, so it goes first)
    {
        Station s;
        s.roam(*s.a, phone);
        s.callFrom(*s.a, phone, "5551234");
        s.air(0.5, *s.a);
    }
}

// The same phone calls twice; the second call is not mixed up with the first.
static void testTwoCallsInARow()
{
    Station s;
    const Phone phone;

    s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
    s.cellular.setAnswerDelay(0.2);
    s.roam(*s.a, phone);

    for (int round = 0; round < 2; round++)
    {
        s.callFrom(*s.a, phone, "5551234");
        CHECK(s.cellular.callCount() == 1);
        // a burst of speech 0.1 s to 0.5 s into the call; the echo of the new call starts silent
        // and brings it back a second later
        const size_t start = s.m_samples;
        const double begin = static_cast<double>(start) / kRate;
        const std::vector<float> sent = s.air(3.0, *s.a, microphone({{begin + 0.1, begin + 0.5}}));
        auto slot = [&](double from, double to) {
            return toneAmplitude(sent, static_cast<size_t>(kRate * from), static_cast<size_t>(kRate * to), 1000.0, kRate);
        };
        CHECK(slot(0.0, 1.0) < 0.02);
        CHECK(std::fabs(slot(1.3, 1.5) - 0.4) < 0.03);
        CHECK(s.a->state() == NmtChannel::State::Active);
        phoneSends(*s.a, lineSignalFrame(*s.a, phone, 0x11111), 1);
        CHECK(s.cellular.callCount() == 0);
        for (int i = 0; i < 4; i++) {
            pull(*s.a);
        }
        CHECK(s.a->state() == NmtChannel::State::Idle);
    }
}

// A connected call is switched to another traffic channel: the router never notices, the
// speech goes on on the new channel, and the phone can end the call there.
static void testMovedCall()
{
    Station s;
    const Phone phone;

    s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
    s.cellular.setAnswerDelay(0.1);
    s.roam(*s.a, phone);
    s.callFrom(*s.a, phone, "5551234");
    s.airBoth(1.0, *s.a, microphone({}), *s.b, nullptr);
    CHECK(s.cellular.callCount() == 1);

    // the order goes out for a while, then the phone is on the new channel
    CHECK(s.nmt.moveCall(phone.country, phone.number));
    s.airBoth(0.3, *s.a, microphone({}), *s.b, microphone({}));
    CHECK(s.a->state() == NmtChannel::State::ScipOrder);
    phoneSends(*s.b, seizureFrame(*s.b, phone, MessageType::Frame10b), 1);
    CHECK(s.b->state() == NmtChannel::State::Active);
    CHECK(s.a->state() == NmtChannel::State::Idle);
    CHECK(s.cellular.callCount() == 1);

    // the phone speaks on the new channel from 0.5 s to 1.5 s and hears itself one second later
    const double start = static_cast<double>(s.m_samples) / kRate;
    auto out = s.airBoth(4.0, *s.a, nullptr, *s.b, microphone({{start + 0.5, start + 1.5}}));
    auto slot = [&](double from, double to, double hz) {
        return toneAmplitude(out.second, static_cast<size_t>(kRate * from), static_cast<size_t>(kRate * to), hz, kRate);
    };

    CHECK(slot(0.0, 1.4, 1000.0) < 0.01);                      // nothing before the speech comes back
    CHECK(std::fabs(slot(1.7, 2.3, 1000.0) - 0.4) < 0.03);     // the echo
    CHECK(s.b->supervisoryDetected());
    CHECK(s.cellular.callCount() == 1);

    // the old channel is free for the next phone
    CHECK(pull(*s.a).mt == MessageType::Frame4);

    // the phone clears on the new channel
    phoneSends(*s.b, lineSignalFrame(*s.b, phone, 0x11111), 1);
    CHECK(s.cellular.callCount() == 0);
    for (int i = 0; i < 4; i++) {
        pull(*s.b);
    }
    CHECK(s.b->state() == NmtChannel::State::Idle);
    CHECK(s.nmt.transactionCount() == 0);
}

// The far end hangs up while the phone is on its way.
static void testFarEndReleasesDuringMove()
{
    Station s(true);
    Phone caller, callee;
    callee.number = "654321";

    s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
    s.roam(*s.a, caller);
    s.callFrom(*s.a, caller, "5551234");
    s.airBoth(1.0, *s.a, microphone({}), *s.b, nullptr);
    CHECK(s.nmt.moveCall(caller.country, caller.number));
    s.airBoth(0.2, *s.a, microphone({}), *s.b, microphone({}));

    // the router ends the call
    CHECK(s.nmt.releaseCall(caller.country, caller.number));
    CHECK(s.a->state() == NmtChannel::State::MtRelease);
    CHECK(s.b->state() == NmtChannel::State::ScipRelease);
    for (int i = 0; i < 4; i++) {
        CHECK(isLineSignal(pull(*s.b), 15));
    }
    CHECK(s.b->state() == NmtChannel::State::Idle);
    phoneSends(*s.a, lineSignalFrame(*s.a, caller, 0x11111), 1);
    CHECK(s.a->state() == NmtChannel::State::Idle);
    CHECK(s.nmt.transactionCount() == 0);
}

// The digits a phone sends to the MFT converter reach the far end as push-button tones.
static void testMftDigitsAsTones()
{
    Station s;
    const Phone phone;

    s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
    s.cellular.setAnswerDelay(0.1);
    s.roam(*s.a, phone);
    s.callFrom(*s.a, phone, "5551234");
    s.air(1.0, *s.a, microphone({}));

    // the converter: the request, the acknowledgement frames, then the audio path stays
    phoneSends(*s.a, lineSignalFrame(*s.a, phone, 0x88888), 1);
    s.air(1.5, *s.a, microphone({}));
    CHECK(s.a->state() == NmtChannel::State::Mft);
    CHECK(s.cellular.callCount() == 1);

    // key 5 and then #: two frames each, the second one in the other kind of frame
    phoneSends(*s.a, digitFrame(*s.a, phone, '5', true), 1);
    phoneSends(*s.a, digitFrame(*s.a, phone, '5', true), 1);
    s.air(0.3, *s.a, microphone({}));
    phoneSends(*s.a, digitFrame(*s.a, phone, '#', false), 1);
    phoneSends(*s.a, digitFrame(*s.a, phone, '#', false), 1);

    // the echo of the router sends them back a second later: 770+1336 Hz, then 941+1477 Hz
    const std::vector<float> sent = s.air(3.0, *s.a, microphone({}));
    bool five = false, hash = false;

    for (size_t from = 0; from + 800 <= sent.size(); from += 400)
    {
        const size_t to = from + 800;

        five = five || (toneAmplitude(sent, from, to, 770.0, kRate) > 0.05 && toneAmplitude(sent, from, to, 1336.0, kRate) > 0.05);
        hash = hash || (toneAmplitude(sent, from, to, 941.0, kRate) > 0.05 && toneAmplitude(sent, from, to, 1477.0, kRate) > 0.05);
    }
    CHECK(five && hash);

    // after the converter the speech is the speech again
    phoneSends(*s.a, lineSignalFrame(*s.a, phone, 0x77777), 1);
    s.air(1.5, *s.a, microphone({}));
    CHECK(s.a->state() == NmtChannel::State::Active);
    const double start = static_cast<double>(s.m_samples) / kRate;
    const std::vector<float> speech = s.air(4.0, *s.a, microphone({{start + 0.2, start + 1.2}}));
    CHECK(std::fabs(toneAmplitude(speech, static_cast<size_t>(kRate * 1.4), static_cast<size_t>(kRate * 2.0), 1000.0, kRate) - 0.4) < 0.05);
}

int main()
{
    testMftDigitsAsTones();
    testMovedCall();
    testFarEndReleasesDuringMove();
    testRoamingRegisters();
    testCallToEcho();
    testCallToMusic();
    testCallToRegisteredPhone();
    testPhoneToPhone();
    testCalleeDoesNotAnswer();
    testRouterRefuses();
    testCallToSelf();
    testRegistrationLifetime();
    testBridgeGoesAway();
    testTwoCallsInARow();

    if (g_failures == 0) {
        std::printf("all bridge tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
