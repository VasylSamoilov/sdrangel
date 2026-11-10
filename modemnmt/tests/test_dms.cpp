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

// The data link of the data and message service.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "nmtdms.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static std::string text(const std::vector<uint8_t>& bits, size_t from, size_t count)
{
    std::string s;

    for (size_t i = from; i < from + count; i++) {
        s.push_back(static_cast<char>('0' + bits[i]));
    }

    return s;
}

static void feed(NmtDmsLink& link, const std::vector<uint8_t>& bits, double quality = 1.0)
{
    for (uint8_t bit : bits) {
        link.pushBit(bit, quality, 1.0);
    }
}

// Two links with a wire in each direction, one bit of each per step, with optional bit errors.
struct Pair
{
    NmtDmsLink a;
    NmtDmsLink b;
    uint32_t seed = 12345;
    double errorRate = 0.0;             // probability of a flipped bit
    bool silent = false;                // the wire from b to a is cut
    std::vector<uint8_t> aBits;         // what a sent
    std::vector<uint8_t> bBits;

    explicit Pair(bool eight = true) : a(eight), b(eight) {}

    bool flip()
    {
        seed = seed * 1664525u + 1013904223u;
        return errorRate > 0.0 && ((seed >> 8) & 0xffff) < static_cast<uint32_t>(errorRate * 65536.0);
    }

    // returns the number of steps
    int run(int steps, const std::function<bool()>& done = nullptr)
    {
        int i;

        for (i = 0; i < steps; i++)
        {
            int bit;

            if (a.pullBit(bit))
            {
                aBits.push_back(static_cast<uint8_t>(bit));
                b.pushBit(flip() ? 1 - bit : bit, 1.0, 1.0);
            }
            if (b.pullBit(bit))
            {
                bBits.push_back(static_cast<uint8_t>(bit));
                if (!silent) {
                    a.pushBit(flip() ? 1 - bit : bit, 1.0, 1.0);
                }
            }
            a.advance(1.0 / 1200.0);
            b.advance(1.0 / 1200.0);
            if (done && done()) {
                break;
            }
        }

        return i;
    }
};

static void testCrcAndFrames()
{
    // CRC-16 with the polynomial x16 + x12 + x5 + 1, start value 0: the check value of "123456789"
    uint8_t bits[72];
    const char* msg = "123456789";

    for (int i = 0; i < 9; i++)
    {
        for (int j = 0; j < 8; j++) {
            bits[i * 8 + j] = static_cast<uint8_t>((msg[i] >> (7 - j)) & 1);
        }
    }
    CHECK(NmtDmsLink::crc16(bits, 72) == 0x31c3);
    CHECK(NmtDmsLink::crc16(bits, 0) == 0);

    // a DT frame: bit sync, frame sync, label, 8 words, CRC, a 1 1 after every 7 bits
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<uint8_t> dt = NmtDmsLink::encodeDt(false, false, 5, data, true);

    CHECK(dt.size() == 127);
    CHECK(text(dt, 0, 15) == "101010101010101");
    CHECK(text(dt, 15, 11) == "00101000111");
    CHECK(text(dt, 26, 7) == "0111101");                   // d=0 s=1 pp=11 n=101
    CHECK(text(dt, 33, 2) == "11");
    CHECK(text(dt, 35, 7) == "0000001");                   // the first word
    CHECK(text(dt, 42, 2) == "11");
    CHECK(NmtDmsLink::encodeDt(false, false, 5, data, false).size() == 112);

    // control frame: s = 0, direction bit 1
    const std::vector<uint8_t> ct = NmtDmsLink::encodeDt(true, true, 0, data, false);
    CHECK(text(ct, 11, 7) == "1011000");                   // d=1 s=0 pp=11 n=000

    // RR: the sync and the label twice, parity
    const std::vector<uint8_t> rr = NmtDmsLink::encodeRr(true, false, false, 3);
    CHECK(rr.size() == 77);
    CHECK(text(rr, 15, 11) == "00101000111");
    CHECK(text(rr, 26, 7) == "1101011");                   // d=1 s=1 pp=01 n=011
    CHECK(text(rr, 33, 2) == "11");
    CHECK(text(rr, 35, 7) == "1101011");
    CHECK(text(rr, 44, 2) == "11");                        // five ones in the label: odd, parity 1
    CHECK(text(rr, 46, 11) == "00101000111");
    CHECK(text(rr, 57, 20) == text(rr, 26, 20));

    // NR has pp = 10
    CHECK(text(NmtDmsLink::encodeRr(false, true, false, 0), 26, 7) == "0110000");
}

