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

// Test helpers: a phone that talks to a channel at frame level.

#ifndef MODEMNMT_TESTS_PHONE_H_
#define MODEMNMT_TESTS_PHONE_H_

#include <cstdio>
#include <string>
#include <vector>

#include "clock.h"
#include "nmtsystem.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kRate = 48000.0;

[[maybe_unused]] static ChannelConfig config(System system, ChannelType type, int channel)
{
    ChannelConfig c;

    c.system = system;
    c.type = type;
    c.channel = channel;
    c.msPower = 1;
    c.trafficArea = 0x11;
    return c;
}

// The frame the base station sends next.
static Frame pull(NmtChannel& channel)
{
    FrameBits bits;
    EncodedBits encoded;

    CHECK(channel.nextFrame(bits));
    for (int i = 0; i < kEncodedBits; i++) {
        encoded[static_cast<size_t>(i)] = bits[static_cast<size_t>(kBitSyncBits + kFrameSyncBits + i)];
    }

    return decodeFrame(encoded, Direction::MtxToMs, false);
}

[[maybe_unused]] static bool isIdleCallingFrame(const Frame& f)
{
    return f.mt == MessageType::Frame1a || f.mt == MessageType::Frame1aA || f.mt == MessageType::Frame1aB
        || f.mt == MessageType::Frame1b;
}

// The next carousel frame of an idle calling channel: its idle frame comes in every other slot, so
// that one is skipped (at most one; the idle frame comes back if nothing is pending).
[[maybe_unused]] static Frame pullCalling(NmtChannel& channel)
{
    const Frame f = pull(channel);

    return isIdleCallingFrame(f) ? pull(channel) : f;
}

// A frame from the phone, as the modem would deliver it.
static void phoneSends(NmtChannel& channel, const Frame& frame, uint64_t framesElapsed)
{
    const FrameBits bits = encodeFrame(frame);
    RxFrame rx;

    for (int i = 0; i < kEncodedBits; i++) {
        rx.bits[static_cast<size_t>(i)] = bits[static_cast<size_t>(kBitSyncBits + kFrameSyncBits + i)];
    }
    rx.quality = 1.0;
    rx.level = 1.0;
    rx.framesElapsed = framesElapsed;
    rx.leadingZeros = true;
    channel.receiveFrame(rx);
}

struct Phone
{
    char country = '1';
    std::string number = "123456";
    std::string password = "789";
};

// Frames 11a, 10b and 13a of a phone that is tuned to the channel.
static Frame seizureFrame(const NmtChannel& channel, const Phone& phone, MessageType type)
{
    const ChannelConfig& c = channel.config();
    Frame f;

    f.mt = type;
    f.channelNo = static_cast<uint16_t>(encodeChannel(c.system, c.channel, c.msPower));
    f.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, phone.country)));
    f.msNumber = static_cast<uint32_t>(digitsToValue(phone.number));
    f.msPassword = static_cast<uint16_t>(digitsToValue(phone.password));
    return f;
}

[[maybe_unused]] static Frame roamingFrame(const NmtChannel& channel, const Phone& phone)
{
    return seizureFrame(channel, phone, MessageType::Frame11a);
}

[[maybe_unused]] static Frame lineSignalFrame(const NmtChannel& channel, const Phone& phone, uint32_t lineSignal)
{
    Frame f = seizureFrame(channel, phone, MessageType::Frame13a);

    f.msPassword = 0;
    f.lineSignal = lineSignal;
    return f;
}

[[maybe_unused]] static bool isLineSignal(const Frame& f, int signal)
{
    return f.mt == MessageType::Frame5a && f.lineSignal == static_cast<uint32_t>((signal << 8) | (signal << 4) | signal);
}

// Walks one channel through the roaming seizure up to the confirmation.
[[maybe_unused]] static void seize(NmtChannel& channel, const Phone& phone)
{
    phoneSends(channel, roamingFrame(channel, phone), 40);      // the first frame after a gap
    phoneSends(channel, roamingFrame(channel, phone), 1);
    phoneSends(channel, roamingFrame(channel, phone), 1);
}


// ---- calls from the phone ----

using State = NmtChannel::State;

[[maybe_unused]] static Frame callFrame(const NmtChannel& channel, const Phone& phone)
{
    return seizureFrame(channel, phone, MessageType::Frame10b);
}

// 14a carries the 1st, 3rd ... digit with 00 in front, 14b the 2nd, 4th ... with FF.
[[maybe_unused]] static Frame digitFrame(const NmtChannel& channel, const Phone& phone, char digit, bool odd)
{
    const std::string keys = "D1234567890*#ABC";
    const uint32_t value = static_cast<uint32_t>(keys.find(digit));
    Frame f = seizureFrame(channel, phone, odd ? MessageType::Frame14a : MessageType::Frame14b);

    f.msPassword = 0;
    f.digit = (odd ? 0x00000u : 0xff000u) | (value << 8) | (value << 4) | value;
    return f;
}

[[maybe_unused]] static Frame idleFromPhone()
{
    Frame f;

    f.mt = MessageType::Frame15;
    return f;
}

// The phone sends the digits as the standard does: every digit for two frame periods.
[[maybe_unused]] static void dial(NmtChannel& channel, const Phone& phone, const std::string& digits)
{
    bool odd = (channel.dialled().size() % 2) == 0;       // the next digit is odd if an even number is there

    for (char digit : digits)
    {
        phoneSends(channel, digitFrame(channel, phone, digit, odd), 1);
        phoneSends(channel, digitFrame(channel, phone, digit, odd), 1);
        odd = !odd;
    }
}

struct Calls
{
    std::vector<std::string> dialled;
    std::vector<NmtSubscriber> subscribers;
    int cleared = 0;
    bool accept = true;
};

[[maybe_unused]] static void connect(NmtSystem& system, Calls& calls)
{
    system.setCallHandler([&calls](const NmtSubscriber& s, const std::string& dialled) {
        calls.subscribers.push_back(s);
        calls.dialled.push_back(dialled);
        return calls.accept;
    });
    system.setClearHandler([&calls](const NmtSubscriber&) { calls.cleared++; });
}

// Seizure and identity of a call, up to the dial tone.
[[maybe_unused]] static void toDialTone(NmtChannel& tc, const Phone& phone, MessageType seizure = MessageType::Frame10b)
{
    phoneSends(tc, seizureFrame(tc, phone, seizure), 40);
    CHECK(tc.state() == State::MoIdent);
    for (int i = 0; i < 2; i++) {
        CHECK(pull(tc).mt == MessageType::Frame3b);
    }
    phoneSends(tc, seizureFrame(tc, phone, seizure), 1);
    phoneSends(tc, seizureFrame(tc, phone, seizure), 1);
    CHECK(tc.state() == State::MoConfirm);

    CHECK(isLineSignal(pull(tc), 3));
    CHECK(isLineSignal(pull(tc), 3));
    FrameBits bits;
    CHECK(!tc.nextFrame(bits));                 // the third frame is replaced by the dial tone
    CHECK(tc.state() == State::MoDialling);
    CHECK(tc.dspMode() == DspMode::DialTone);
}

#endif // MODEMNMT_TESTS_PHONE_H_
