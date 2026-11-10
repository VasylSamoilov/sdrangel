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

// The short message codec, against the examples of Annex 24 (pages 28 and 29).

#include <cstdio>
#include <string>
#include <vector>

#include "nmtsms.h"

using namespace modemnmt;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

typedef std::vector<uint8_t> Bytes;

static const Bytes kHello = {0xc8, 0x22, 0x93, 0xf9, 0x04};         // 'HELLO' in 7-bit characters (page 28)

static void testAlphabet()
{
    Bytes septets = smsGsmEncode("HELLO");

    CHECK(septets == Bytes({0x48, 0x45, 0x4c, 0x4c, 0x4f}));
    CHECK(smsPackSeptets(septets) == kHello);
    CHECK(smsUnpackSeptets(kHello.data(), kHello.size(), 5) == septets);
    CHECK(smsGsmDecode(septets) == "HELLO");

    // seven characters take seven octets less one
    CHECK(smsPackSeptets(smsGsmEncode("1234567")).size() == 7);
    CHECK(smsPackSeptets(smsGsmEncode("12345678")).size() == 7);
    CHECK(smsPackSeptets(smsGsmEncode("123456789")).size() == 8);

    // '@' is 0, '$' is 2, the national letters and the extension table
    CHECK(smsGsmEncode("@$") == Bytes({0x00, 0x02}));
    CHECK(smsGsmEncode("\xc3\xa4\xc3\xb6\xc3\xbc\xc3\x9f") == Bytes({0x7b, 0x7c, 0x7e, 0x1e}));       // a o u umlaut, sz
    CHECK(smsGsmEncode("[]{}\\^~|") == Bytes({0x1b, 0x3c, 0x1b, 0x3e, 0x1b, 0x28, 0x1b, 0x29, 0x1b, 0x2f, 0x1b, 0x14, 0x1b, 0x3d, 0x1b, 0x40}));
    CHECK(smsGsmEncode("\xe2\x82\xac") == Bytes({0x1b, 0x65}));                                       // euro
    CHECK(smsGsmEncode("\xe4\xb8\xad") == Bytes({0x3f}));                                              // not in the alphabet: '?'
    CHECK(smsGsmEncode("\xff") == Bytes({0x3f}));                                                      // broken UTF-8

    // every character of the table comes back
    Bytes all;

    for (int i = 0; i < 128; i++)
    {
        if (i != 0x1b) {
            all.push_back(static_cast<uint8_t>(i));
        }
    }
    CHECK(smsGsmEncode(smsGsmDecode(all)) == all);
    CHECK(smsGsmDecode(smsGsmEncode("[A\xe2\x82\xac]")) == "[A\xe2\x82\xac]");
    CHECK(smsGsmDecode(Bytes({0x1b, 0x7f})) == " ");                  // an unknown escape is a blank
    CHECK(smsGsmDecode(Bytes({0x41, 0x1b})) == "A\x1b" || smsGsmDecode(Bytes({0x41, 0x1b})).size() >= 1);

    // 160 characters in 140 octets and back
    const Bytes long160(160, 0x5a);

    CHECK(smsPackSeptets(long160).size() == 140);
    CHECK(smsUnpackSeptets(smsPackSeptets(long160).data(), 140, 160) == long160);
}

static void testAddresses()
{
    Bytes out;

    // '+35849654321': 12 semi-octets, international, digits swapped in pairs (page 28)
    CHECK(smsEncodeAddress("+35849654321", out));
    CHECK(out == Bytes({0x0c, 0x91, 0x3d, 0x85, 0x94, 0x56, 0x34, 0x12}));
    // a zero is 1010, an odd number is filled with 1111 (page 29)
    out.clear();
    CHECK(smsEncodeAddress("+358400654321", out));
    CHECK(out == Bytes({0x0d, 0x91, 0x3d, 0x85, 0xa4, 0x6a, 0x45, 0x23, 0xf1}));
    out.clear();
    CHECK(smsEncodeAddress("*#12", out));
    CHECK(out == Bytes({0x04, 0x81, 0xcb, 0x21}));

    std::string number;
    size_t used = 0;

    CHECK(smsDecodeAddress(out.data(), out.size(), number, used) == SmsParse::Ok && number == "*#12" && used == 4);
    const Bytes international = {0x0d, 0x91, 0x3d, 0x85, 0xa4, 0x6a, 0x45, 0x23, 0xf1, 0x99};
    CHECK(smsDecodeAddress(international.data(), international.size(), number, used) == SmsParse::Ok);
    CHECK(number == "+358400654321" && used == 9);
    // an international address without the plus gets it
    const Bytes noPlus = {0x02, 0x91, 0x21};
    CHECK(smsDecodeAddress(noPlus.data(), noPlus.size(), number, used) == SmsParse::Ok && number == "+12");

    // short, and not an address
    CHECK(smsDecodeAddress(international.data(), 1, number, used) == SmsParse::NeedMore);
    CHECK(smsDecodeAddress(international.data(), 5, number, used) == SmsParse::NeedMore);
    const Bytes noTypeBit = {0x02, 0x11, 0x21};
    CHECK(smsDecodeAddress(noTypeBit.data(), noTypeBit.size(), number, used) == SmsParse::Error);
    const Bytes tooLong = {0x30, 0x91, 0, 0, 0};
    CHECK(smsDecodeAddress(tooLong.data(), tooLong.size(), number, used) == SmsParse::Error);

    // what cannot be coded
    out.clear();
    CHECK(!smsEncodeAddress("", out));
    CHECK(!smsEncodeAddress("12a4", out));
    CHECK(!smsEncodeAddress("12+4", out));
    CHECK(!smsEncodeAddress(std::string(21, '1'), out));
}