static void testHandshakeAndData()
{
    Pair p;
    std::vector<std::vector<uint8_t>> received;
    bool established = false;
    bool allSent = false;

    p.b.setDataHandler([&](const std::vector<uint8_t>& d) { received.push_back(d); });
    p.a.setEstablishedHandler([&] { established = true; });
    p.a.setAllSentHandler([&] { allSent = true; });
    p.a.setIdentity("1123456");

    // 20 bytes with bytes above 127 and a zero inside
    std::vector<uint8_t> data;

    for (int i = 0; i < 20; i++) {
        data.push_back(static_cast<uint8_t>(i == 5 ? 0 : 0x7a + i * 7));
    }
    p.a.startAsCaller();
    p.a.queueData(data.data(), data.size());
    CHECK(p.a.unacknowledged() == 2);                       // identity, random number: the data wait for them

    p.run(6000, [&] { return allSent; });
    CHECK(established);
    CHECK(allSent);
    CHECK(p.b.established());
    CHECK(!p.b.callerSide());
    p.run(400);                                         // the frame that was going out finishes
    CHECK(p.a.idle());

    std::vector<uint8_t> all;

    for (const auto& d : received) {
        all.insert(all.end(), d.begin(), d.end());
    }
    CHECK(all == data);
    CHECK(p.b.peerIdentity().size() == 7 && p.b.peerIdentity()[0] == '1' && p.b.peerIdentity()[6] == '6');

    // the answering side used the other direction bit: the first label of its bits has d = 1
    CHECK(p.bBits.size() > 40 && p.bBits[26] == 1);
    CHECK(p.aBits[26] == 0);
    // data frames follow each other without a second bit sync (112 bits), the first has one
    CHECK(p.a.framesRejected() == 0 && p.b.framesRejected() == 0);

    // the other way round: the answering side sends
    received.clear();
    std::vector<uint8_t> back(15, 0x41);

    p.b.queueData(back.data(), back.size());
    allSent = false;
    p.b.setAllSentHandler([&] { allSent = true; });
    std::vector<uint8_t> got;

    p.a.setDataHandler([&](const std::vector<uint8_t>& d) { got.insert(got.end(), d.begin(), d.end()); });
    p.run(6000, [&] { return allSent; });
    CHECK(allSent);
    CHECK(got == back);
}

