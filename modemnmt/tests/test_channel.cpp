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

#include <cstdio>
#include <vector>

#include "nmtchannel.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static ChannelConfig baseConfig(System system, ChannelType type)
{
    ChannelConfig c;

    c.system = system;
    c.type = type;
    c.channel = 100;
    c.msPower = 1;
    c.trafficArea = 0x11;       // region 1, area 1
    return c;
}

static void testFrameTypes()
{
    struct Case { System system; ChannelType type; MessageType mt; };
    const Case cases[] = {
        {System::Nmt450, ChannelType::Cc, MessageType::Frame1a},
        {System::Nmt450, ChannelType::Tc, MessageType::Frame4},
        {System::Nmt450, ChannelType::CcTc, MessageType::Frame1b},
        {System::Nmt450, ChannelType::Test, MessageType::Frame30},
        {System::Nmt900, ChannelType::Cc, MessageType::Frame1a},
        {System::Nmt900, ChannelType::Cca, MessageType::Frame1aA},
        {System::Nmt900, ChannelType::Ccb, MessageType::Frame1aB},
        {System::Nmt900, ChannelType::AcTc, MessageType::Frame4b},
    };

    for (const Case& c : cases)
    {
        NmtChannel ch(baseConfig(c.system, c.type), 48000.0, 1.0);
        CHECK(ch.valid());
        const Frame frame = ch.idleFrame();
        CHECK(frame.mt == c.mt);
        CHECK(frame.channelNo == encodeChannel(c.system, 100, 1));
        CHECK(frame.trafficArea == encodeTrafficArea(c.system, 100, 0x11));
    }

    // frames of NMT-900 only
    CHECK(!NmtChannel(baseConfig(System::Nmt450, ChannelType::Cca), 48000.0, 1.0).valid());
    CHECK(!NmtChannel(baseConfig(System::Nmt450, ChannelType::Ccb), 48000.0, 1.0).valid());
    CHECK(!NmtChannel(baseConfig(System::Nmt450, ChannelType::AcTc), 48000.0, 1.0).valid());

    ChannelConfig bad = baseConfig(System::Nmt450, ChannelType::Cc);
    bad.msPower = 4;
    CHECK(!NmtChannel(bad, 48000.0, 1.0).valid());
    bad = baseConfig(System::Nmt450, ChannelType::Cc);
    bad.batteryPeriod = 10;
    CHECK(!NmtChannel(bad, 48000.0, 1.0).valid());
    bad = baseConfig(System::Nmt450, ChannelType::Cc);
    bad.areaNo = 5;
    CHECK(!NmtChannel(bad, 48000.0, 1.0).valid());
}

static void testBatterySaving()
{
    ChannelConfig c = baseConfig(System::Nmt450, ChannelType::Cc);

    // defaults: no battery saving, all phones, overdecadic dialling allowed
    {
        NmtChannel ch(c, 48000.0, 1.0);
        CHECK(ch.idleFrame().additionalInfo == 0xeb00028000ULL);
    }

    c.batteryPeriod = 3;
    c.batteryGroup = 8;
    {
        NmtChannel ch(c, 48000.0, 1.0);
        CHECK(ch.idleFrame().additionalInfo == 0xeb30028000ULL);
    }

    // clock 13:45 = (1 << 11) | (13 << 6) | 45 = 0xb6d
    c.sendClock = true;
    {
        NmtChannel ch(c, 48000.0, 1.0);
        CHECK(ch.idleFrame().additionalInfo == 0xeb30028000ULL);    // no time source, no clock
        ch.setTimeOfDay([]() { return std::make_pair(13, 45); });
        CHECK(ch.idleFrame().additionalInfo == 0xeb30028b6dULL);
        ch.setTimeOfDay([]() { return std::make_pair(0, 0); });
        CHECK(ch.idleFrame().additionalInfo == 0xeb30028800ULL);
        ch.setTimeOfDay([]() { return std::make_pair(23, 59); });
        CHECK(ch.idleFrame().additionalInfo == 0xeb30028000ULL + ((1 << 11) | (23 << 6) | 59));
    }

    // combined channel: no clock, but the area number
    c.type = ChannelType::CcTc;
    c.areaNo = 2;
    {
        NmtChannel ch(c, 48000.0, 1.0);
        ch.setTimeOfDay([]() { return std::make_pair(13, 45); });
        CHECK(ch.idleFrame().additionalInfo == 0xeb300283f4ULL);
    }

    // group and period are 4 bits
    c = baseConfig(System::Nmt450, ChannelType::Cc);
    c.batteryPeriod = 9;
    c.batteryGroup = 1;
    CHECK(NmtChannel(c, 48000.0, 1.0).idleFrame().additionalInfo == 0xeb90021000ULL);
}