static void testTime()
{
    SmsTime t;

    t.year = 96;
    t.month = 12;
    t.day = 24;
    t.hour = 13;
    t.minute = 35;
    t.second = 4;
    t.zoneQuarters = 0;
    // 24.12.96 13:35:04 (page 28): the zero of the seconds is 1010
    Bytes bytes = smsEncodeTime(t);

    CHECK(Bytes(bytes.begin(), bytes.begin() + 6) == Bytes({0x69, 0x21, 0x42, 0x31, 0x53, 0x4a}));
    CHECK(bytes[6] == 0x00);
    const SmsTime back = smsDecodeTime(bytes.data());
    CHECK(back.year == 96 && back.month == 12 && back.day == 24 && back.hour == 13 && back.minute == 35 && back.second == 4);

    // all values of the digits, with zeros; the zone with its sign in bit 3 of the first semi-octet
    for (int zone : {-47, -8, -1, 0, 1, 4, 8, 23, 48})
    {
        SmsTime u;

        u.year = 7;
        u.month = 10;
        u.day = 30;
        u.hour = 0;
        u.minute = 10;
        u.second = 59;
        u.zoneQuarters = zone;
        const Bytes b = smsEncodeTime(u);
        const SmsTime v = smsDecodeTime(b.data());

        CHECK(v.year == 7 && v.month == 10 && v.day == 30 && v.hour == 0 && v.minute == 10 && v.second == 59);
        CHECK(v.zoneQuarters == zone);
    }
    t.zoneQuarters = 8;
    CHECK(smsEncodeTime(t)[6] == 0x80);                 // two hours east: units 8 in the high nibble
    t.zoneQuarters = -8;
    CHECK(smsEncodeTime(t)[6] == 0x88);
}

static void testDeliverExample()
{
    SmsDeliver m;
    Bytes out;

    m.originator = "+35849654321";
    m.text = "HELLO";
    m.time.year = 96;
    m.time.month = 12;
    m.time.day = 24;
    m.time.hour = 13;
    m.time.minute = 35;
    m.time.second = 4;
    CHECK(smsBuildDeliver(m, 1, out));

    // RP-MT-DATA, reference 1, user data, the length of the TPDU; then page 28
    const Bytes expected = {
        0x01, 0x01, 0x41, 0x18,
        0x04,                                           // SMS-DELIVER, no more messages
        0x0c, 0x91, 0x3d, 0x85, 0x94, 0x56, 0x34, 0x12, // originator
        0x00, 0x00,                                     // protocol, coding scheme
        0x69, 0x21, 0x42, 0x31, 0x53, 0x4a, 0x00,       // time
        0x05, 0xc8, 0x22, 0x93, 0xf9, 0x04              // 5 characters
    };
    CHECK(out == expected);
    CHECK(out[3] == out.size() - 4);

    // more messages waiting: bit 2 is 0
    m.moreWaiting = true;
    CHECK(smsBuildDeliver(m, 1, out) && out[4] == 0x00);

    // the header before it
    CHECK(smsHeader() == Bytes({0x01, 0x18, 0x53, 0x4d, 0x53, 0x48, 0x18, 0x41, 0x42, 0x43, 0x02}));
    const Bytes withAddress = smsHeader("+358400654321");
    CHECK(withAddress == Bytes({0x01, 0x18, 0x53, 0x4d, 0x53, 0x48, 0x18, 0x41, 0x42, 0x43,
                                0x01, 0x41, 0x0d, 0x91, 0x3d, 0x85, 0xa4, 0x6a, 0x45, 0x23, 0xf1, 0x02}));

    // the answers to the phone
    CHECK(smsRpAckToPhone(7) == Bytes({0x03, 0x07}));
    CHECK(smsRpErrorToPhone(7, SmsCauseMemoryFull) == Bytes({0x05, 0x07, 0x42, 0x01, 0x16}));
    CHECK(smsRpNoMessages(9) == Bytes({0x07, 0x09}));

    // what does not fit
    m.text = std::string(161, 'a');
    CHECK(!smsBuildDeliver(m, 1, out));
    m.text = std::string(160, 'a');
    CHECK(smsBuildDeliver(m, 1, out) && out.size() == 4 + 1 + 8 + 2 + 7 + 1 + 140);
    m.text = std::string(80, '[');                       // 160 septets with the escapes
    CHECK(smsBuildDeliver(m, 1, out));
    m.text = std::string(81, '[');
    CHECK(!smsBuildDeliver(m, 1, out));
    m.text = "x";
    m.originator = "not a number";
    CHECK(!smsBuildDeliver(m, 1, out));
}