static void testSevenBit()
{
    Pair p(false);
    std::vector<uint8_t> got;
    bool done = false;

    p.b.setDataHandler([&](const std::vector<uint8_t>& d) { got.insert(got.end(), d.begin(), d.end()); });
    p.a.setAllSentHandler([&] { done = true; });
    p.a.startAsCaller();

    const std::string message = "HELLO WORLD, 7 BIT TEXT";

    p.a.queueData(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    p.run(6000, [&] { return done; });
    CHECK(done && !p.b.eightBits());
    CHECK(std::string(got.begin(), got.end()) == message);
}

static void testZeroAtTheEndOfAFrame()
{
    Pair p;
    std::vector<uint8_t> got;
    bool done = false;

    p.b.setDataHandler([&](const std::vector<uint8_t>& d) { got.insert(got.end(), d.begin(), d.end()); });
    p.a.setAllSentHandler([&] { done = true; });
    p.a.startAsCaller();

    // the 7th byte is zero: it must not end a frame (it would be taken for filling)
    const uint8_t data[] = {1, 2, 3, 4, 5, 6, 0, 8, 9};

    p.a.queueData(data, sizeof(data));
    CHECK(p.a.unacknowledged() == 2);
    p.run(6000, [&] { return done; });
    CHECK(done);
    CHECK(got == std::vector<uint8_t>(data, data + sizeof(data)));
}

static void testErrorsAreRepeated()
{
    // bit errors: the CRC drops the frames, they are sent again
    for (int round = 0; round < 4; round++)
    {
        Pair p;
        std::vector<uint8_t> got;
        bool done = false;

        p.seed = 777u + static_cast<uint32_t>(round) * 31u;
        p.errorRate = 0.002;
        p.b.setDataHandler([&](const std::vector<uint8_t>& d) { got.insert(got.end(), d.begin(), d.end()); });
        p.a.setAllSentHandler([&] { done = true; });
        p.a.startAsCaller();

        std::vector<uint8_t> data;

        for (int i = 0; i < 100; i++) {
            data.push_back(static_cast<uint8_t>(i * 13 + 1));
        }
        p.a.queueData(data.data(), data.size());
        p.run(60000, [&] { return done; });
        CHECK(done);
        CHECK(got == data);
        CHECK(p.a.framesRejected() + p.b.framesRejected() > 0);
    }
}

static void testSequenceWraps()
{
    Pair p;
    std::vector<uint8_t> got;
    bool done = false;

    p.b.setDataHandler([&](const std::vector<uint8_t>& d) { got.insert(got.end(), d.begin(), d.end()); });
    p.a.setAllSentHandler([&] { done = true; });
    p.a.startAsCaller();

    // 300 bytes: 43 frames, the numbers go round five times
    std::vector<uint8_t> data;

    for (int i = 0; i < 300; i++) {
        data.push_back(static_cast<uint8_t>((i * 37 + 11) | 1));
    }
    p.a.queueData(data.data(), data.size());
    p.run(200000, [&] { return done; });
    CHECK(done);
    CHECK(got == data);
}

static void testOwnFramesAreIgnored()
{
    // a frame with the direction bit of this side (an echo) is not taken
    NmtDmsLink a;
    int bit;
    std::vector<uint8_t> echoed;

    a.startAsCaller();
    while (a.pullBit(bit)) {
        echoed.push_back(static_cast<uint8_t>(bit));
    }
    CHECK(!echoed.empty());
    feed(a, echoed);
    CHECK(a.framesReceived() == 0);

    // a frame from the other side is taken
    const uint8_t data[8] = {73, '1', '2', '3', '4', '5', '6', '7'};

    feed(a, NmtDmsLink::encodeDt(true, true, 0, data, true));
    CHECK(a.framesReceived() == 1);
    CHECK(a.peerIdentity().size() == 7);
}

static void testQualityAndGarbage()
{
    NmtDmsLink b;
    const uint8_t data[8] = {73, '1', '2', '3', '4', '5', '6', '7'};
    std::vector<uint8_t> frame = NmtDmsLink::encodeDt(false, true, 0, data, true);

    // a weak signal does not take the sync
    feed(b, frame, 0.3);
    CHECK(b.framesReceived() == 0);

    // one bit wrong in the data: the CRC says no
    std::vector<uint8_t> bad = frame;
    bad[40] ^= 1;
    feed(b, bad);
    CHECK(b.framesReceived() == 0 && b.framesRejected() == 1);

    // a label that means nothing (pp = 00)
    std::vector<uint8_t> nothing = frame;
    nothing[26 + 2] = 0;
    nothing[26 + 3] = 0;
    feed(b, nothing);
    CHECK(b.framesReceived() == 0 && b.framesRejected() == 2);

    // noise before the frame does not matter
    std::vector<uint8_t> noise(300);

    for (size_t i = 0; i < noise.size(); i++) {
        noise[i] = static_cast<uint8_t>((i * 7 + (i >> 3)) & 1);
    }
    feed(b, noise);
    feed(b, frame);
    CHECK(b.framesReceived() == 1);
}

static void testBusyReceiver()
{
    Pair p;
    bool done = false;
    std::vector<uint8_t> got;

    p.b.setDataHandler([&](const std::vector<uint8_t>& d) { got.insert(got.end(), d.begin(), d.end()); });
    p.a.startAsCaller();
    p.a.setEstablishedHandler([&] { done = true; });
    p.run(6000, [&] { return done; });
    CHECK(done);

    // a full receiver (NR): the data wait, up to 3 s
    std::vector<uint8_t> data(14, 0x55);

    p.a.queueData(data.data(), data.size());
    feed(p.a, NmtDmsLink::encodeRr(true, true, false, 2));
    CHECK(p.a.peerBusy());

    int bit;
    int drained = 0;

    // the frame that is going out is finished, then nothing
    while (p.a.pullBit(bit) && drained < 400) {
        drained++;
    }
    CHECK(drained < 400);
    p.a.advance(0.5);
    CHECK(!p.a.pullBit(bit));
    p.a.advance(2.6);
    CHECK(!p.a.peerBusy());
    CHECK(p.a.pullBit(bit));

    // and an RR clears it at once
    feed(p.a, NmtDmsLink::encodeRr(true, true, false, 2));
    CHECK(p.a.peerBusy());
    feed(p.a, NmtDmsLink::encodeRr(true, false, false, 2));
    CHECK(!p.a.peerBusy());
}

static void testNoDataBeforeTheRandomNumbers()
{
    // the answering side has no direction and sends nothing until it heard the caller
    NmtDmsLink b;
    int bit;

    b.queueData(reinterpret_cast<const uint8_t*>("abc"), 3);
    CHECK(!b.pullBit(bit));

    // the caller sends identity and random number; the data wait for the acknowledgement
    NmtDmsLink a;

    a.startAsCaller();
    a.queueData(reinterpret_cast<const uint8_t*>("abc"), 3);
    int frames = 0;
    std::vector<uint8_t> bits;

    while (a.pullBit(bit) && bits.size() < 5000) {
        bits.push_back(static_cast<uint8_t>(bit));
    }
    // two control frames go round (identity, random number), the data frame is not among them
    CHECK(!bits.empty());
    for (size_t i = 0; i + 11 <= bits.size(); i++)
    {
        if (text(bits, i, 11) == "00101000111") {
            frames++;
        }
    }
    CHECK(frames == 4);                                     // two passes of the two frames, then the pause
    CHECK(bits.size() == 127 + 112 + 112 + 112);
}

static void testResetAndLeaks()
{
    NmtDmsLink a;
    std::vector<uint8_t> data(70, 9);

    a.startAsCaller();
    a.queueData(data.data(), data.size());
    CHECK(a.unacknowledged() == 2);
    a.reset();
    CHECK(a.unacknowledged() == 0 && !a.callerSide() && !a.established());
    int bit;
    CHECK(!a.pullBit(bit));
}

// Wire that carries what the test builds, for one side
static std::vector<uint8_t> drain(NmtDmsLink& link, int limit = 2000)
{
    std::vector<uint8_t> out;
    int bit;

    while (out.size() < static_cast<size_t>(limit) && link.pullBit(bit)) {
        out.push_back(static_cast<uint8_t>(bit));
    }

    return out;
}

// An RR that is old or beyond the window acknowledges nothing; only the frames that went out count.
static void testAcknowledgementWindow()
{
    NmtDmsLink a;
    std::vector<uint8_t> d(7 * 20, 0x41);

    a.startAsCaller();
    // the random numbers are exchanged
    const uint8_t id[8] = {82, 0, 1, 0, 2, 0, 3, '8'};
    feed(a, NmtDmsLink::encodeRr(true, false, false, 1));          // nothing sent yet: nothing acknowledged
    CHECK(a.unacknowledged() == 2);
    drain(a, 127 + 112);                                           // both control frames went out
    feed(a, NmtDmsLink::encodeRr(true, false, false, 1));
    CHECK(a.unacknowledged() == 1);
    feed(a, NmtDmsLink::encodeDt(true, true, 0, id, true));        // the peer's random number
    feed(a, NmtDmsLink::encodeRr(true, false, false, 2));
    CHECK(a.unacknowledged() == 0 && a.established());

    CHECK(a.queueData(d.data(), d.size()));
    CHECK(a.unacknowledged() == 20);
    drain(a, 4 * 112 + 20);                                        // the window of four went out
    feed(a, NmtDmsLink::encodeRr(true, false, false, 1));          // old: the frames before
    CHECK(a.unacknowledged() == 20);
    feed(a, NmtDmsLink::encodeRr(true, false, false, 7));          // beyond the window
    CHECK(a.unacknowledged() == 20);
    feed(a, NmtDmsLink::encodeRr(true, false, false, 6));          // the window
    CHECK(a.unacknowledged() == 16);
}

// The answering side may queue before it heard anything; its random number is not behind data.
static void testResponderQueuesFirst()
{
    Pair p;
    std::vector<uint8_t> got;
    bool done = false;
    const uint8_t data[] = {'a', 'b', 'c'};

    CHECK(p.b.queueData(data, sizeof(data)));
    p.a.setDataHandler([&](const std::vector<uint8_t>& dd) { got.insert(got.end(), dd.begin(), dd.end()); });
    p.b.setAllSentHandler([&] { done = true; });
    p.a.startAsCaller();
    p.run(8000, [&] { return done; });
    CHECK(done);
    CHECK(p.b.established() && p.a.established());
    CHECK(got == std::vector<uint8_t>(data, data + 3));
}

// Zero bytes: a frame of nothing else is one zero; none is lost.
static void testZeros()
{
    const std::vector<std::vector<uint8_t>> messages = {
        {0x41, 0x00, 0x00},
        {1, 0, 0, 0, 0, 0, 0, 0, 9},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5},
        {0x80, 0x00, 0x80},
        {0x00},
    };

    for (const auto& message : messages)
    {
        for (int eight = 0; eight < 2; eight++)
        {
            Pair p(eight != 0);
            std::vector<uint8_t> got;
            bool done = false;

            p.b.setDataHandler([&](const std::vector<uint8_t>& dd) { got.insert(got.end(), dd.begin(), dd.end()); });
            p.a.setAllSentHandler([&] { done = true; });
            p.a.startAsCaller();
            p.a.queueData(message.data(), message.size());
            p.run(8000, [&] { return done; });
            CHECK(done);
            if (eight) {
                CHECK(got == message);
            } else
            {
                std::vector<uint8_t> low;
                for (uint8_t b : message) {
                    low.push_back(b & 0x7f);
                }
                CHECK(got == low);
                if (got != low)
                {
                    std::fprintf(stderr, "message of %zu bytes: got %zu bytes\n", message.size(), got.size());
                }
            }
        }
    }
}

