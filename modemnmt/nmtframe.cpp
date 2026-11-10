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

#include <cstring>

#include "nmtframe.h"

namespace modemnmt
{

using MT = MessageType;

namespace
{

// Frame table, NMT Doc 450-1 para 4.3.2 and NMT Doc 900-1 para 4.3.2. The entries are in the
// order of MessageType.
const FrameDef kFrames[] = {
    {MT::Frame1a,   "NNNPYYHHHHHHHHHH", Direction::MtxToMs, 12, "1a",   "Calling channel indication"},
    {MT::Frame1aA,  "NNNPYYHHHHHHHHHH", Direction::MtxToMs, 11, "1a'",  "Calling channel indication (for MS group A)"},
    {MT::Frame1aB,  "NNNPYYHHHHHHHHHH", Direction::MtxToMs, 13, "1a''", "Calling channel indication (for MS group B)"},
    {MT::Frame1b,   "NNNPYYHHHHHHHHHH", Direction::MtxToMs, 4,  "1b",   "Combined calling and traffic channel indication"},
    {MT::Frame2a,   "NNNPYYZXXXXXXHHH", Direction::MtxToMs, 12, "2a",   "Call to mobile subscriber on calling channel"},
    {MT::Frame2b,   "NNNPYYZXXXXXXnnn", Direction::MtxToMs, 12, "2b",   "Traffic channel allocation on calling channel"},
    {MT::Frame2c,   "NNNPYYZXXXXXXHHH", Direction::MtxToMs, 12, "2c",   "Queueing information to MS with priority on calling channel"},
    {MT::Frame2d,   "NNNPYYZXXXXXXHHH", Direction::MtxToMs, 12, "2d",   "Traffic channel scanning order on calling channel"},
    {MT::Frame2e,   "NNNPYYZXXXXXXHHH", Direction::MtxToMs, 4,  "2e",   "Alternative type of call to MS on combined CC/TC"},
    {MT::Frame2f,   "NNNPYYZXXXXXXHHH", Direction::MtxToMs, 12, "2f",   "Queuing information to ordinary MS"},
    {MT::Frame3a,   "NNNPYYZXXXXXXnnn", Direction::MtxToMs, 5,  "3a",   "Traffic channel allocation on traffic channel"},
    {MT::Frame3b,   "NNNPYYZXXXXXXHHH", Direction::MtxToMs, 5,  "3b",   "Identity request on traffic channel"},
    {MT::Frame3c,   "NNNPYYZXXXXXXnnn", Direction::MtxToMs, 9,  "3c",   "Traffic channel allocation on traffic channel, short procedure"},
    {MT::Frame3d,   "NNNPYYZXXXXXXnnn", Direction::MtxToMs, 7,  "3d",   "Traffic channel allocation on access channel"},
    {MT::Frame4,    "NNNPYYJJJJJJJHHH", Direction::MtxToMs, 3,  "4",    "Free traffic channel indication"},
    {MT::Frame4b,   "NNNPYYJJJJJJJHHH", Direction::MtxToMs, 7,  "4b",   "Access channel indication"},
    {MT::Frame5a,   "NNNPYYZXXXXXXLLL", Direction::MtxToMs, 6,  "5a",   "Line signal"},
    {MT::Frame5b,   "NNNPYYZXXXXXXLQQ", Direction::MtxToMs, 6,  "5b",   "Line signal: Answer to coin-box"},
    {MT::Frame5c,   "NNNPYYZXXXXXXFFF", Direction::MtxToMs, 2,  "5c",   "Line signal: Message(s) waiting"},
    {MT::Frame6,    "JJJPJJJJJJJJJJJJ", Direction::MtxToXx, 0,  "6",    "Idle frame"},
    {MT::Frame7,    "NNNPYYCCCCCCCJJJ", Direction::MtxToMs, 8,  "7",    "Authentication request"},
    {MT::Frame8,    "NNNPYYMHHHHHHHWW", Direction::MtxToMs, 1,  "8",    "A-subscriber number"},
    {MT::Frame10a,  "NNNPZXXXXXXTJJJJ", Direction::MsToMtx, 1,  "10a",  "Call acknowledgment from MS on calling channel and access on access channel (shortened frame)"},
    {MT::Frame10b,  "NNNPZXXXXXXTYKKK", Direction::MsToMtx, 1,  "10b",  "Seizure from ordinary MS and identity on traffic channel"},
    {MT::Frame10c,  "NNNPZXXXXXXTYKKK", Direction::MsToMtx, 6,  "10c",  "Seizure and identity from called MS on traffic channel"},
    {MT::Frame10d,  "NNNPZXXXXXXTJJJJ", Direction::MsToMtx, 10, "10d",  "Call acknowledgement from MS on the alternative type of call on combined CC/TC (shortened frame)"},
    {MT::Frame11a,  "NNNPZXXXXXXTYKKK", Direction::MsToMtx, 14, "11a",  "Roaming updating seizure and identity on traffic channel"},
    {MT::Frame11b,  "NNNPZXXXXXXTJJJJ", Direction::MsToMtx, 15, "11b",  "Seizure and call acknowledgment on calling channel from MS with priority (shortened frame)"},
    {MT::Frame12,   "NNNPZXXXXXXTYKKK", Direction::MsToMtx, 11, "12",   "Seizure from coin-box on traffic channel"},
    {MT::Frame13a,  "NNNPZXXXXXXLLLLL", Direction::MsToMtx, 8,  "13a",  "Line signal"},
    {MT::Frame13b,  "NNNPZXXXXXXLLLQQ", Direction::MsToMtx, 8,  "13b",  "Line signal: Answer acknowledgment from coin box"},
    {MT::Frame14a,  "NNNPZXXXXXXSSSSS", Direction::MsToMtx, 7,  "14a",  "Digit signal (1st, 3rd, 5th ........digit)"},
    {MT::Frame14b,  "NNNPZXXXXXXSSSSS", Direction::MsToMtx, 7,  "14b",  "Digit signal (2nd, 4th, 6th ........digit)"},
    {MT::Frame15,   "JJJPJJJJJJJJJJJJ", Direction::XxToMtx, 0,  "15",   "Idle frame"},
    {MT::Frame16,   "NNNPRRRRRRRRRRRR", Direction::MsToMtx, 12, "16",   "Signed response"},
    {MT::Frame20_1, "NNNPYYZJJJAfffff", Direction::MtxToBs, 15, "20",   "Channel activation order"},
    {MT::Frame20_2, "NNNPYYZJJJAJJJJJ", Direction::MtxToBs, 15, "20",   "Channel activation order"},
    {MT::Frame20_3, "NNNPYYZJJJAfffff", Direction::MtxToBs, 15, "20",   "Channel activation order"},
    {MT::Frame20_4, "NNNPYYZJJJAlllff", Direction::MtxToBs, 15, "20",   "Channel activation order"},
    {MT::Frame20_5, "NNNPYYZJJJAlllJJ", Direction::MtxToBs, 15, "20",   "Channel activation order"},
    {MT::Frame21b,  "NNNPYYZJJJVJJnnn", Direction::MtxToBs, 3,  "21b",  "Signal strength measurement order on data channel or idle or free marked traffic channel"},
    {MT::Frame21c,  "NNNPYYZJJJVJJnnn", Direction::MtxToBs, 5,  "21c",  "Signal strength measurement order on traffic actually used"},
    {MT::Frame22,   "NNNPYYZJJJVVVVVV", Direction::MtxToBs, 14, "22",   "Order management/maintenance order on idle channel or data channel"},
    {MT::Frame25_1, "NNNPZJJAJJJJJJJJ", Direction::BsToMtx, 9,  "25",   "Channel status information"},
    {MT::Frame25_2, "NNNPZJJAJJJfllJJ", Direction::BsToMtx, 9,  "25",   "Channel status information"},
    {MT::Frame25_3, "NNNPZJJAJJJJllJJ", Direction::BsToMtx, 9,  "25",   "Channel status information"},
    {MT::Frame25_4, "NNNPZJJAJJJcccJJ", Direction::BsToMtx, 9,  "25",   "Channel status information"},
    {MT::Frame26,   "NNNPZJJnnnrrrrrr", Direction::BsToMtx, 2,  "26",   "Signal strength measurement result"},
    {MT::Frame27,   "NNNPZJJVVVVJJJJJ", Direction::BsToMtx, 4,  "27",   "Response on other management/maintenance order on idle channel or data channel"},
    {MT::Frame28,   "NNNPZJJVVVVJJJJJ", Direction::BsToMtx, 13, "28",   "Other maintenance information from BS"},
    {MT::Frame30,   "NNNPYYJJJJJJJHHH", Direction::MtxToMs, 10, "30",   "Test channel indication"},
    {MT::FrameUnknownMtx,  "---P------------", Direction::MtxToXx, 0, "", "illegal (Spare)"},
    {MT::FrameUnknownBsMs, "---P------------", Direction::XxToMtx, 0, "", "illegal (Spare)"},
};

static_assert(sizeof(kFrames) / sizeof(kFrames[0]) == static_cast<size_t>(kMessageTypeCount),
              "the frame table has one entry per message type");

constexpr const char* kElementLetters = "NnYZXQLSJAVrPfKTHCRlcMWF";

uint64_t elementValue(const Frame& frame, char letter)
{
    switch (letter)
    {
    case 'N': return frame.channelNo;
    case 'n': return frame.tcNo;
    case 'Y': return frame.trafficArea;
    case 'Z': return frame.msCountry;
    case 'X': return frame.msNumber;
    case 'Q': return frame.tariffClass;
    case 'L': return frame.lineSignal;
    case 'S': return frame.digit;
    case 'J': return frame.idle;
    case 'A': return frame.chanAct;
    case 'V': return frame.measOrder;
    case 'r': return frame.meas;
    case 'P': return frame.prefix;
    case 'f': return frame.supervisory;
    case 'K': return frame.msPassword;
    case 'T': return frame.areaInfo;
    case 'H': return frame.additionalInfo;
    case 'C': return frame.rand;
    case 'R': return frame.sres;
    case 'l': return frame.limitStrengthEval;
    case 'c': return frame.c;
    case 'M': return frame.seqNumber;
    case 'W': return frame.checksum;
    case 'F': return frame.waitingInfo;
    default: return 0;
    }
}

// The value is cut to the width of the element.
void setElement(Frame& frame, char letter, uint64_t value)
{
    switch (letter)
    {
    case 'N': frame.channelNo = static_cast<uint16_t>(value); break;
    case 'n': frame.tcNo = static_cast<uint16_t>(value); break;
    case 'Y': frame.trafficArea = static_cast<uint8_t>(value); break;
    case 'Z': frame.msCountry = static_cast<uint8_t>(value); break;
    case 'X': frame.msNumber = static_cast<uint32_t>(value); break;
    case 'Q': frame.tariffClass = static_cast<uint8_t>(value); break;
    case 'L': frame.lineSignal = static_cast<uint32_t>(value); break;
    case 'S': frame.digit = static_cast<uint32_t>(value); break;
    case 'J': frame.idle = value; break;
    case 'A': frame.chanAct = static_cast<uint8_t>(value); break;
    case 'V': frame.measOrder = static_cast<uint32_t>(value); break;
    case 'r': frame.meas = static_cast<uint32_t>(value); break;
    case 'P': frame.prefix = static_cast<uint8_t>(value); break;
    case 'f': frame.supervisory = static_cast<uint32_t>(value); break;
    case 'K': frame.msPassword = static_cast<uint16_t>(value); break;
    case 'T': frame.areaInfo = static_cast<uint8_t>(value); break;
    case 'H': frame.additionalInfo = value; break;
    case 'C': frame.rand = static_cast<uint32_t>(value); break;
    case 'R': frame.sres = value; break;
    case 'l': frame.limitStrengthEval = static_cast<uint16_t>(value); break;
    case 'c': frame.c = static_cast<uint16_t>(value); break;
    case 'M': frame.seqNumber = static_cast<uint8_t>(value); break;
    case 'W': frame.checksum = static_cast<uint16_t>(value); break;
    case 'F': frame.waitingInfo = value; break;
    default: break;
    }
}

bool isTowardsMtx(Direction direction)
{
    return (direction == Direction::MsToMtx) || (direction == Direction::BsToMtx) || (direction == Direction::XxToMtx);
}

// H8H9H10 (digits 13..15). NMT-900 codes the fictitious channels 1008..1014 like a channel
// number (bit 9 moves to bit 11), so 3F0..3F6 are 9F0..9F6 there. Neither range is a valid
// traffic channel number in the other system (BCD digit F, bit above the 11 bit channel), so
// both are accepted whatever the system.
int specialCode(const FrameDigits& d)
{
    const int h = (d[13] << 8) + (d[14] << 4) + d[15];

    return (h >= 0x9f0 && h <= 0x9f6) ? h - 0x600 : h;
}

// H8H9H10 of the frames of a calling channel: 000 and the area numbers 3F3..3F6 are no queueing
// or scanning information.
bool isAreaCode(const FrameDigits& d)
{
    const int h = specialCode(d);

    return (h == 0x3f3) || (h == 0x3f4) || (h == 0x3f5) || (h == 0x3f6) || (h == 0x000);
}

// Frames on a calling channel that have prefix 12 (or 11 and 13 if the base station has two
// calling channels). NMT Doc 450-1 para 4.3.3.10 and 4.3.3.12.
MessageType callingChannelFrame(const FrameDigits& d)
{
    // no subscriber
    if (d[6] == 0) {
        return MT::Frame1a;
    }

    // H1(14) and H2(11): battery saving information, a calling channel indication
    if ((d[6] == 14) && (d[7] == 11)) {
        return MT::Frame1a;
    }

    // information for a base station
    if (d[6] == 15) {
        return MT::Frame1a;
    }

    if (isAreaCode(d)) {
        return MT::Frame2a;
    }

    switch (specialCode(d))
    {
    case 0x3f0:
        return MT::Frame2f;
    case 0x3f1:
        return MT::Frame2c;
    case 0x3f2:
        return MT::Frame2d;
    default:
        return MT::Frame2b;
    }
}

} // namespace

const FrameDef& frameDef(MessageType type)
{
    const size_t index = static_cast<size_t>(type);

    if (index >= static_cast<size_t>(kMessageTypeCount)) {
        return kFrames[static_cast<size_t>(MT::FrameUnknownMtx)];
    }

    return kFrames[index];
}

const char* frameName(MessageType type)
{
    return frameDef(type).number;
}

bool isFrameElement(char letter)
{
    return (letter != '\0') && (std::strchr(kElementLetters, letter) != nullptr);
}

MessageType decodeMessageType(const FrameDigits& d, Direction direction, bool callAck)
{
    if (isTowardsMtx(direction))
    {
        switch (d[3])
        {
        case 0:
            return MT::Frame15;
        case 1:
            return callAck ? MT::Frame10a : MT::Frame10b;
        case 2:
            return MT::Frame26;
        case 4:
            return MT::Frame27;
        case 6:
            return MT::Frame10c;
        case 7:
            if (d[11] == 0) {
                return MT::Frame14a;
            }

            if (d[11] == 15) {
                return MT::Frame14b;
            }

            break;
        case 8:
            return (d[11] == 2) ? MT::Frame13b : MT::Frame13a;
        case 9:
            // channel status information, the status A is the digit after BS indication and idle
            switch (d[7])
            {
            case 2:
            case 6:
                return MT::Frame25_2;
            case 14:
                return MT::Frame25_3;
            case 7:
            case 8:
                return MT::Frame25_4;
            default:
                return MT::Frame25_1;
            }
        case 10:
            return MT::Frame10d;
        case 11:
            return MT::Frame12;
        case 12:
            return MT::Frame16;
        case 13:
            return MT::Frame28;
        case 14:
            return MT::Frame11a;
        case 15:
            return MT::Frame11b;
        default:
            break;      // 3 and 5 are spare
        }

        return MT::FrameUnknownBsMs;
    }

    switch (d[3])
    {
    case 0:
        return MT::Frame6;
    case 1:
        return MT::Frame8;
    case 2:
        return MT::Frame5c;
    case 3:
        return (d[6] == 15) ? MT::Frame21b : MT::Frame4;
    case 4:
        // No discriminator in the standard: any information after the channel and area makes it
        // 2e. But H1 = 14 marks additional information (battery saving, band advertisement),
        // which a base station sends on every idle 1b; 14 is no country digit of a 2e.
        if (d[6] == 14) {
            return MT::Frame1b;
        }
        if (d[6] || d[7] || d[8] || d[9] || d[10] || d[11] || d[12]) {
            return MT::Frame2e;
        }

        return MT::Frame1b;
    case 5:
        if (d[6] == 15) {
            return MT::Frame21c;
        }

        return isAreaCode(d) ? MT::Frame3b : MT::Frame3a;
    case 6:
        return (d[13] == 0) ? MT::Frame5b : MT::Frame5a;
    case 7:
        return isAreaCode(d) ? MT::Frame4b : MT::Frame3d;
    case 8:
        return MT::Frame7;
    case 9:
        return MT::Frame3c;
    case 10:
        return MT::Frame30;
    case 11:
    case 13:
    {
        // two calling channels: the prefix of all signalling on them
        const MessageType type = callingChannelFrame(d);

        if (type == MT::Frame1a) {
            return (d[3] == 11) ? MT::Frame1aA : MT::Frame1aB;
        }

        return type;
    }
    case 12:
        return callingChannelFrame(d);
    case 14:
        return (d[6] == 15) ? MT::Frame22 : MT::FrameUnknownMtx;
    case 15:
        if (d[6] != 15) {
            return MT::FrameUnknownMtx;
        }

        switch (d[10])
        {
        case 3:
            return MT::Frame20_1;
        case 6:
        case 13:
            return MT::Frame20_3;
        case 7:
        case 14:
            return MT::Frame20_4;
        case 15:
            return MT::Frame20_5;
        default:
            return MT::Frame20_2;
        }
    default:
        break;
    }

    return MT::FrameUnknownMtx;
}

FrameDigits assembleDigits(const Frame& frame)
{
    const FrameDef& def = frameDef(frame.mt);
    FrameDigits digits{};

    for (int i = 15; i >= 0; i--)
    {
        const char letter = def.digits[i];

        if (letter == '-')
        {
            digits[i] = 0;
            continue;
        }

        uint64_t value = (letter == 'P') ? static_cast<uint64_t>(def.prefix) : elementValue(frame, letter);

        digits[i] = static_cast<uint8_t>(value & 0xf);
        value >>= 4;

        // the digits to the left with the same letter hold the higher part of the value
        for (int j = i - 1; j >= 0; j--)
        {
            if (def.digits[j] != letter) {
                break;
            }

            digits[j] = static_cast<uint8_t>(value & 0xf);
            value >>= 4;
            i--;
        }
    }

    return digits;
}

Frame disassembleDigits(const FrameDigits& digits, Direction direction, bool callAck)
{
    Frame frame;
    frame.mt = decodeMessageType(digits, direction, callAck);

    const FrameDef& def = frameDef(frame.mt);

    for (int i = 0; i < 16; i++)
    {
        const char letter = def.digits[i];

        if (letter == '-') {
            continue;
        }

        uint64_t value = digits[i];

        for (int j = i + 1; j < 16; j++)
        {
            if (def.digits[j] != letter) {
                break;
            }

            value = (value << 4) | digits[j];
            i++;
        }

        setElement(frame, letter, value);
    }

    return frame;
}

FrameBits encodeFrame(const Frame& frame)
{
    static const char kSync[] = "10101010101010111100010010";     // bit sync 101010101010101, frame sync 11100010010
    static_assert(sizeof(kSync) - 1 == kBitSyncBits + kFrameSyncBits, "synchronisation length");

    const FrameDigits digits = assembleDigits(frame);
    MessageBytes message;

    for (int i = 0; i < 8; i++) {
        message[i] = static_cast<uint8_t>((digits[2 * i] << 4) | digits[2 * i + 1]);
    }

    const EncodedBits encoded = hagelbargerEncode(message);
    FrameBits bits;

    for (int i = 0; i < kBitSyncBits + kFrameSyncBits; i++) {
        bits[i] = static_cast<uint8_t>(kSync[i] - '0');
    }

    for (int i = 0; i < kEncodedBits; i++) {
        bits[kBitSyncBits + kFrameSyncBits + i] = encoded[i];
    }

    return bits;
}

Frame decodeFrame(const EncodedBits& bits, Direction direction, bool callAck)
{
    const MessageBytes message = hagelbargerDecode(bits);
    FrameDigits digits;

    for (int i = 0; i < 8; i++)
    {
        digits[2 * i] = message[i] >> 4;
        digits[2 * i + 1] = message[i] & 0x0f;
    }

    return disassembleDigits(digits, direction, callAck);
}

int encodeANumber(Frame& frame, int index, modemcellular::NumberType type, const std::string& number,
                  System system, int channel, int power, uint8_t trafficArea)
{
    using modemcellular::NumberType;

    const int numberLength = static_cast<int>(number.size());
    // 0..5 digits need one frame, 6..12 two frames, ...
    const int nframes = (numberLength + 8) / 7;

    index %= nframes;

    // the second frame starts at digit 5, the following ones 7 digits later
    const int numberOffset = index ? index * 7 - 2 : 0;
    int shift;

    frame.mt = MT::Frame8;
    frame.channelNo = static_cast<uint16_t>(encodeChannel(system, channel, power));
    frame.trafficArea = static_cast<uint8_t>(encodeTrafficArea(system, channel, trafficArea));
    frame.seqNumber = static_cast<uint8_t>(index);

    if (index == 0)
    {
        uint64_t numberType = 0;

        switch (type)
        {
        case NumberType::NotAvailable:
            numberType = 3;
            break;
        case NumberType::Anonymous:
            numberType = 4;
            break;
        case NumberType::Unknown:
        case NumberType::Subscriber:
            numberType = 0;
            break;
        case NumberType::National:
            numberType = 1;
            break;
        case NumberType::International:
            numberType = 2;
            break;
        }

        // H1 is the index of the last frame, H2 the type, H3..H7 the first five digits
        frame.additionalInfo = (static_cast<uint64_t>(nframes - 1) << 24) | (numberType << 20);
        shift = 16;
    }
    else
    {
        // H1..H7 are digits
        frame.additionalInfo = 0;
        shift = 24;
    }

    for (int i = numberOffset; (i < numberLength) && (shift >= 0); i++, shift -= 4)
    {
        const char c = number[i];
        uint64_t digit;

        if (c >= '1' && c <= '9') {
            digit = static_cast<uint64_t>(c - '0');
        } else if (c == '0') {
            digit = 10;
        } else {
            digit = 13;     // '+' and illegal digits
        }

        frame.additionalInfo |= digit << shift;
    }

    // modulo 256 sum of MH1, H2H3, H4H5 and H6H7
    uint8_t sum = static_cast<uint8_t>((frame.seqNumber << 4) | (frame.additionalInfo >> 24));
    sum = static_cast<uint8_t>(sum + (frame.additionalInfo >> 16));
    sum = static_cast<uint8_t>(sum + (frame.additionalInfo >> 8));
    sum = static_cast<uint8_t>(sum + frame.additionalInfo);
    frame.checksum = sum;

    // next frame, or the first one again after the last
    if (++index == nframes) {
        index = 0;
    }

    return index;
}

} // namespace modemnmt
