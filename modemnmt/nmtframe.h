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

#ifndef MODEMNMT_NMTFRAME_H_
#define MODEMNMT_NMTFRAME_H_

#include <array>
#include <cstdint>
#include <string>

#include "calltypes.h"
#include "export.h"
#include "hagelbarger.h"
#include "nmtnumbers.h"

namespace modemnmt
{

// Signalling frames of NMT-450 and NMT-900 (NMT Doc 450-1 para 4.3.2, NMT Doc 900-1 para 4.3.2).
// A frame is 16 hexadecimal digits (13 in the shortened frames, padded with idle digits). The
// first digit group is the channel number N1N2N3 and the fourth digit is the prefix P, which
// together with the direction and a few digits tells the frame type.

enum class Direction
{
    MtxToMs,
    MsToMtx,
    MtxToBs,
    BsToMtx,
    MtxToXx,    // from the exchange to a mobile station or a base station
    XxToMtx     // from a mobile station or a base station to the exchange
};

enum class MessageType : uint8_t
{
    Frame1a = 0,
    Frame1aA,       // 1a'  calling channel indication for group A
    Frame1aB,       // 1a'' calling channel indication for group B
    Frame1b,
    Frame2a,
    Frame2b,
    Frame2c,
    Frame2d,
    Frame2e,
    Frame2f,
    Frame3a,
    Frame3b,
    Frame3c,
    Frame3d,
    Frame4,
    Frame4b,
    Frame5a,
    Frame5b,
    Frame5c,
    Frame6,
    Frame7,
    Frame8,
    Frame10a,
    Frame10b,
    Frame10c,
    Frame10d,
    Frame11a,
    Frame11b,
    Frame12,
    Frame13a,
    Frame13b,
    Frame14a,
    Frame14b,
    Frame15,
    Frame16,
    Frame20_1,
    Frame20_2,
    Frame20_3,
    Frame20_4,
    Frame20_5,
    Frame21b,
    Frame21c,
    Frame22,
    Frame25_1,
    Frame25_2,
    Frame25_3,
    Frame25_4,
    Frame26,
    Frame27,
    Frame28,
    Frame30,
    FrameUnknownMtx,    // prefix of no frame, from the exchange
    FrameUnknownBsMs    // prefix of no frame, towards the exchange
};

constexpr int kMessageTypeCount = static_cast<int>(MessageType::FrameUnknownBsMs) + 1;

// The information elements of a frame, named by the letter used in the frame table.
struct Frame
{
    MessageType mt = MessageType::Frame1a;
    uint16_t channelNo = 0;         // N  channel number
    uint16_t tcNo = 0;              // n  traffic channel number
    uint8_t trafficArea = 0;        // Y  traffic area
    uint8_t msCountry = 0;          // Z  mobile subscriber country
    uint32_t msNumber = 0;          // X  mobile subscriber number
    uint8_t tariffClass = 0;        // Q
    uint32_t lineSignal = 0;        // L
    uint32_t digit = 0;             // S  digit signal
    uint64_t idle = 0;              // J
    uint8_t chanAct = 0;            // A  channel activation, status information
    uint32_t measOrder = 0;         // V  management order
    uint32_t meas = 0;              // r  measurement results
    uint8_t prefix = 0;             // P
    uint32_t supervisory = 0;       // f  supervisory signal
    uint16_t msPassword = 0;        // K
    uint8_t areaInfo = 0;           // T
    uint64_t additionalInfo = 0;    // H
    uint32_t rand = 0;              // C  random challenge
    uint64_t sres = 0;              // R  signed response
    uint16_t limitStrengthEval = 0; // l
    uint16_t c = 0;                 // c
    uint8_t seqNumber = 0;          // M
    uint16_t checksum = 0;          // W
    uint64_t waitingInfo = 0;       // F  messages waiting
};

struct FrameDef
{
    MessageType type;
    const char* digits;     // one letter per digit, '-' for digits without an element
    Direction direction;
    int prefix;
    const char* number;     // "1a'", "10d", ... ("" for the unknown types)
    const char* description;
};

// The frame table entry of a type.
MODEMNMT_API const FrameDef& frameDef(MessageType type);
MODEMNMT_API const char* frameName(MessageType type);
// True for the letters that name an information element of Frame.
MODEMNMT_API bool isFrameElement(char letter);

using FrameDigits = std::array<uint8_t, 16>;

// Which frame the digits are. direction is where the frame comes from and goes to; XxToMtx
// covers a mobile station or a base station. A frame with prefix 1 is 10a if callAck is set
// (a call acknowledgement on a calling channel, 13 digits) and 10b otherwise; nothing in the
// digits tells them apart.
MODEMNMT_API MessageType decodeMessageType(const FrameDigits& digits, Direction direction, bool callAck);
// The digits of a frame; the prefix comes from the frame table.
MODEMNMT_API FrameDigits assembleDigits(const Frame& frame);
MODEMNMT_API Frame disassembleDigits(const FrameDigits& digits, Direction direction, bool callAck);

// On the air: 15 bits of bit synchronisation, 11 bits of frame synchronisation, 140 encoded bits.
constexpr int kBitSyncBits = 15;
constexpr int kFrameSyncBits = 11;
constexpr int kFrameBits = kBitSyncBits + kFrameSyncBits + kEncodedBits;
using FrameBits = std::array<uint8_t, kFrameBits>;     // one bit per element, in transmission order

MODEMNMT_API FrameBits encodeFrame(const Frame& frame);
// The 140 bits after the synchronisation. There is no error detection, so a frame always
// decodes; the receiver has to check the information elements.
MODEMNMT_API Frame decodeFrame(const EncodedBits& bits, Direction direction, bool callAck);

// Frame 8 carries the number of the calling subscriber, five digits in the first frame of a
// sequence and seven in the following ones (NMT Doc 450-1 para 4.3.3.13). Fills in frame
// number index of the sequence and returns the index of the next one, which is 0 after the
// last.
MODEMNMT_API int encodeANumber(Frame& frame, int index, modemcellular::NumberType type, const std::string& number,
                               System system, int channel, int power, uint8_t trafficArea);

} // namespace modemnmt

#endif // MODEMNMT_NMTFRAME_H_