// The format follows the random numbers: a side that asks for 7 bits gets 7 bits both ways.
static void testFormatNegotiation()
{
    for (int variant = 0; variant < 2; variant++)
    {
        NmtDmsLink a(variant == 0), b(variant != 0);
        Pair p;
        std::vector<uint8_t> got;
        bool done = false;

        // the Pair has equal links; use the two above through references
        std::swap(p.a, a);
        std::swap(p.b, b);
        p.a.setDataHandler([&](const std::vector<uint8_t>& dd) { got.insert(got.end(), dd.begin(), dd.end()); });
        p.b.setDataHandler([&](const std::vector<uint8_t>& dd) { got.insert(got.end(), dd.begin(), dd.end()); });
        p.a.setAllSentHandler([&] { done = true; });
        p.a.startAsCaller();

        const std::string message = "HELLO WORLD";

        p.a.queueData(reinterpret_cast<const uint8_t*>(message.data()), message.size());
        p.run(8000, [&] { return done; });
        CHECK(done);
        CHECK(std::string(got.begin(), got.end()) == message);
        CHECK(!p.a.eightBits() && !p.b.eightBits());
    }
}

// The same frame twice is taken once and answered once; a first frame that is not number 0 is not taken.
static void testDuplicatesAndStart()
{
    NmtDmsLink b;
    const uint8_t id[8] = {73, '1', '1', '2', '3', '4', '5', '6'};

    feed(b, NmtDmsLink::encodeDt(false, true, 1, id, true));
    CHECK(b.nextRx() == 0 && b.peerIdentity().empty());
    std::vector<uint8_t> out = drain(b);
    CHECK(text(out, 26, 7) == "1101000");                          // RR(0), d = 1

    feed(b, NmtDmsLink::encodeDt(false, true, 0, id, true));
    feed(b, NmtDmsLink::encodeDt(false, true, 0, id, false));      // again
    CHECK(b.nextRx() == 1);
    int controls = 0;
    // one RR for both
    out = drain(b);
    CHECK(out.size() == 77);
    (void) controls;
}