// Page 29: a message from the phone. The length of the TPDU in the example (19) does not match its
// octets (20); the fields tell where the message ends.
static const Bytes kSubmit = {
    0x00, 0x01,
    0x0c, 0x91, 0x3d, 0x85, 0x94, 0x21, 0x43, 0x65,     // originator +35849123456
    0x41, 0x13,
    0x11, 0x01,
    0x0d, 0x91, 0x3d, 0x85, 0xa4, 0x6a, 0x45, 0x23, 0xf1,      // destination +358400654321
    0x00, 0x00, 0x8f,
    0x05, 0xc8, 0x22, 0x93, 0xf9, 0x04
};

static void testSubmitExample()
{
    SmsFromPhone m;
    size_t used = 0;

    CHECK(smsParseFromPhone(kSubmit.data(), kSubmit.size(), m, used) == SmsParse::Ok);
    CHECK(used == kSubmit.size());
    CHECK(m.kind == SmsRpKind::Submit && m.submit.rpRef == 1 && m.rpRef == 1);
    CHECK(m.submit.originator == "+35849123456");
    CHECK(m.submit.destination == "+358400654321");
    CHECK(m.submit.messageRef == 1);
    CHECK(m.submit.text == "HELLO");
    CHECK(m.submit.dcs == 0 && m.submit.hasValidity && m.submit.validity == 0x8f);

    // more octets after it are not taken
    Bytes more = kSubmit;
    more.push_back(0x77);
    CHECK(smsParseFromPhone(more.data(), more.size(), m, used) == SmsParse::Ok && used == kSubmit.size());

    // every shorter start needs more
    for (size_t n = 0; n < kSubmit.size(); n++) {
        CHECK(smsParseFromPhone(kSubmit.data(), n, m, used) == SmsParse::NeedMore);
    }

    // without a validity period, with an absolute one, with the other coding schemes
    Bytes noVp = kSubmit;
    noVp[12] = 0x01;                                    // SUBMIT without validity period
    noVp.erase(noVp.begin() + 25);                      // the validity octet
    CHECK(smsParseFromPhone(noVp.data(), noVp.size(), m, used) == SmsParse::Ok);
    CHECK(!m.submit.hasValidity && m.submit.text == "HELLO");

    Bytes absolute = kSubmit;
    absolute[12] = 0x19;                                // VPF 11: seven octets
    absolute.insert(absolute.begin() + 26, 6, 0x00);
    CHECK(smsParseFromPhone(absolute.data(), absolute.size(), m, used) == SmsParse::Ok && m.submit.text == "HELLO");

    Bytes eight = kSubmit;
    eight[24] = 0x30;                                   // a national set: 5 characters in 5 octets
    eight.resize(27);
    for (uint8_t c : {'H', 'E', 'L', 'L', 'O'}) {
        eight.push_back(c);
    }
    CHECK(smsParseFromPhone(eight.data(), eight.size(), m, used) == SmsParse::Ok && m.submit.text == "HELLO");
    eight[27] = 0xe4;                                   // beyond the first 128: a question mark
    CHECK(smsParseFromPhone(eight.data(), eight.size(), m, used) == SmsParse::Ok && m.submit.text == "?ELLO");
    eight[24] = 0x04;                                   // 8-bit data of another kind
    CHECK(smsParseFromPhone(eight.data(), eight.size(), m, used) == SmsParse::Error);

    // damaged
    Bytes bad = kSubmit;
    bad[10] = 0x42;                                     // not the user data element
    CHECK(smsParseFromPhone(bad.data(), bad.size(), m, used) == SmsParse::Error);
    bad = kSubmit;
    bad[12] = 0x10;                                     // not an SMS-SUBMIT
    CHECK(smsParseFromPhone(bad.data(), bad.size(), m, used) == SmsParse::Error);
    bad = kSubmit;
    bad[12] = 0x09;                                     // reserved validity format
    CHECK(smsParseFromPhone(bad.data(), bad.size(), m, used) == SmsParse::Error);
    bad = kSubmit;
    bad[0] = 0x08;                                      // spare bits
    CHECK(smsParseFromPhone(bad.data(), bad.size(), m, used) == SmsParse::Error);
    bad = kSubmit;
    bad[26] = 0xa1;                                     // 161 characters
    CHECK(smsParseFromPhone(bad.data(), bad.size(), m, used) == SmsParse::Error);
}

