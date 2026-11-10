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
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "nmtframe.h"

using namespace modemnmt;
using MT = MessageType;
using modemcellular::NumberType;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static EncodedBits codedPart(const FrameBits& bits)
{
    EncodedBits coded;

    for (int i = 0; i < kEncodedBits; i++) {
        coded[i] = bits[kBitSyncBits + kFrameSyncBits + i];
    }

    return coded;
}

static bool sameFrame(const Frame& a, const Frame& b)
{
    return (a.mt == b.mt) && (a.channelNo == b.channelNo) && (a.tcNo == b.tcNo)
        && (a.trafficArea == b.trafficArea) && (a.msCountry == b.msCountry) && (a.msNumber == b.msNumber)
        && (a.tariffClass == b.tariffClass) && (a.lineSignal == b.lineSignal) && (a.digit == b.digit)
        && (a.idle == b.idle) && (a.chanAct == b.chanAct) && (a.measOrder == b.measOrder) && (a.meas == b.meas)
        && (a.prefix == b.prefix) && (a.supervisory == b.supervisory) && (a.msPassword == b.msPassword)
        && (a.areaInfo == b.areaInfo) && (a.additionalInfo == b.additionalInfo) && (a.rand == b.rand)
        && (a.sres == b.sres) && (a.limitStrengthEval == b.limitStrengthEval) && (a.c == b.c)
        && (a.seqNumber == b.seqNumber) && (a.checksum == b.checksum) && (a.waitingInfo == b.waitingInfo);
}

// The 16 digits of a frame written as 16 hexadecimal characters: N1 N2 N3 P and 12 more.
static FrameDigits digitsOf(const std::string& hex)
{
    FrameDigits d{};

    if (hex.size() != 16) {
        std::fprintf(stderr, "digit string '%s' has %zu characters, not 16\n", hex.c_str(), hex.size());
        g_failures++;
        return d;
    }

    for (int i = 0; i < 16; i++)
    {
        const char c = hex[i];
        d[i] = static_cast<uint8_t>((c >= 'a') ? (c - 'a' + 10) : (c - '0'));
    }

    return d;
}

static MT typeOf(const std::string& hex, Direction direction, bool callAck = false)
{
    return decodeMessageType(digitsOf(hex), direction, callAck);
}

// ---- the frame table --------------------------------------------------------------------

static void testFrameTable()
{
    for (int i = 0; i < kMessageTypeCount; i++)
    {
        const FrameDef& def = frameDef(static_cast<MT>(i));

        CHECK(static_cast<int>(def.type) == i);
        CHECK(std::strlen(def.digits) == 16);
        CHECK(def.digits[3] == 'P');
        CHECK(def.prefix >= 0 && def.prefix <= 15);
        CHECK(def.number != nullptr);
        CHECK(def.description != nullptr);

        for (int j = 0; j < 16; j++) {
            CHECK(def.digits[j] == '-' || isFrameElement(def.digits[j]));
        }

        // frames that carry a channel number start with N1N2N3
        if (def.digits[0] != 'J' && def.digits[0] != '-') {
            CHECK(std::strncmp(def.digits, "NNN", 3) == 0);
        }
    }

    CHECK(std::string(frameName(MT::Frame1a)) == "1a");
    CHECK(std::string(frameName(MT::Frame1aA)) == "1a'");
    CHECK(std::string(frameName(MT::Frame1aB)) == "1a''");
    CHECK(std::string(frameName(MT::Frame10d)) == "10d");
    CHECK(std::string(frameName(MT::Frame20_3)) == "20");
    CHECK(std::string(frameName(MT::FrameUnknownMtx)).empty());
    CHECK(frameDef(static_cast<MT>(200)).type == MT::FrameUnknownMtx);

    CHECK(!isFrameElement('-'));
    CHECK(!isFrameElement('x'));
    CHECK(!isFrameElement('\0'));
    CHECK(isFrameElement('N') && isFrameElement('r') && isFrameElement('F'));
}