// A receiver that is not ready answers NR; an RR with s = 0 is the positive response.
static void testNotReadyAndPositiveResponse()
{
    NmtDmsLink b;
    const uint8_t data[8] = {0x41, 0x42, 0, 0, 0, 0, 0, 0};

    feed(b, NmtDmsLink::encodeDt(false, false, 0, data, true));
    drain(b);
    b.setReady(false);
    feed(b, NmtDmsLink::encodeDt(false, false, 1, data, true));
    CHECK(b.nextRx() == 1);
    std::vector<uint8_t> out = drain(b);
    CHECK(text(out, 26, 7) == "1110001");                          // NR(1): d=1 s=1 pp=10 n=001
    b.setReady(true);

    NmtDmsLink a;
    const uint8_t rand[8] = {82, 0, 1, 0, 2, 0, 3, '8'};

    a.startAsCaller();
    drain(a, 127 + 112);
    feed(a, NmtDmsLink::encodeDt(true, true, 0, rand, true));
    feed(a, NmtDmsLink::encodeRr(true, false, true, 2));           // s = 0
    CHECK(a.positiveResponse());
}

// A frame that is cut off does not swallow the next one; a NaN quality does not stop the receiver.
static void testRecovery()
{
    NmtDmsLink b;
    const uint8_t id[8] = {73, '1', '1', '2', '3', '4', '5', '6'};
    std::vector<uint8_t> frame = NmtDmsLink::encodeDt(false, true, 0, id, true);
    std::vector<uint8_t> cut(frame.begin(), frame.begin() + 60);

    feed(b, cut);
    feed(b, frame);
    CHECK(b.framesReceived() == 1);

    NmtDmsLink c;
    c.pushBit(0, std::nan(""), 1.0);
    feed(c, frame);
    CHECK(c.framesReceived() == 1);

    // one damaged half of an RR: the other half is taken, the acknowledgement applies once
    NmtDmsLink a;
    a.startAsCaller();
    drain(a, 127 + 112);
    std::vector<uint8_t> rr = NmtDmsLink::encodeRr(true, false, false, 2);
    rr[30] ^= 1;
    feed(a, rr);
    CHECK(a.unacknowledged() == 0 && a.framesReceived() == 1);
}