static void testOtherMessages()
{
    SmsFromPhone m;
    size_t used = 0;

    // RP-ACK, RP-ERROR (page 29), with and without the cause
    CHECK(smsParseFromPhone(Bytes({0x02, 0x01}).data(), 2, m, used) == SmsParse::Ok);
    CHECK(m.kind == SmsRpKind::Ack && m.rpRef == 1 && used == 2);
    const Bytes error = {0x04, 0x01, 0x42, 0x01, 0x6f};
    CHECK(smsParseFromPhone(error.data(), error.size(), m, used) == SmsParse::Ok);
    CHECK(m.kind == SmsRpKind::Error && m.cause == 0x6f && used == 5);
    CHECK(smsParseFromPhone(error.data(), 4, m, used) == SmsParse::NeedMore);
    CHECK(smsParseFromPhone(error.data(), 2, m, used) == SmsParse::NeedMore);
    const Bytes noCause = {0x04, 0x02, 0x00};
    CHECK(smsParseFromPhone(noCause.data(), noCause.size(), m, used) == SmsParse::Ok && m.cause == 0 && used == 2);

    // memory available and ready to receive (page 31): reference, address
    const Bytes ready = {0x07, 0x03, 0x0c, 0x91, 0x3d, 0x85, 0x94, 0x21, 0x43, 0x65};
    CHECK(smsParseFromPhone(ready.data(), ready.size(), m, used) == SmsParse::Ok);
    CHECK(m.kind == SmsRpKind::ReadyToReceive && m.rpRef == 3 && m.address == "+35849123456" && used == ready.size());
    Bytes memory = ready;
    memory[0] = 0x06;
    CHECK(smsParseFromPhone(memory.data(), memory.size(), m, used) == SmsParse::Ok && m.kind == SmsRpKind::MemoryAvailable);
    CHECK(smsParseFromPhone(ready.data(), 6, m, used) == SmsParse::NeedMore);

    // messages that a phone does not send
    for (uint8_t mti : {1, 3, 5})
    {
        const Bytes b = {mti, 0x01, 0x00, 0x00, 0x00};

        CHECK(smsParseFromPhone(b.data(), b.size(), m, used) == SmsParse::Error);
    }
}

// The data come in pieces: nothing is taken until the message is whole.
static void testPieces()
{
    for (size_t piece = 1; piece <= 7; piece++)
    {
        Bytes stream;
        SmsFromPhone m;
        size_t used = 0;
        bool done = false;

        for (size_t i = 0; i < kSubmit.size() && !done; i += piece)
        {
            stream.insert(stream.end(), kSubmit.begin() + static_cast<long>(i),
                          kSubmit.begin() + static_cast<long>(std::min(kSubmit.size(), i + piece)));
            const SmsParse r = smsParseFromPhone(stream.data(), stream.size(), m, used);

            CHECK(r != SmsParse::Error);
            if (r == SmsParse::Ok)
            {
                CHECK(stream.size() == kSubmit.size() && used == kSubmit.size());
                done = true;
            }
        }
        CHECK(done);
    }

    // random octets never crash and never take more than there is
    uint32_t seed = 99;

    for (int round = 0; round < 3000; round++)
    {
        Bytes noise;

        seed = seed * 1664525u + 1013904223u;
        const size_t n = (seed >> 24) % 40;
        for (size_t i = 0; i < n; i++)
        {
            seed = seed * 1664525u + 1013904223u;
            noise.push_back(static_cast<uint8_t>(seed >> 24));
        }
        // some of them start like a real one
        if (!noise.empty() && (round & 1)) {
            noise[0] = 0x00;
        }
        SmsFromPhone m;
        size_t used = 0;

        if (smsParseFromPhone(noise.data(), noise.size(), m, used) == SmsParse::Ok) {
            CHECK(used <= noise.size());
        }
    }
}

int main()
{
    testAlphabet();
    testAddresses();
    testTime();
    testDeliverExample();
    testSubmitExample();
    testOtherMessages();
    testPieces();

    if (g_failures) {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("short message tests passed\n");
    return 0;
}
