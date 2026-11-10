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

// The bits a side sends, at most `limit` of them
static std::vector<uint8_t> drain(NmtDmsLink& link, int limit = 2000)
{
    std::vector<uint8_t> out;
    int bit;

    while (out.size() < static_cast<size_t>(limit) && link.pullBit(bit)) {
        out.push_back(static_cast<uint8_t>(bit));
    }

    return out;
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
    // the identity frame carries the id 3 and zeros
    CHECK(p.b.peerIdentity() == std::vector<uint8_t>({3, 0, 0, 0, 0, 0, 0}));

    // the answering side used the other direction bit: the first label of its bits has d = 1
    CHECK(p.bBits.size() > 40 && p.bBits[26] == 1);
    CHECK(p.aBits[26] == 0);
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

    a.startAsCaller();
    const std::vector<uint8_t> echoed = drain(a, 4 * 127);
    CHECK(echoed.size() == 4 * 127);
    feed(a, echoed);
    CHECK(a.framesReceived() == 0);

    // a frame from the other side is taken
    const uint8_t data[8] = {73, '1', '2', '3', '4', '5', '6', '7'};

    feed(a, NmtDmsLink::encodeDt(true, true, 0, data, true));
    CHECK(a.framesReceived() == 1);
    CHECK(a.peerIdentity() == std::vector<uint8_t>({'1', '2', '3', '4', '5', '6', '7'}));
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
    p.run(2000, [&] { return p.a.unacknowledged() == 0; });
    CHECK(p.a.unacknowledged() == 0);

    // a full receiver (NR): the data wait until an RR or a DT comes, at most T = 3 s (Figure 1)
    std::vector<uint8_t> data(21, 0x55);                        // three frames, numbers 2, 3, 4

    p.a.queueData(data.data(), data.size());
    CHECK(p.a.unacknowledged() == 3);
    feed(p.a, NmtDmsLink::encodeRr(true, true, false, 2));      // NR(2) acknowledges frame 1: nothing
    CHECK(p.a.peerBusy());
    CHECK(p.a.unacknowledged() == 3);

    int bit;
    int drained = 0;

    // the frame that is going out is finished, then nothing until T is over
    while (p.a.pullBit(bit) && drained < 400) {
        drained++;
    }
    CHECK(drained < 400);
    p.a.advance(2.9);
    CHECK(!p.a.pullBit(bit));
    CHECK(p.a.peerBusy());
    p.a.advance(0.2);
    CHECK(!p.a.peerBusy());
    CHECK(p.a.pullBit(bit));
    while (p.a.pullBit(bit)) {
    }

    // an NR acknowledges like an RR; every NR starts T again
    feed(p.a, NmtDmsLink::encodeRr(true, true, false, 3));      // frame 2
    CHECK(p.a.peerBusy() && p.a.unacknowledged() == 2);
    CHECK(!p.a.pullBit(bit));
    p.a.advance(2.0);
    feed(p.a, NmtDmsLink::encodeRr(true, true, false, 3));
    p.a.advance(2.0);
    CHECK(p.a.peerBusy());

    // any RR clears it, also one that acknowledges nothing new
    feed(p.a, NmtDmsLink::encodeRr(true, false, false, 3));
    CHECK(!p.a.peerBusy() && p.a.unacknowledged() == 2);
    CHECK(p.a.pullBit(bit));
    while (p.a.pullBit(bit)) {
    }

    // a DT of the other side clears it too
    feed(p.a, NmtDmsLink::encodeRr(true, true, false, 3));
    CHECK(p.a.peerBusy());
    const uint8_t words[8] = {0x41, 0, 0, 0, 0, 0, 0, 0};
    feed(p.a, NmtDmsLink::encodeDt(true, false, p.a.nextRx(), words, true));
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
    const std::vector<uint8_t> bits = drain(a, 8 * 127);
    // the two control frames go round (identity, random number), the data frame is not among them
    CHECK(bits.size() == 8 * 127);
    for (size_t frame = 0; frame < 8; frame++)
    {
        const size_t at = frame * 127;

        CHECK(text(bits, at, 15) == "101010101010101");
        CHECK(text(bits, at + 15, 11) == "00101000111");
        CHECK(text(bits, at + 26, 7) == ((frame & 1) ? "0011001" : "0011000"));    // d=0 s=0 pp=11 n=0 / n=1
    }
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

// An RR(n) acknowledges the frame n-1 if it is one of the first four frames of the list, with the
// frames before it, whether or not they went out. Any other number acknowledges nothing.
static void testAcknowledgementWindow()
{
    NmtDmsLink a;
    std::vector<uint8_t> d(7 * 20, 0x41);

    a.startAsCaller();
    CHECK(a.unacknowledged() == 2);
    feed(a, NmtDmsLink::encodeRr(true, false, false, 1));          // acknowledges the identity, not sent yet
    CHECK(a.unacknowledged() == 1);
    const uint8_t rand[8] = {82, 0, 1, 0, 2, 0, 3, '8'};
    feed(a, NmtDmsLink::encodeDt(true, true, 0, rand, true));      // the peer's random number
    CHECK(a.established());
    feed(a, NmtDmsLink::encodeRr(true, false, false, 2));
    CHECK(a.unacknowledged() == 0);

    CHECK(a.queueData(d.data(), d.size()));
    CHECK(a.unacknowledged() == 20);                               // numbers 2, 3, 4, 5, 6, 7, 0, 1, ...
    feed(a, NmtDmsLink::encodeRr(true, false, false, 2));          // frame 1: not in the list
    CHECK(a.unacknowledged() == 20);
    feed(a, NmtDmsLink::encodeRr(true, false, false, 3));          // frame 2, the first
    CHECK(a.unacknowledged() == 19);
    feed(a, NmtDmsLink::encodeRr(true, false, false, 7));          // frame 6 is now the fourth
    CHECK(a.unacknowledged() == 15);
    // RR(5) is NTX-2: the peer wants frame 5 again, which is gone: numbered again from 5
    feed(a, NmtDmsLink::encodeRr(true, false, false, 5));
    CHECK(a.unacknowledged() == 15 && a.nextTx() == ((5 + 15) & 7));
    feed(a, NmtDmsLink::encodeRr(true, false, false, 6));          // the first, now frame 5
    CHECK(a.unacknowledged() == 14);
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

// The format follows the random numbers received.
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
        // every random number received sets the format: 8 bits if its last byte is '8'. The answer
        // to a random number always asks for 8 bits.
        CHECK(p.a.eightBits());
        CHECK(p.b.eightBits() == (variant == 0));
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
    drain(a, 2 * 127);
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
    drain(a, 2 * 127);
    std::vector<uint8_t> rr = NmtDmsLink::encodeRr(true, false, false, 2);
    rr[30] ^= 1;
    feed(a, rr);
    CHECK(a.unacknowledged() == 0 && a.framesReceived() == 1);
}

// Frames that are not acknowledged go round without a pause, each with its bit sync; the data frames
// after the random numbers too. From the fourth repetition every frame is followed by 81 ms
// without FFSK (Annex 21 para 6.5) until something is acknowledged.
static void testNoPause()
{
    NmtDmsLink a;
    int bit;

    a.startAsCaller();
    // identity and random number: sent once and repeated three times back to back, then the
    // first frame of the fourth repetition and the gap
    const std::vector<uint8_t> bits = drain(a, 20 * 127);
    CHECK(bits.size() == 9 * 127);
    for (int frame = 0; frame < 9; frame++) {
        CHECK(text(bits, static_cast<size_t>(frame) * 127, 15) == "101010101010101");
    }
    a.advance(0.080);
    CHECK(!a.pullBit(bit));
    a.advance(0.002);
    CHECK(a.pullBit(bit));
    CHECK(drain(a, 20 * 127).size() == 127 - 1);
    a.advance(0.081);
    CHECK(drain(a, 20 * 127).size() == 127);

    // an acknowledgement: the repetitions count again
    feed(a, NmtDmsLink::encodeRr(true, false, false, 1));
    CHECK(a.unacknowledged() == 1);
    a.advance(0.081);
    CHECK(drain(a, 20 * 127).size() == 5 * 127);

    // the data frames of an established link: 4 frames in the window, each with a bit sync
    NmtDmsLink b;
    const uint8_t rand[8] = {82, 0, 1, 0, 2, 0, 3, '8'};
    const std::vector<uint8_t> data(7 * 6, 0x41);

    b.startAsCaller();
    feed(b, NmtDmsLink::encodeDt(true, true, 0, rand, true));
    feed(b, NmtDmsLink::encodeRr(true, false, false, 2));
    CHECK(b.established() && b.unacknowledged() == 0);
    b.queueData(data.data(), data.size());
    drain(b, 77);                                                   // the RR for the peer's random number
    const std::vector<uint8_t> frames = drain(b, 9 * 127);
    CHECK(frames.size() == 9 * 127);
    for (size_t frame = 0; frame < 9; frame++)
    {
        CHECK(text(frames, frame * 127, 15) == "101010101010101");
        CHECK(text(frames, frame * 127 + 15, 11) == "00101000111");
        CHECK(text(frames, frame * 127 + 26, 7) == std::string("0111") + (frame % 4 == 0 ? "010" : frame % 4 == 1 ? "011" : frame % 4 == 2 ? "100" : "101"));
    }
}

// An RR for a frame that is gone (NTX-1 to NTX-3): the frames are numbered again from it
// (Annex 21 Figure 1, note 2). The speech is closed 200 ms from the start of a frame.
static void testRestartAndSpeech()
{
    NmtDmsLink b;
    const uint8_t rand[8] = {82, 0, 1, 0, 2, 0, 3, '8'};
    const std::vector<uint8_t> data(7, 0x41);
    int bit;

    b.startAsCaller();
    feed(b, NmtDmsLink::encodeDt(true, true, 0, rand, true));
    feed(b, NmtDmsLink::encodeRr(true, false, false, 2));
    CHECK(b.established() && b.unacknowledged() == 0 && b.nextTx() == 2);
    b.queueData(data.data(), data.size());
    drain(b, 77);                                                   // the RR for the peer's random number

    // RR(4) refers to the frames under transmission: nothing changes
    feed(b, NmtDmsLink::encodeRr(true, false, false, 4));
    std::vector<uint8_t> frame = drain(b, 127);
    CHECK(text(frame, 26, 7) == "0111010");
    CHECK(b.nextTx() == 3);

    // RR(7) is before the frame (NTX-3): it is sent again as frame 7
    feed(b, NmtDmsLink::encodeRr(true, false, false, 7));
    CHECK(b.unacknowledged() == 1 && b.nextTx() == 0);
    while (b.pullBit(bit)) {
    }
    b.advance(1.0);
    frame = drain(b, 127);
    CHECK(text(frame, 26, 7) == "0111111");
    feed(b, NmtDmsLink::encodeRr(true, false, false, 0));
    CHECK(b.unacknowledged() == 0);

    // a frame that goes out, or comes in, closes the speech for 200 ms
    NmtDmsLink a;

    CHECK(!a.speechClosed());
    a.startAsCaller();
    CHECK(a.pullBit(bit) && a.speechClosed());
    a.advance(0.199);
    CHECK(a.speechClosed());
    a.advance(0.002);
    CHECK(!a.speechClosed());
    NmtDmsLink c;
    feed(c, NmtDmsLink::encodeRr(true, false, false, 0));
    CHECK(c.speechClosed());
    c.advance(0.2);
    CHECK(!c.speechClosed());
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

static void testPendingAndMasked()
{
    NmtDmsLink b;
    const uint8_t data[] = {'a', 'b', 'c'};

    CHECK(b.queueData(data, sizeof(data)));
    CHECK(!b.idle() && b.pendingBytes() == 3);
    CHECK(!b.queueData(data, static_cast<size_t>(-1)));
    std::vector<uint8_t> big(65536, 1);
    NmtDmsLink c;
    CHECK(c.queueData(big.data(), big.size()));
    CHECK(!c.queueData(big.data(), 1));

    // a caller that asks for 7 bits sends 7 bits: the eighth bits are lost, and the link says how many
    NmtDmsLink a7(false), b8(true);
    Pair p;
    std::vector<uint8_t> got;
    bool done = false;

    std::swap(p.a, a7);
    std::swap(p.b, b8);
    p.b.setDataHandler([&](const std::vector<uint8_t>& dd) { got.insert(got.end(), dd.begin(), dd.end()); });
    p.a.setAllSentHandler([&] { done = true; });
    p.a.startAsCaller();
    const uint8_t mixed[] = {0xc1, 0xc2, 0x41};
    p.a.queueData(mixed, sizeof(mixed));
    p.run(8000, [&] { return done; });
    CHECK(done);
    CHECK(got == std::vector<uint8_t>({0x41, 0x42, 0x41}));
    CHECK(p.a.maskedBytes() == 2);
}

// A receiver that was full says when it can take data again.
static void testReadyAgain()
{
    NmtDmsLink b;
    const uint8_t data[8] = {0x41, 0x42, 0, 0, 0, 0, 0, 0};

    feed(b, NmtDmsLink::encodeDt(false, false, 0, data, true));
    drain(b);
    b.setReady(false);
    feed(b, NmtDmsLink::encodeDt(false, false, 1, data, true));
    drain(b);                                                      // NR(1)
    b.setReady(true);
    std::vector<uint8_t> out = drain(b);
    CHECK(out.size() == 77 && text(out, 26, 7) == "1101001");     // RR(1)
}

// The first frame needs no history of good bits; the minimum quality cannot be NaN; the 1 1 after
// every 7 bits are not looked at.
static void testQualityStart()
{
    const uint8_t id[8] = {73, '1', '1', '2', '3', '4', '5', '6'};
    const std::vector<uint8_t> frame = NmtDmsLink::encodeDt(false, true, 0, id, false);
    NmtDmsLink b;

    feed(b, frame, 0.9);
    CHECK(b.framesReceived() == 1);

    NmtDmsLink c;
    c.setMinQuality(std::nan(""));
    feed(c, frame, 0.1);
    CHECK(c.framesReceived() == 1);                                // NaN means no minimum

    std::vector<uint8_t> bad = NmtDmsLink::encodeDt(false, true, 0, id, true);
    bad[15 + 11 + 7] = 0;
    NmtDmsLink d;
    feed(d, bad);
    CHECK(d.framesReceived() == 1 && d.framesRejected() == 0);
}

// A callback that resets the link ends the handling of that frame.
static void testResetInCallback()
{
    Pair p;
    int controls = 0;

    p.a.setControlHandler([&](uint8_t, const uint8_t*) { controls++; });
    bool once = true;

    p.a.setEstablishedHandler([&] {
        if (once)
        {
            once = false;
            p.a.reset();
        }
    });
    p.a.startAsCaller();
    p.run(3000);                                        // no crash, no frame of the old session handled after the reset
    CHECK(!once && controls <= 1);

    // a second message from the end-of-message callback
    Pair q;
    int sent = 0;
    std::vector<uint8_t> got;
    const uint8_t data[] = {'x', 'y', 'z', 'w', 'v', 'u', 't', 's', 'r', 'q'};

    q.b.setDataHandler([&](const std::vector<uint8_t>& dd) { got.insert(got.end(), dd.begin(), dd.end()); });
    q.a.setAllSentHandler([&] {
        if (++sent == 1) {
            q.a.queueData(data, sizeof(data));
        }
    });
    q.a.startAsCaller();
    q.a.queueData(data, sizeof(data));
    q.run(20000, [&] { return sent == 2; });
    CHECK(sent == 2 && got.size() == 20);
}

// A random number from the peer is answered once with a random number that asks for 8 bits, whatever
// it asked for, and data may go at once, before the answer is acknowledged.
static void testPeerRandom()
{
    NmtDmsLink b(true);
    const uint8_t id[8] = {73, 3, 0, 0, 0, 0, 0, 0};
    const uint8_t rand7[8] = {82, 0, 1, 0, 2, 0, 3, '7'};

    feed(b, NmtDmsLink::encodeDt(false, true, 0, id, true));
    CHECK(!b.established());
    feed(b, NmtDmsLink::encodeDt(false, true, 1, rand7, true));
    CHECK(b.established() && !b.eightBits());
    feed(b, NmtDmsLink::encodeDt(false, true, 1, rand7, true));    // again: no second answer
    CHECK(b.unacknowledged() == 1);

    const uint8_t data[] = {'a', 'b', 'c'};

    CHECK(b.queueData(data, sizeof(data)));
    CHECK(b.unacknowledged() == 2);
    const std::vector<uint8_t> bits = drain(b, 77 + 2 * 127);
    CHECK(bits.size() == 77 + 2 * 127);
    CHECK(text(bits, 26, 7) == "1101010");                          // RR(2), d = 1
    CHECK(text(bits, 77 + 26, 7) == "1011000");                     // the random number, s = 0, n = 0
    CHECK(text(bits, 77 + 26 + 9 * 8, 7) == "0111000");             // '8'
    CHECK(text(bits, 77 + 127 + 26, 7) == "1111001");               // the data, s = 1, n = 1

    // a random number that asks for 8 bits sets 8 bits, one that asks for 7 sets 7 bits
    const uint8_t rand8[8] = {82, 0, 1, 0, 2, 0, 3, '8'};
    NmtDmsLink c(false);

    feed(c, NmtDmsLink::encodeDt(false, true, 0, rand8, true));
    CHECK(c.eightBits());
    feed(c, NmtDmsLink::encodeDt(false, true, 1, rand7, true));
    CHECK(!c.eightBits());
    feed(c, NmtDmsLink::encodeDt(false, true, 2, rand8, true));
    CHECK(c.eightBits());
}

int main()
{
    testPeerRandom();
    testAcknowledgementWindow();
    testResponderQueuesFirst();
    testZeros();
    testFormatNegotiation();
    testDuplicatesAndStart();
    testNotReadyAndPositiveResponse();
    testRecovery();
    testNoPause();
    testRestartAndSpeech();
    testBothCall();
    testPendingAndMasked();
    testReadyAgain();
    testQualityStart();
    testResetInCallback();
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