// ---- the frames of the standard --------------------------------------------------------

// NMT Doc 450-1 pages 85 and 86: frame 1a, channel 135 (N1N2N3 = 1 3 5), P(12), traffic area 64
// (Y1 = 6, Y2 = 4), H1..H10 = 0, and the encoded message printed there.
static void testStandardExampleFrame()
{
    Frame f;
    f.mt = MT::Frame1a;
    f.channelNo = static_cast<uint16_t>(encodeChannel(System::Nmt450, 135, 0));
    f.trafficArea = static_cast<uint8_t>(encodeTrafficArea(System::Nmt450, 135, 0x64));

    const FrameDigits digits = assembleDigits(f);
    const uint8_t expected[16] = {1, 3, 5, 12, 6, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    for (int i = 0; i < 16; i++) {
        CHECK(digits[i] == expected[i]);
    }

    const FrameBits bits = encodeFrame(f);

    // bit synchronisation 101010101010101 and frame synchronisation 11100010010
    const char* sync = "10101010101010111100010010";

    for (int i = 0; i < 26; i++) {
        CHECK(bits[i] == sync[i] - '0');
    }

    CHECK(kFrameBits == 166);
    CHECK(kBitSyncBits == 15);
    CHECK(kFrameSyncBits == 11);

    // Y1..Y20 and Y133..Y140 as printed
    const int first20[20] = {1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1};
    const int last8[8] = {1, 0, 1, 0, 1, 0, 1, 0};

    for (int i = 0; i < 20; i++) {
        CHECK(bits[26 + i] == first20[i]);
    }

    for (int i = 0; i < 8; i++) {
        CHECK(bits[26 + 132 + i] == last8[i]);
    }

    // and back
    const Frame g = decodeFrame(codedPart(bits), Direction::MtxToMs, false);
    CHECK(g.mt == MT::Frame1a);
    CHECK(g.prefix == 12);
    CHECK(g.channelNo == f.channelNo);
    CHECK(g.trafficArea == f.trafficArea);
    CHECK(g.additionalInfo == 0);
}

// NMT Doc 450-1 page 56: the nationally significant A-number 060132389 in two frames.
static void testANumberExample()
{
    Frame f;

    int next = encodeANumber(f, 0, NumberType::National, "060132389", System::Nmt450, 135, 0, 0x64);
    CHECK(next == 1);
    CHECK(f.mt == MT::Frame8);
    CHECK(f.seqNumber == 0);
    // H1 = 1 (last frame), H2 = 1 (national), H3..H7 = 10 6 10 1 3, W1W2 = 9 8
    CHECK(f.additionalInfo == 0x11a6a13);
    CHECK(f.checksum == 0x98);
    CHECK(f.channelNo == 0x135);
    CHECK(f.trafficArea == 0x64);

    const FrameDigits d1 = assembleDigits(f);
    const uint8_t expected1[16] = {1, 3, 5, 1, 6, 4, 0, 1, 1, 10, 6, 10, 1, 3, 9, 8};

    for (int i = 0; i < 16; i++) {
        CHECK(d1[i] == expected1[i]);
    }

    next = encodeANumber(f, next, NumberType::National, "060132389", System::Nmt450, 135, 0, 0x64);
    CHECK(next == 0);
    CHECK(f.seqNumber == 1);
    // H1..H7 = 2 3 8 9 0 0 0, W1W2 = 13 10
    CHECK(f.additionalInfo == 0x2389000);
    CHECK(f.checksum == 0xda);

    const FrameDigits d2 = assembleDigits(f);
    const uint8_t expected2[16] = {1, 3, 5, 1, 6, 4, 1, 2, 3, 8, 9, 0, 0, 0, 13, 10};

    for (int i = 0; i < 16; i++) {
        CHECK(d2[i] == expected2[i]);
    }
}

// Capacity of the sequences (NMT Doc 450-1 page 56): 5, 12, 19, 26, 33, 40 digits for 1..6 frames.
static void testANumberFrameCount()
{
    const int capacity[6] = {5, 12, 19, 26, 33, 40};

    for (int frames = 1; frames <= 6; frames++)
    {
        for (int length = (frames == 1) ? 0 : capacity[frames - 2] + 1; length <= capacity[frames - 1]; length++)
        {
            Frame f;
            const std::string number(static_cast<size_t>(length), '5');

            // the first frame names the index of the last one, and the sequence has that many frames
            encodeANumber(f, 0, NumberType::Unknown, number, System::Nmt450, 1, 0, 0);
            CHECK(((f.additionalInfo >> 24) & 0xf) == static_cast<uint64_t>(frames - 1));

            int index = 0;
            int count = 0;

            do {
                index = encodeANumber(f, index, NumberType::Unknown, number, System::Nmt450, 1, 0, 0);
                count++;
            } while (index != 0);

            CHECK(count == frames);
        }
    }

    // types, digits and fillers
    Frame f;
    encodeANumber(f, 0, NumberType::NotAvailable, "", System::Nmt450, 1, 0, 0);
    CHECK(((f.additionalInfo >> 20) & 0xf) == 3);
    CHECK((f.additionalInfo & 0xfffff) == 0);
    encodeANumber(f, 0, NumberType::Anonymous, "1", System::Nmt450, 1, 0, 0);
    CHECK(((f.additionalInfo >> 20) & 0xf) == 4);
    CHECK((f.additionalInfo & 0xfffff) == 0x10000);
    encodeANumber(f, 0, NumberType::International, "+49", System::Nmt450, 1, 0, 0);
    CHECK(((f.additionalInfo >> 20) & 0xf) == 2);
    CHECK((f.additionalInfo & 0xfffff) == 0xd4900);         // '+' is 13
    encodeANumber(f, 0, NumberType::Subscriber, "1", System::Nmt450, 1, 0, 0);
    CHECK(((f.additionalInfo >> 20) & 0xf) == 0);
    encodeANumber(f, 0, NumberType::Unknown, "1", System::Nmt450, 1, 0, 0);
    CHECK(((f.additionalInfo >> 20) & 0xf) == 0);
}

// ---- every frame ------------------------------------------------------------------------

static void setField(Frame& f, char letter, uint64_t v)
{
    switch (letter)
    {
    case 'N': f.channelNo = static_cast<uint16_t>(v); break;
    case 'n': f.tcNo = static_cast<uint16_t>(v); break;
    case 'Y': f.trafficArea = static_cast<uint8_t>(v); break;
    case 'Z': f.msCountry = static_cast<uint8_t>(v); break;
    case 'X': f.msNumber = static_cast<uint32_t>(v); break;
    case 'Q': f.tariffClass = static_cast<uint8_t>(v); break;
    case 'L': f.lineSignal = static_cast<uint32_t>(v); break;
    case 'S': f.digit = static_cast<uint32_t>(v); break;
    case 'J': f.idle = v; break;
    case 'A': f.chanAct = static_cast<uint8_t>(v); break;
    case 'V': f.measOrder = static_cast<uint32_t>(v); break;
    case 'r': f.meas = static_cast<uint32_t>(v); break;
    case 'f': f.supervisory = static_cast<uint32_t>(v); break;
    case 'K': f.msPassword = static_cast<uint16_t>(v); break;
    case 'T': f.areaInfo = static_cast<uint8_t>(v); break;
    case 'H': f.additionalInfo = v; break;
    case 'C': f.rand = static_cast<uint32_t>(v); break;
    case 'R': f.sres = v; break;
    case 'l': f.limitStrengthEval = static_cast<uint16_t>(v); break;
    case 'c': f.c = static_cast<uint16_t>(v); break;
    case 'M': f.seqNumber = static_cast<uint8_t>(v); break;
    case 'W': f.checksum = static_cast<uint16_t>(v); break;
    case 'F': f.waitingInfo = v; break;
    default: break;
    }
}

// A frame with a value in every element of its template. Elements that appear in several
// places of the template cannot keep one value per place, so they are 0.
static Frame canonical(MT mt)
{
    const FrameDef& def = frameDef(mt);
    Frame f;
    f.mt = mt;
    f.prefix = static_cast<uint8_t>(def.prefix);

    struct Group { char letter; int width; };
    std::vector<Group> groups;

    for (int i = 0; i < 16; i++)
    {
        if (def.digits[i] == '-' || def.digits[i] == 'P') {
            continue;
        }

        if (!groups.empty() && groups.back().letter == def.digits[i] && def.digits[i - 1] == def.digits[i]) {
            groups.back().width++;
        } else {
            groups.push_back({def.digits[i], 1});
        }
    }

    std::map<char, int> count;

    for (const Group& g : groups) {
        count[g.letter]++;
    }

    for (const Group& g : groups)
    {
        if (count[g.letter] > 1) {
            continue;
        }

        const uint64_t mask = (g.width >= 16) ? ~0ULL : ((1ULL << (4 * g.width)) - 1);
        const uint64_t seed = static_cast<uint64_t>(g.letter) * 0x9e3779b97f4a7c15ULL + static_cast<uint64_t>(mt) * 0x2545f4914f6cdd1dULL;
        setField(f, g.letter, ((seed >> 7) | 1) & mask);
    }

    // The values that tell the frames of one prefix apart (see decodeMessageType).
    switch (mt)
    {
    case MT::Frame1a:
    case MT::Frame1aA:
    case MT::Frame1aB:
        f.additionalInfo &= (1ULL << 36) - 1;       // H1 = Z = 0: no subscriber
        break;
    case MT::Frame1b:
        f.additionalInfo &= 0xfff;                  // H1..H7 = 0
        break;
    case MT::Frame2a:
        f.msCountry = 5;
        f.additionalInfo = 0x3f3;
        break;
    case MT::Frame2b:
        f.msCountry = 5;
        f.tcNo = 0x123;
        break;
    case MT::Frame2c:
        f.msCountry = 5;
        f.additionalInfo = 0x3f1;
        break;
    case MT::Frame2d:
        f.msCountry = 5;
        f.additionalInfo = 0x3f2;
        break;
    case MT::Frame2e:
        f.msCountry = 5;
        f.msNumber = 0x123456;
        break;
    case MT::Frame2f:
        f.msCountry = 5;
        f.additionalInfo = 0x3f0;
        break;
    case MT::Frame3a:
    case MT::Frame3d:
        f.msCountry = 5;
        f.tcNo = 0x123;
        break;
    case MT::Frame3b:
        f.msCountry = 5;
        f.additionalInfo = 0x000;
        break;
    case MT::Frame3c:
        f.msCountry = 5;
        break;
    case MT::Frame4:
        f.idle = 0x1234567;
        break;
    case MT::Frame4b:
        f.idle = 0x1234567;
        f.additionalInfo = 0x3f6;
        break;
    case MT::Frame5a:
        f.msCountry = 5;
        f.lineSignal = 0x333;
        break;
    case MT::Frame5b:
        f.msCountry = 5;
        f.lineSignal = 0;
        f.tariffClass = 0x5a;
        break;
    case MT::Frame5c:
        f.msCountry = 5;
        break;
    case MT::Frame13a:
        f.lineSignal = 0x1ee11;
        break;
    case MT::Frame13b:
        f.lineSignal = 0x222;
        f.tariffClass = 0x12;
        break;
    case MT::Frame14a:
        f.digit = 0x00333;
        break;
    case MT::Frame14b:
        f.digit = 0xff333;
        break;
    case MT::Frame20_1:
        f.msCountry = 15;
        f.chanAct = 3;
        break;
    case MT::Frame20_2:
        f.msCountry = 15;
        f.chanAct = 1;
        break;
    case MT::Frame20_3:
        f.msCountry = 15;
        f.chanAct = 6;
        break;
    case MT::Frame20_4:
        f.msCountry = 15;
        f.chanAct = 7;
        break;
    case MT::Frame20_5:
        f.msCountry = 15;
        f.chanAct = 15;
        break;
    case MT::Frame21b:
    case MT::Frame21c:
        f.msCountry = 15;
        f.measOrder = 15;
        break;
    case MT::Frame22:
        f.msCountry = 15;
        f.measOrder = 0x123456;
        break;
    case MT::Frame25_1:
        f.chanAct = 1;
        break;
    case MT::Frame25_2:
        f.chanAct = 2;
        break;
    case MT::Frame25_3:
        f.chanAct = 14;
        break;
    case MT::Frame25_4:
        f.chanAct = 7;
        break;
    default:
        break;
    }

    return f;
}

// Every frame of the standard comes back the way it was sent, as the frame type it is.
static void testEveryFrameRoundTrips()
{
    for (int i = 0; i < kMessageTypeCount; i++)
    {
        const MT mt = static_cast<MT>(i);

        if (mt == MT::FrameUnknownMtx || mt == MT::FrameUnknownBsMs) {
            continue;
        }

        const Frame f = canonical(mt);
        const FrameBits bits = encodeFrame(f);
        const Frame g = decodeFrame(codedPart(bits), frameDef(mt).direction, mt == MT::Frame10a);

        if (!sameFrame(f, g))
        {
            std::fprintf(stderr, "frame %s (index %d) does not come back: decoded as %s (index %d)\n",
                         frameName(mt), i, frameName(g.mt), static_cast<int>(g.mt));
            g_failures++;
        }

        // the fourth digit is the prefix of the table
        CHECK(assembleDigits(f)[3] == frameDef(mt).prefix);
    }
}

// ---- decoding rules ---------------------------------------------------------------------
// Digit strings: N1 N2 N3 P and twelve more digits; for the frames from the exchange the fifth
// and sixth digit are the traffic area.

static void testDecodeFromTheExchange()
{
    const Direction from = Direction::MtxToMs;

    CHECK(typeOf("1350640000000000", from) == MT::Frame6);
    CHECK(typeOf("1351640000000000", from) == MT::Frame8);
    CHECK(typeOf("1352640000000000", from) == MT::Frame5c);
    CHECK(typeOf("1353640000000000", from) == MT::Frame4);
    CHECK(typeOf("1353645000000000", from) == MT::Frame4);
    CHECK(typeOf("135364f000000000", from) == MT::Frame21b);
    CHECK(typeOf("1355645123456000", from) == MT::Frame3b);
    CHECK(typeOf("13556451234563f3", from) == MT::Frame3b);
    CHECK(typeOf("1355645123456123", from) == MT::Frame3a);
    CHECK(typeOf("135564f000000000", from) == MT::Frame21c);
    CHECK(typeOf("1356645123456123", from) == MT::Frame5a);
    CHECK(typeOf("1356645123456023", from) == MT::Frame5b);
    CHECK(typeOf("1357645123456123", from) == MT::Frame3d);
    CHECK(typeOf("13576451234563f4", from) == MT::Frame4b);
    CHECK(typeOf("1358645123456123", from) == MT::Frame7);
    CHECK(typeOf("1359645123456123", from) == MT::Frame3c);
    CHECK(typeOf("135a645123456123", from) == MT::Frame30);

    // prefix 4: combined calling and traffic channel indication, or an alternative type of call
    CHECK(typeOf("1354640000000000", from) == MT::Frame1b);
    CHECK(typeOf("1354645123456000", from) == MT::Frame2e);
    // NMT-900 area numbers and queue codes: 9F3..9F6 and 9F0..9F2 like 3F3..3F6 and 3F0..3F2
    CHECK(typeOf("13556411234569f5", from) == MT::Frame3b);
    CHECK(typeOf("13556411234569f3", from) == MT::Frame3b);
    CHECK(typeOf("13556411234569f7", from) == MT::Frame3a);
    CHECK(typeOf("13556411234563f5", from) == MT::Frame3b);
    CHECK(typeOf("13576411234569f6", from) == MT::Frame4b);
    CHECK(typeOf("13576411234569f2", from) == MT::Frame3d);
    // H1 = 14 (additional information) is 1b, battery saving as on every idle 1b
    CHECK(typeOf("135464eb30028000", from) == MT::Frame1b);

    // prefix 12: calling channel; the area numbers 3F3..3F6 and 000 call a subscriber, 3F0..3F2
    // are queueing and scanning information, anything else is a traffic channel
    CHECK(typeOf("135c640000000000", from) == MT::Frame1a);
    CHECK(typeOf("135c645123456000", from) == MT::Frame2a);
    CHECK(typeOf("135c6451234563f3", from) == MT::Frame2a);
    CHECK(typeOf("135c6451234563f6", from) == MT::Frame2a);
    CHECK(typeOf("135c6451234563f0", from) == MT::Frame2f);
    CHECK(typeOf("135c6451234563f1", from) == MT::Frame2c);
    CHECK(typeOf("135c6451234563f2", from) == MT::Frame2d);
    CHECK(typeOf("135c6451234563f7", from) == MT::Frame2b);
    CHECK(typeOf("135c645123456123", from) == MT::Frame2b);
}

// NMT Doc 900-1 page 22: with two calling channels the prefixes 11 and 13 are used in all
// signalling on the calling channel, including 2a to 2d and 2f.
static void testCallingChannelWithTwoCallingChannels()
{
    const Direction from = Direction::MtxToMs;

    for (int prefix : {11, 12, 13})
    {
        const char p = static_cast<char>('a' + prefix - 10);
        const std::string head = std::string("135") + p + "64";
        const std::string noSubscriber = head + "0000000000";
        const std::string a = head + "51234563f3";
        const std::string b = head + "5123456123";
        const std::string c = head + "51234563f1";
        const std::string d = head + "51234563f2";
        const std::string f = head + "51234563f0";

        const MT none = (prefix == 11) ? MT::Frame1aA : (prefix == 13) ? MT::Frame1aB : MT::Frame1a;

        CHECK(typeOf(noSubscriber, from) == none);
        CHECK(typeOf(a, from) == MT::Frame2a);
        CHECK(typeOf(b, from) == MT::Frame2b);
        CHECK(typeOf(c, from) == MT::Frame2c);
        CHECK(typeOf(d, from) == MT::Frame2d);
        CHECK(typeOf(f, from) == MT::Frame2f);

        // the decoded frame keeps the prefix that was received
        const Frame g = disassembleDigits(digitsOf(a), from, false);
        CHECK(g.mt == MT::Frame2a);
        CHECK(g.prefix == prefix);
        CHECK(g.msCountry == 5);
        CHECK(g.msNumber == 0x123456);
        CHECK(g.additionalInfo == 0x3f3);
    }
}

// NMT Doc 450-1 page 52: H1(14) and H2(11) are battery saving information; Z(14) alone is not.
static void testBatterySaving()
{
    const Direction from = Direction::MtxToMs;

    CHECK(typeOf("135c64eb00000000", from) == MT::Frame1a);
    CHECK(typeOf("135c64e512345600", from) != MT::Frame1a);
    // Z(15) is information for a base station
    CHECK(typeOf("135c64f000000000", from) == MT::Frame1a);
}

// NMT Doc 450-1 pages 30 and 31: Z(15) in the seventh digit marks the frames to the base station.
static void testExchangeToBaseStation()
{
    const Direction from = Direction::MtxToBs;

    // frame 22: management order; without Z(15) it is no frame
    CHECK(typeOf("135e64f000123456", from) == MT::Frame22);
    CHECK(typeOf("135e640000123456", from) == MT::FrameUnknownMtx);

    // frame 20: channel activation order, told apart by A (the eleventh digit); the digits after
    // it are f or J and do not matter
    const struct { char a; MT mt; } byActivation[] = {
        {'0', MT::Frame20_2}, {'1', MT::Frame20_2}, {'2', MT::Frame20_2}, {'3', MT::Frame20_1},
        {'4', MT::Frame20_2}, {'5', MT::Frame20_2}, {'6', MT::Frame20_3}, {'7', MT::Frame20_4},
        {'8', MT::Frame20_2}, {'9', MT::Frame20_2}, {'a', MT::Frame20_2}, {'b', MT::Frame20_2},
        {'c', MT::Frame20_2}, {'d', MT::Frame20_3}, {'e', MT::Frame20_4}, {'f', MT::Frame20_5},
    };

    for (const auto& e : byActivation)
    {
        CHECK(typeOf(std::string("135f64f000") + e.a + "3c3c3", from) == e.mt);
        CHECK(typeOf(std::string("135f64f000") + e.a + "00000", from) == e.mt);
        CHECK(typeOf(std::string("135f64f000") + e.a + "fffff", from) == e.mt);
    }

    // without Z(15) it is no frame
    CHECK(typeOf("135f640003c3c3c3", from) == MT::FrameUnknownMtx);

    // frames 21b and 21c carry the measurement order V(15) after Z(15)
    CHECK(typeOf("135364f000f00123", from) == MT::Frame21b);
    CHECK(typeOf("135564f000f00123", from) == MT::Frame21c);
}

static void testToTheExchange()
{
    const Direction to = Direction::MsToMtx;

    CHECK(typeOf("1350000000000000", to) == MT::Frame15);
    CHECK(typeOf("1351000000000000", to) == MT::Frame10b);
    CHECK(typeOf("1351000000000000", to, true) == MT::Frame10a);
    CHECK(typeOf("1352000000000000", to) == MT::Frame26);
    CHECK(typeOf("1353000000000000", to) == MT::FrameUnknownBsMs);
    CHECK(typeOf("1354000000000000", to) == MT::Frame27);
    CHECK(typeOf("1355000000000000", to) == MT::FrameUnknownBsMs);
    CHECK(typeOf("1356000000000000", to) == MT::Frame10c);
    CHECK(typeOf("135a000000000000", to) == MT::Frame10d);
    CHECK(typeOf("135b000000000000", to) == MT::Frame12);
    CHECK(typeOf("135c000000000000", to) == MT::Frame16);
    CHECK(typeOf("135d000000000000", to) == MT::Frame28);
    CHECK(typeOf("135e000000000000", to) == MT::Frame11a);
    CHECK(typeOf("135f000000000000", to) == MT::Frame11b);

    // prefix 7: digit signals, told apart by the position indicator S(0) or S(15) in the twelfth digit
    CHECK(typeOf("1357645123400333", to) == MT::Frame14a);
    CHECK(typeOf("13576451234ff333", to) == MT::Frame14b);
    CHECK(typeOf("1357645123455333", to) == MT::FrameUnknownBsMs);

    // prefix 8: line signals; L(2) in the twelfth digit is the answer acknowledgement of a coin-box
    CHECK(typeOf("1358645123456eee", to) == MT::Frame13a);
    CHECK(typeOf("1358645123422255", to) == MT::Frame13b);

    // the same digits are the same frame from a base station or from an unknown origin
    CHECK(typeOf("135c000000000000", Direction::BsToMtx) == MT::Frame16);
    CHECK(typeOf("135c000000000000", Direction::XxToMtx) == MT::Frame16);
}

// NMT Doc 900-1 page 25: the call acknowledgement of the alternative type of call is P(10).
static void testFrame10d()
{
    const Frame f = canonical(MT::Frame10d);

    CHECK(f.prefix == 10);
    CHECK(assembleDigits(f)[3] == 10);
    CHECK(decodeMessageType(assembleDigits(f), Direction::MsToMtx, false) == MT::Frame10d);
    // prefix 10 from the exchange is the test channel indication
    CHECK(decodeMessageType(assembleDigits(f), Direction::MtxToMs, false) == MT::Frame30);
}

// NMT Doc 450-1 pages 31 and 32: the status information A is the digit after BS indication and idle.
static void testChannelStatusInformation()
{
    const Direction to = Direction::BsToMtx;

    // digits 13 to 15 hold other information (f, l, C) and must not matter
    CHECK(typeOf("1359f001ee222222", to) == MT::Frame25_1);
    CHECK(typeOf("1359f0020000002e", to) == MT::Frame25_2);
    CHECK(typeOf("1359f0060000003e", to) == MT::Frame25_2);
    CHECK(typeOf("1359f00e0000000e", to) == MT::Frame25_3);
    CHECK(typeOf("1359f0070000000e", to) == MT::Frame25_4);
    CHECK(typeOf("1359f0080000000e", to) == MT::Frame25_4);
    CHECK(typeOf("1359f0000000000e", to) == MT::Frame25_1);
}

static void testUnknownFrames()
{
    CHECK(typeOf("135e640000000000", Direction::MtxToMs) == MT::FrameUnknownMtx);

    const Frame f = disassembleDigits(digitsOf("1353000123456789"), Direction::MsToMtx, false);
    CHECK(f.mt == MT::FrameUnknownBsMs);
    CHECK(f.prefix == 3);                       // only the prefix is taken from an unknown frame
    CHECK(f.channelNo == 0);
    CHECK(f.msNumber == 0);
}

// ---- bits -------------------------------------------------------------------------------

// A burst of up to 6 bits in the encoded part does not change the frame.
static void testFramesSurviveBursts()
{
    std::mt19937 rng(7);

    for (int i = 0; i < kMessageTypeCount; i++)
    {
        const MT mt = static_cast<MT>(i);

        if (mt == MT::FrameUnknownMtx || mt == MT::FrameUnknownBsMs) {
            continue;
        }

        const Frame f = canonical(mt);
        const EncodedBits clean = codedPart(encodeFrame(f));

        for (int n = 0; n < 20; n++)
        {
            EncodedBits bits = clean;
            const int length = 1 + static_cast<int>(rng() % 6);
            const int start = static_cast<int>(rng() % (kEncodedBits - length + 1));

            for (int k = 0; k < length; k++) {
                bits[start + k] ^= 1;
            }

            const Frame g = decodeFrame(bits, frameDef(mt).direction, mt == MT::Frame10a);
            CHECK(sameFrame(f, g));
        }
    }
}

// There is no error detection: any 140 bits decode to some frame.
static void testAnyBitsDecode()
{
    std::mt19937 rng(8);

    for (int n = 0; n < 20000; n++)
    {
        EncodedBits bits;

        for (auto& b : bits) {
            b = static_cast<uint8_t>(rng() & 1);
        }

        for (Direction d : {Direction::MtxToMs, Direction::MsToMtx, Direction::MtxToBs, Direction::BsToMtx})
        {
            const Frame f = decodeFrame(bits, d, (n & 1) != 0);
            CHECK(static_cast<int>(f.mt) < kMessageTypeCount);
        }
    }
}

int main()
{
    testFrameTable();
    testStandardExampleFrame();
    testANumberExample();
    testANumberFrameCount();
    testEveryFrameRoundTrips();
    testDecodeFromTheExchange();
    testCallingChannelWithTwoCallingChannels();
    testBatterySaving();
    testExchangeToBaseStation();
    testToTheExchange();
    testFrame10d();
    testChannelStatusInformation();
    testUnknownFrames();
    testFramesSurviveBursts();
    testAnyBitsDecode();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("all frame tests passed\n");
    return 0;
}