// The pause comes with a repetition, not with the first transmission; an acknowledgement ends it.
static void testPause()
{
    NmtDmsLink a;

    a.startAsCaller();
    std::vector<uint8_t> first = drain(a);
    CHECK(first.size() == 127 + 112 + 112 + 112);                   // two passes in one burst, then the pause
    int bit;
    CHECK(!a.pullBit(bit));
    a.advance(0.05);
    CHECK(!a.pullBit(bit));
    a.advance(0.04);
    CHECK(a.pullBit(bit));
}

// Two stations that both call: the first frame decides, the other one gives way.
static void testBothCall()
{
    Pair p;

    p.a.startAsCaller();
    p.b.startAsCaller();
    p.run(4000);
    CHECK(p.a.framesReceived() == 0 && p.b.framesReceived() == 0);        // each takes the other for an echo
    CHECK(!p.a.established() && !p.b.established());
}

int main()
{
    testAcknowledgementWindow();
    testResponderQueuesFirst();
    testZeros();
    testFormatNegotiation();
    testDuplicatesAndStart();
    testNotReadyAndPositiveResponse();
    testRecovery();
    testPause();
    testBothCall();
    testCrcAndFrames();
    testHandshakeAndData();
    testSevenBit();
    testZeroAtTheEndOfAFrame();
    testErrorsAreRepeated();
    testSequenceWraps();
    testOwnFramesAreIgnored();
    testQualityAndGarbage();
    testBusyReceiver();
    testNoDataBeforeTheRandomNumbers();
    testResetAndLeaks();

    if (g_failures) {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("data link tests passed\n");
    return 0;
}