static void testAreaNumber()
{
    for (System system : {System::Nmt450, System::Nmt900})
    {
        for (ChannelType type : {ChannelType::Tc, ChannelType::Test})
        {
            ChannelConfig c = baseConfig(system, type);

            CHECK(NmtChannel(c, 48000.0, 1.0).idleFrame().additionalInfo == 0);
            c.areaNo = 3;
            const uint64_t expected = (system == System::Nmt450) ? 0x3f5 : 0x9f5;
            CHECK(NmtChannel(c, 48000.0, 1.0).idleFrame().additionalInfo == expected);
        }
    }

    ChannelConfig c = baseConfig(System::Nmt900, ChannelType::AcTc);
    c.areaNo = 1;
    CHECK(NmtChannel(c, 48000.0, 1.0).idleFrame().additionalInfo == 0x9f3);

    // not on a calling channel without battery saving frame content
    c = baseConfig(System::Nmt450, ChannelType::Cc);
    c.areaNo = 1;
    CHECK((NmtChannel(c, 48000.0, 1.0).idleFrame().additionalInfo & 0xfff) == 0);
}

static void testBandAdvertisement()
{
    ChannelConfig c = baseConfig(System::Nmt900, ChannelType::Cc);

    c.ccBandStart = 4;
    c.ccBandEnd = 124;          // limits 0x01 and 0x1f
    c.tcBandStart = 200;
    c.tcBandEnd = 990;          // limits 0x32 and 0xf7
    {
        NmtChannel ch(c, 48000.0, 1.0);
        CHECK(ch.idleFrame().additionalInfo == 0xe0011f32f7ULL);
    }

    // combined channel: no traffic channel band, no overdecadic bit, area number stays
    c.type = ChannelType::CcTc;
    c.areaNo = 4;
    {
        NmtChannel ch(c, 48000.0, 1.0);
        CHECK(ch.idleFrame().additionalInfo == 0xe0011f0000ULL + 0x9f6);
    }

    // a traffic channel band alone: no calling channel band
    c = baseConfig(System::Nmt900, ChannelType::Ccb);
    c.tcBandStart = 8;
    c.tcBandEnd = 12;
    CHECK(NmtChannel(c, 48000.0, 1.0).idleFrame().additionalInfo == 0xe000000203ULL);

    // NMT-450 has no band advertisement
    c = baseConfig(System::Nmt450, ChannelType::Cc);
    c.ccBandStart = 4;
    c.ccBandEnd = 124;
    CHECK(NmtChannel(c, 48000.0, 1.0).idleFrame().additionalInfo == 0xeb00028000ULL);

    // both wanted: they take turns, advertisement first
    c = baseConfig(System::Nmt900, ChannelType::Cc);
    c.ccBandStart = 4;
    c.ccBandEnd = 124;
    c.batteryPeriod = 5;
    {
        NmtChannel ch(c, 48000.0, 1.0);
        CHECK(ch.idleFrame().additionalInfo == 0xe0011f0000ULL);
        CHECK(ch.idleFrame().additionalInfo == 0xeb50028000ULL);
        CHECK(ch.idleFrame().additionalInfo == 0xe0011f0000ULL);
        CHECK(ch.idleFrame().additionalInfo == 0xeb50028000ULL);
        CHECK(ch.framesSent() == 4);
    }

    // advertisement only when battery saving is off
    c.batteryPeriod = 0;
    {
        NmtChannel ch(c, 48000.0, 1.0);
        for (int i = 0; i < 4; i++) {
            CHECK(ch.idleFrame().additionalInfo == 0xe0011f0000ULL);
        }
    }
}

// What the receiver of a loopback channel makes of the audio.
struct Capture
{
    std::vector<NmtChannel::ReceivedFrame> frames;
};

static void testAudioLoopback()
{
    const double fs = 48000.0;

    for (System system : {System::Nmt450, System::Nmt900})
    {
        for (ChannelType type : {ChannelType::Cc, ChannelType::Tc, ChannelType::CcTc, ChannelType::Test})
        {
            ChannelConfig c = baseConfig(system, type);
            c.loopback = true;
            c.areaNo = 2;
            c.batteryPeriod = 4;
            c.sendClock = true;

            NmtChannel ch(c, fs, 1.0);
            ch.setTimeOfDay([]() { return std::make_pair(7, 8); });
            Capture capture;
            ch.setFrameListener([&](const NmtChannel::ReceivedFrame& f) { capture.frames.push_back(f); });

            std::vector<float> audio(static_cast<size_t>(fs * 1.5));
            ch.render(audio.data(), static_cast<int>(audio.size()), false);
            ch.receive(audio.data(), static_cast<int>(audio.size()));

            // 1.5 s are 10 frames of 138.33 ms; the receiver needs the end of the last frame
            CHECK(capture.frames.size() >= 9);
            CHECK(ch.framesSent() >= 10);

            NmtChannel reference(c, fs, 1.0);
            reference.setTimeOfDay([]() { return std::make_pair(7, 8); });
            const Frame expected = reference.idleFrame();
            for (const NmtChannel::ReceivedFrame& f : capture.frames)
            {
                CHECK(f.frame.mt == expected.mt);
                CHECK(f.frame.channelNo == expected.channelNo);
                CHECK(f.frame.trafficArea == expected.trafficArea);
                CHECK(f.frame.additionalInfo == expected.additionalInfo);
                CHECK(f.quality > 0.8);
                CHECK(f.leadingZeros);
            }
            for (size_t i = 1; i < capture.frames.size(); i++) {
                CHECK(capture.frames[i].framesElapsed == 1);
            }
        }
    }
}

// Without loopback the channel decodes what a mobile station sends.
static void testReceiveDirection()
{
    ChannelConfig c = baseConfig(System::Nmt450, ChannelType::Cc);
    NmtChannel ch(c, 48000.0, 1.0);
    std::vector<NmtChannel::ReceivedFrame> got;
    ch.setFrameListener([&](const NmtChannel::ReceivedFrame& f) { got.push_back(f); });

    Frame mobile;
    mobile.mt = MessageType::Frame11a;
    mobile.channelNo = static_cast<uint16_t>(encodeChannel(System::Nmt450, 100, 1));
    mobile.trafficArea = 0x11;
    mobile.msCountry = 1;
    mobile.msNumber = 123456;

    const FrameBits bits = encodeFrame(mobile);
    RxFrame rx;
    for (int i = 0; i < kEncodedBits; i++) {
        rx.bits[static_cast<size_t>(i)] = bits[static_cast<size_t>(kBitSyncBits + kFrameSyncBits + i)];
    }
    rx.quality = 0.9;
    rx.level = 1.1;
    rx.framesElapsed = 7;
    rx.leadingZeros = true;
    ch.receiveFrame(rx);

    CHECK(got.size() == 1);
    if (got.size() == 1)
    {
        CHECK(got[0].frame.mt == MessageType::Frame11a);
        CHECK(got[0].frame.msCountry == 1);
        CHECK(got[0].frame.msNumber == 123456);
        CHECK(got[0].frame.channelNo == mobile.channelNo);
        CHECK(got[0].framesElapsed == 7);
        CHECK(got[0].leadingZeros);
        CHECK(got[0].level == 1.1);
    }

    // an invalid channel stays silent
    ChannelConfig bad = c;
    bad.msPower = 9;
    NmtChannel dead(bad, 48000.0, 1.0);
    std::vector<float> audio(4800, 5.0f);
    CHECK(dead.render(audio.data(), static_cast<int>(audio.size()), false) == 0);
    CHECK(audio[100] == 0.0f);
    int heard = 0;
    dead.setFrameListener([&](const NmtChannel::ReceivedFrame&) { heard++; });
    dead.receiveFrame(rx);
    CHECK(heard == 0);
}

int main()
{
    testFrameTypes();
    testBatterySaving();
    testAreaNumber();
    testBandAdvertisement();
    testAudioLoopback();
    testReceiveDirection();

    if (g_failures == 0) {
        std::printf("all channel tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
