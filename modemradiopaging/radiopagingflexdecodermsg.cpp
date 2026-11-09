///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

// Address walk and message decoding of a phase of a FLEX frame

#include "radiopagingflexdecoder.h"

#include <algorithm>

namespace {

const int wordsPerFrame = RadioPagingFlex::m_wordsPerFrame;

// Address word ranges (21 bit address words)
const uint32_t addrLong1Max = 0x008000;
const uint32_t addrShortMin = 0x008001;
const uint32_t addrShortMax = 0x1e0000;
const uint32_t addrLong3Min = 0x1e0001;
const uint32_t addrLong3Max = 0x1e8000;
const uint32_t addrLong4Min = 0x1e8001;
const uint32_t addrLong4Max = 0x1f0000;
const uint32_t addrReserved1Min = 0x1f0001;
const uint32_t addrReserved1Max = 0x1f27ff;
const uint32_t addrInfoMin = 0x1f2800;
const uint32_t addrInfoMax = 0x1f67ff;
const uint32_t addrNetworkMin = 0x1f6800;
const uint32_t addrNetworkMax = 0x1f77ff;
const uint32_t addrTempMin = 0x1f7800;
const uint32_t addrTempMax = 0x1f780f;
const uint32_t addrOperatorMin = 0x1f7810;
const uint32_t addrOperatorMax = 0x1f781f;
const uint32_t addrReserved2Min = 0x1f7820;
const uint32_t addrReserved2Max = 0x1f7ffe;
const uint32_t addrLong2Min = 0x1f7fff;
const uint32_t addrLong2Max = 0x1ffffe;
const uint32_t shortOffset = 32768;

const char bcdChars[17] = "0123456789.U -][";

enum AddressClass {
    ClassLong1,
    ClassShort,
    ClassLong2,
    ClassLong3,
    ClassLong4,
    ClassReserved,
    ClassInfo,
    ClassNetwork,
    ClassTemporary,
    ClassOperator,
    ClassUnknown
};

AddressClass classify(uint32_t aw)
{
    if ((aw >= 1) && (aw <= addrLong1Max)) return ClassLong1;
    if ((aw >= addrShortMin) && (aw <= addrShortMax)) return ClassShort;
    if ((aw >= addrLong3Min) && (aw <= addrLong3Max)) return ClassLong3;
    if ((aw >= addrLong4Min) && (aw <= addrLong4Max)) return ClassLong4;
    if ((aw >= addrReserved1Min) && (aw <= addrReserved1Max)) return ClassReserved;
    if ((aw >= addrInfoMin) && (aw <= addrInfoMax)) return ClassInfo;
    if ((aw >= addrNetworkMin) && (aw <= addrNetworkMax)) return ClassNetwork;
    if ((aw >= addrTempMin) && (aw <= addrTempMax)) return ClassTemporary;
    if ((aw >= addrOperatorMin) && (aw <= addrOperatorMax)) return ClassOperator;
    if ((aw >= addrReserved2Min) && (aw <= addrReserved2Max)) return ClassReserved;
    if ((aw >= addrLong2Min) && (aw <= addrLong2Max)) return ClassLong2;
    return ClassUnknown;
}

bool isLongClass(AddressClass c)
{
    return (c == ClassLong1) || (c == ClassLong2) || (c == ClassLong3) || (c == ClassLong4);
}

RadioPagingFlexDecoder::AddressType addressType(AddressClass c)
{
    switch (c)
    {
    case ClassShort: return RadioPagingFlexDecoder::AddressShort;
    case ClassLong1:
    case ClassLong2:
    case ClassLong3:
    case ClassLong4: return RadioPagingFlexDecoder::AddressLong;
    case ClassNetwork: return RadioPagingFlexDecoder::AddressNetwork;
    case ClassTemporary: return RadioPagingFlexDecoder::AddressTemporary;
    case ClassOperator: return RadioPagingFlexDecoder::AddressOperator;
    case ClassInfo: return RadioPagingFlexDecoder::AddressInfoService;
    case ClassReserved: return RadioPagingFlexDecoder::AddressReserved;
    case ClassUnknown: break;
    }

    return RadioPagingFlexDecoder::AddressUnknown;
}

// Capcode of the two words of a long address, 0 if the pair is not a valid set
uint64_t longCapcode(uint32_t w1, uint32_t w2)
{
    AddressClass c1 = classify(w1);
    AddressClass c2 = classify(w2);

    if ((c1 == ClassLong1) && (c2 == ClassLong2)) {
        return (uint64_t) w1 + (uint64_t) (2097151u - w2) * shortOffset + 2068480ULL;
    }
    if ((c1 == ClassLong1) && ((c2 == ClassLong3) || (c2 == ClassLong4))) {
        return (uint64_t) w1 + (uint64_t) (w2 - 1933312u) * shortOffset + 2068480ULL;
    }
    if ((c1 == ClassLong2) && ((c2 == ClassLong3) || (c2 == ClassLong4))) {
        return (uint64_t) (w1 - 2064383u) + (uint64_t) (w2 - 1867776u) * shortOffset + 2068479ULL;
    }

    return 0;
}

uint32_t groupSums(uint32_t word)
{
    return (word & 0xff) + ((word >> 8) & 0xff) + ((word >> 16) & 0x1f);
}

const char *operatorSubtype(uint32_t aw)
{
    switch (aw & 0xf)
    {
    case 0: return "all";
    case 1: return "home";
    case 2: return "roaming";
    case 3: return "ssid";
    case 4: return "time";
    case 14: return "ssid_change";
    case 15: return "system_event";
    default: return "reserved";
    }
}

const char *operatorCategory(uint32_t aw)
{
    uint32_t lsb = aw & 0xf;

    if (lsb <= 4) {
        return "system_message";
    }
    if (lsb == 14) {
        return "ssid_change";
    }
    if (lsb == 15) {
        return "system_event";
    }
    return "reserved";
}

char statusChar(uint8_t status)
{
    switch (status)
    {
    case 1: return '.';
    case 2: return 'c';
    case 3: return 'X';
    default: return '_';
    }
}

} // namespace

RadioPagingFlexDecoder::Check RadioPagingFlexDecoder::combineChecks(Check a, Check b)
{
    if ((a == CheckFailed) || (b == CheckFailed)) {
        return CheckFailed;
    }
    if ((a == CheckIncomplete) || (b == CheckIncomplete)) {
        return CheckIncomplete;
    }
    if ((a == CheckOk) || (b == CheckOk)) {
        return CheckOk;
    }
    return CheckNone;
}

// Walk the address field. Each address has a vector in the vector field in the same order (one word for
// a short address, two for a long address). Addresses at the end of the address field that have no
// valid vector are tone only
void RadioPagingFlexDecoder::walkAddresses(unsigned addressStart, unsigned vectorStart, unsigned priorityWords)
{
    BiwState& biw = pol().m_biw;
    int addrIdx = (int) addressStart;
    int vecCount = 0;
    int priorityEnd = (int) (addressStart + priorityWords);

    // Number of vector slots up to the last one with a valid checksum
    int maxVec = (int) (vectorStart - addressStart);

    if (biw.m_sysMsgType >= 0) {
        maxVec++;
    }
    if (maxVec > wordsPerFrame - (int) vectorStart) {
        maxVec = wordsPerFrame - (int) vectorStart;
    }

    int lastValid = -1;

    for (int vi = 0; vi < maxVec; vi++)
    {
        int wi = (int) vectorStart + vi;

        if ((m_w[wi].m_status == WordUncorrectable) || (m_w[wi].m_status == WordNotReceived)) {
            continue;
        }
        if (RadioPagingFlex::checksumValid(m_w[wi].m_data)) {
            lastValid = vi;
        }
    }

    int validVectors = lastValid + 1;
    int vectorBound = wordsPerFrame;

    for (; addrIdx < (int) vectorStart; addrIdx++)
    {
        int j = (int) vectorStart + vecCount;
        AddressContext a;

        a.m_priority = (addrIdx < priorityEnd);
        a.m_index = addrIdx;

        if (j >= wordsPerFrame) {
            break;
        }

        if ((m_w[addrIdx].m_status == WordUncorrectable) || (m_w[addrIdx].m_status == WordNotReceived))
        {
            vecCount++;
            continue;
        }

        uint32_t aw = m_w[addrIdx].m_data;
        AddressClass cls = classify(aw);

        a.m_word = aw;
        a.m_type = addressType(cls);
        a.m_long = isLongClass(cls);

        if (cls == ClassTemporary) {
            a.m_tempSlot = (int) ((aw - addrTempMin) & 0xf);
        }

        if (a.m_long)
        {
            if (addrIdx + 1 >= (int) vectorStart) {
                break;
            }

            if ((m_w[addrIdx + 1].m_status == WordUncorrectable) || (m_w[addrIdx + 1].m_status == WordNotReceived))
            {
                addrIdx++;
                vecCount += 2;
                continue;
            }

            a.m_capcode = longCapcode(aw, m_w[addrIdx + 1].m_data);
            addrIdx++;

            if (a.m_capcode == 0)
            {
                vecCount += 2;
                continue;
            }
        }
        else
        {
            a.m_capcode = (uint64_t) aw - shortOffset;
        }

        // No valid vector left: tone only address
        if ((vecCount >= validVectors) || (j >= vectorBound))
        {
            if (a.m_capcode == 1) {
                continue;
            }

            Message msg = newMessage(a);

            msg.m_type = TypeTone;
            countWords(msg, a.m_index, a.m_index + (a.m_long ? 1 : 0));
            emit(msg);
            continue;
        }

        vecCount += a.m_long ? 2 : 1;

        if ((m_w[j].m_status == WordUncorrectable) || (m_w[j].m_status == WordNotReceived)) {
            continue;
        }

        // The message field starts where the vector field ends: the lowest message start of the vectors
        // seen so far. Vector slots from there on are message words
        if (RadioPagingFlex::checksumValid(m_w[j].m_data))
        {
            unsigned type = (m_w[j].m_data >> 4) & 7;
            int start = (int) ((m_w[j].m_data >> 7) & 0x7f);

            if ((type != 1) && (type != 2) && (start > j + (a.m_long ? 1 : 0))) {
                vectorBound = std::min(vectorBound, start);
            }
        }

        if (a.m_long && ((j + 1 >= wordsPerFrame) || (m_w[j + 1].m_status == WordUncorrectable)
            || (m_w[j + 1].m_status == WordNotReceived))) {
            continue;
        }

        processVector(a, j);
    }

    // System message announced by a block information word: its vector is the last vector word
    if (biw.m_sysMsgType >= 0)
    {
        int type = biw.m_sysMsgType;
        int index = (int) vectorStart + validVectors - 1;

        if ((index > (int) vectorStart + vecCount - 1) && (index < wordsPerFrame)
            && (m_w[index].m_status != WordUncorrectable) && (m_w[index].m_status != WordNotReceived))
        {
            AddressContext a;

            a.m_word = addrOperatorMin + (uint32_t) type;
            a.m_capcode = (uint64_t) a.m_word - shortOffset;
            a.m_type = AddressOperator;
            a.m_index = -1;
            biw.m_sysMsgType = -1;
            processVector(a, index);
        }
    }
}

void RadioPagingFlexDecoder::handleTemporaryAddress(const AddressContext& a, int j, int vecType, int mw1, Message& proto)
{
    TempSlot& t = pol().m_temp[a.m_tempSlot];
    bool complete = true;

    // A continued alphanumeric or secure message keeps the assignment
    if ((vecType == 5) || (vecType == 0))
    {
        int hi = a.m_long ? (j + 1) : mw1;

        if ((hi <= 87) && wordOk(hi) && ((m_w[hi].m_data >> 10) & 1)) {
            complete = false;
        }
    }

    proto.m_tempSlot = a.m_tempSlot;

    if (t.m_active)
    {
        proto.m_groupTracked = true;
        proto.m_groupMembers = t.m_members;

        if (complete) {
            t = TempSlot();
        }
    }
}

// Decode the vector at word j and the message it points to
void RadioPagingFlexDecoder::processVector(const AddressContext& a, int j)
{
    uint32_t viw = m_w[j].m_data;
    int vecType = (viw >> 4) & 7;
    int mw1 = (viw >> 7) & 0x7f;
    int length;

    // Numeric vectors have a 3 bit length (words - 1), the others a 7 bit length
    if ((vecType == 3) || (vecType == 4) || (vecType == 7)) {
        length = (int) ((viw >> 14) & 7) + 1;
    } else {
        length = (int) ((viw >> 14) & 0x7f);
    }

    int mw2 = mw1 + (length - 1);

    // The first message word of a long address is in the second vector word
    if (a.m_long && (length > 0) && (vecType != 1) && (vecType != 2)) {
        mw2--;
    }

    Message proto = newMessage(a);

    if (a.m_index >= 0) {
        countWords(proto, a.m_index, a.m_index + (a.m_long ? 1 : 0));
    }
    countWords(proto, j, j + (a.m_long ? 1 : 0));

    if (vecType == 1)
    {
        decodeShortInstruction(proto, a, j);
        return;
    }

    if ((mw1 == 0) && (mw2 == 0)) {
        return;
    }

    // Network address: area, zones and traffic flags in the first message word
    if ((a.m_type == AddressNetwork) && (vecType == 0) && (mw1 <= 87) && wordOk(mw1))
    {
        uint32_t mw = m_w[mw1].m_data;

        proto.m_type = TypeNetwork;
        proto.m_areaId = mw & 0xfff;
        proto.m_coverageZones = (mw >> 12) & 0xf;
        proto.m_trafficFlags = (mw >> 16) & 0x1f;
        proto.m_overload = ((mw >> 16) & 1) != 0;
        countWords(proto, mw1, mw1);
        setRaw(proto, std::vector<uint32_t>(1, mw));
        emit(proto);
        return;
    }

    // Operator messaging address: the message is decoded as any other message
    if (a.m_type == AddressOperator)
    {
        proto.m_operatorSubtype = operatorSubtype(a.m_word);
        proto.m_operatorCategory = operatorCategory(a.m_word);
        proto.m_operatorPayload = ((mw1 <= 87) && wordOk(mw1)) ? (int64_t) m_w[mw1].m_data : -1;
    }

    if (a.m_type == AddressReserved) {
        return;
    }

    if (a.m_type == AddressTemporary) {
        handleTemporaryAddress(a, j, vecType, mw1, proto);
    }

    switch (vecType)
    {
    case 5:
    case 0:
        decodeAlpha(proto, a, j, vecType, mw1, mw2);
        break;
    case 3:
    case 4:
    case 7:
        decodeNumeric(proto, a, j, vecType, mw1, mw2);
        break;
    case 2:
        decodeShortMessage(proto, a, j);
        break;
    case 6:
        decodeBinary(proto, a, j, mw1, mw2);
        break;
    default:
        break;
    }
}

// Short instruction: 14 data bits in the vector. Type 0 assigns a temporary address to the pager of the
// address word, type 1 announces a system event
void RadioPagingFlexDecoder::decodeShortInstruction(Message& proto, const AddressContext& a, int j)
{
    uint32_t data = (m_w[j].m_data >> 7) & 0x3fff;
    unsigned type = data & 7;

    proto.m_type = TypeShortInstruction;
    proto.m_instructionType = (int) type;
    proto.m_instructionData = data;
    setRaw(proto, std::vector<uint32_t>(1, m_w[j].m_data));

    if (type == 0)
    {
        unsigned slot = (data >> 10) & 0xf;
        unsigned target = (data >> 3) & 0x7f;
        TempSlot& t = pol().m_temp[slot];

        // A new assignment on a slot in use replaces the old group
        if (!t.m_active)
        {
            t = TempSlot();
            t.m_targetFrame = target;
            t.m_setupFrame = m_ctx.m_frame;
            t.m_setupCycle = m_ctx.m_cycle;
            t.m_active = true;
        }

        if (std::find(t.m_members.begin(), t.m_members.end(), a.m_capcode) == t.m_members.end()
            && (t.m_members.size() < 8)) {
            t.m_members.push_back(a.m_capcode);
        }

        proto.m_instructionSlot = (int) slot;
        proto.m_instructionFrame = (int) target;
        proto.m_groupTracked = true;
        proto.m_groupMembers = t.m_members;
    }
    else if ((type == 1) && (a.m_type == AddressOperator))
    {
        proto.m_eventFlags = (data >> 3) & 0x7ff;
        proto.m_operatorSubtype = operatorSubtype(a.m_word);
        proto.m_operatorCategory = operatorCategory(a.m_word);
    }

    emit(proto);
}

// Short message: the message is in the vector word (two words for a long address)
void RadioPagingFlexDecoder::decodeShortMessage(Message& proto, const AddressContext& a, int j)
{
    uint32_t viw = m_w[j].m_data;
    uint32_t t = (viw >> 7) & 3;
    uint32_t d = (viw >> 9) & 0xfff;
    std::vector<uint32_t> raw(1, viw);

    proto.m_type = TypeShortMessage;
    proto.m_shortType = (int) t;

    if (t == 0)
    {
        std::string digits;

        digits += bcdChars[d & 0xf];
        digits += bcdChars[(d >> 4) & 0xf];
        digits += bcdChars[(d >> 8) & 0xf];

        if (a.m_long && (j + 1 < wordsPerFrame))
        {
            uint32_t v2 = m_w[j + 1].m_data;

            raw.push_back(v2);
            digits += bcdChars[v2 & 0xf];
            digits += bcdChars[(v2 >> 4) & 0xf];
            digits += bcdChars[(v2 >> 8) & 0xf];
            digits += bcdChars[(v2 >> 12) & 0xf];
            digits += bcdChars[(v2 >> 16) & 0xf];
        }

        proto.m_text = digits;
        proto.m_toneOnly = (digits.find_first_not_of(' ') == std::string::npos);
    }
    else if ((t == 1) || (t == 2))
    {
        proto.m_shortSource = (int) (d & 7);

        if (t == 2)
        {
            proto.m_messageNumber = (int) ((d >> 3) & 0x3f);
            proto.m_retrieval = ((d >> 9) & 1) != 0;
        }
    }

    setRaw(proto, raw);
    emit(proto);
}

// Reassembly of fragments of alphanumeric, secure and binary messages
void RadioPagingFlexDecoder::handleFragment(Message& msg, const AddressContext& a, int vector, const FragmentInfo& info)
{
    const bool signatureUsed = (vector == 5) || (vector == 0);

    setRaw(msg, info.m_raw);

    switch (msg.m_fragment)
    {
    case FragmentUnknown:
        msg.m_complete = false;
        emit(msg);
        return;

    case FragmentComplete:
        reassemblyInvalidate(a.m_capcode, -1);
        emit(msg);
        return;

    case FragmentFirst:
    {
        reassemblyInvalidate(a.m_capcode, msg.m_messageNumber);

        int old = reassemblyFind(a.m_capcode, msg.m_messageNumber);

        if (old >= 0)
        {
            emitIncomplete(pol().m_reassembly[old]);
            pol().m_reassembly[old].m_active = false;
        }

        int index = reassemblyAlloc(a.m_capcode, msg.m_messageNumber, vector);
        Reassembly& slot = pol().m_reassembly[index];

        slot.m_signature = info.m_signature;
        slot.m_signatureSum = info.m_signatureSum;
        slot.m_signatureValid = info.m_wordsOk;
        slot.m_kanji = info.m_kanji;
        slot.m_text = msg.m_text;
        slot.m_raw = info.m_raw;
        slot.m_fragments = 1;
        slot.m_expected = 0;
        slot.m_message = msg;
        slot.m_message.m_fragmented = true;
        slot.m_message.m_isFragment = false;
        slot.m_message.m_complete = false;

        if (m_reportFragments)
        {
            Message fragment = msg;

            fragment.m_fragmented = true;
            fragment.m_isFragment = true;
            emit(fragment);
        }
        return;
    }

    case FragmentMiddle:
    case FragmentLast:
    {
        int index = reassemblyFind(a.m_capcode, msg.m_messageNumber);

        if (index < 0)
        {
            // The first fragment was not received
            msg.m_fragmented = true;
            msg.m_complete = false;
            emit(msg);
            return;
        }

        Reassembly& slot = pol().m_reassembly[index];

        if (msg.m_fragmentNumber != slot.m_expected) {
            slot.m_gap = true;
        }

        slot.m_signatureSum += info.m_signatureSum;

        if (!info.m_wordsOk) {
            slot.m_signatureValid = false;
        }

        slot.m_text += msg.m_text;
        slot.m_raw.insert(slot.m_raw.end(), info.m_raw.begin(), info.m_raw.end());
        slot.m_fragments++;
        slot.m_lastFrame = m_ctx.m_frame;
        slot.m_lastCycle = m_ctx.m_cycle;
        slot.m_expected = (msg.m_fragmentNumber + 1) % 3;

        Message& first = slot.m_message;

        first.m_cycle = m_ctx.m_cycle;
        first.m_frame = m_ctx.m_frame;
        first.m_wordsCorrected += msg.m_wordsCorrected;
        first.m_bitErrors += msg.m_bitErrors;
        first.m_wordsUncorrectable += msg.m_wordsUncorrectable;
        first.m_parityCorrections += msg.m_parityCorrections;
        first.m_dataBits += msg.m_dataBits;
        first.m_checksum = combineChecks(first.m_checksum, msg.m_checksum);
        first.m_recovered = first.m_recovered || msg.m_recovered;
        first.m_wordMap += "|" + msg.m_wordMap;

        if (msg.m_fragment == FragmentMiddle)
        {
            if (m_reportFragments)
            {
                Message fragment = msg;

                fragment.m_fragmented = true;
                fragment.m_isFragment = true;
                emit(fragment);
            }
            return;
        }

        Message out = first;

        out.m_text = slot.m_text;
        out.m_fragment = FragmentLast;
        out.m_fragmentNumber = msg.m_fragmentNumber;
        out.m_fragmentCount = slot.m_fragments;
        out.m_fragmentGap = slot.m_gap;
        out.m_complete = !slot.m_gap;
        out.m_isFragment = false;

        if (signatureUsed)
        {
            if (slot.m_signatureValid) {
                out.m_signature = (slot.m_signature == ((~slot.m_signatureSum) & 0x7f)) ? CheckOk : CheckFailed;
            } else {
                out.m_signature = CheckIncomplete;
            }
        }
        else
        {
            out.m_signature = CheckNone;
        }

        setRaw(out, slot.m_raw);
        slot.m_active = false;

        if (m_reportFragments)
        {
            Message fragment = msg;

            fragment.m_fragmented = true;
            fragment.m_isFragment = true;
            emit(fragment);
        }

        emit(out);
        return;
    }
    }
}

// Alphanumeric (vector 5) and secure (vector 0) messages. Header word: K (10 bits), C continued, F fragment
// number, N message number, and R, M (first fragment) or the secure type t. The first character slot of the first
// word after the header is the signature
void RadioPagingFlexDecoder::decodeAlpha(Message& proto, const AddressContext& a, int j, int vecType, int mw1, int mw2)
{
    if ((mw1 > 87) || (mw2 > 87))
    {
        m_status.m_invalidVectors++;
        return;
    }

    const bool secure = (vecType == 0);
    int hdrIdx = a.m_long ? (j + 1) : mw1;
    bool hdrOk = (hdrIdx < wordsPerFrame) && wordOk(hdrIdx);
    uint32_t hdr = 0;
    uint32_t hdrK = 0;
    int hdrC = 0;
    int hdrF = 0;
    int hdrN = 0;
    int hdrR = 0;
    int hdrM = 0;
    int secureType = -1;
    FragmentKind kind = FragmentUnknown;

    if (hdrOk)
    {
        hdr = m_w[hdrIdx].m_data;
        hdrK = hdr & 0x3ff;
        hdrC = (hdr >> 10) & 1;
        hdrF = (hdr >> 11) & 3;
        hdrN = (hdr >> 13) & 0x3f;

        if (hdrC == 0) {
            kind = (hdrF == 3) ? FragmentComplete : FragmentLast;
        } else {
            kind = (hdrF == 3) ? FragmentFirst : FragmentMiddle;
        }

        if (secure)
        {
            secureType = (hdr >> 19) & 3;
        }
        else if (hdrF == 3)
        {
            hdrR = (hdr >> 19) & 1;
            hdrM = (hdr >> 20) & 1;
        }
    }

    // Retransmission of a message with a message number
    bool recovered = false;
    int phaseIndex = m_name - 'A';

    if ((hdrF == 3) && !secure && hdrOk)
    {
        int count = std::max(0, mw2 - mw1 + 1);
        int result = checkRetransmission(a.m_capcode, hdrR != 0, hdrN, vecType, phaseIndex, &m_w[mw1], count,
            (hdrC == 0) ? 1 : 0, a.m_long, 0);

        if (result == 1) {
            return;
        }

        if (result == -1)
        {
            HistoryEntry *entry = historyFind(a.m_capcode, hdrN, vecType, phaseIndex);

            if (entry)
            {
                int copy = std::min(count, entry->m_wordCount);

                for (int i = 0; i < copy; i++) {
                    m_w[mw1 + i] = entry->m_words[i];
                }
            }

            recovered = true;
        }
    }

    // Characters
    const bool initial = (hdrF == 3);
    int startWord = a.m_long ? mw1 : (mw1 + 1);
    bool kanji = false;

    if (!secure)
    {
        if (initial)
        {
            kanji = m_kanji;
        }
        else
        {
            int slot = reassemblyFind(a.m_capcode, hdrN);
            kanji = (slot >= 0) ? pol().m_reassembly[slot].m_kanji : m_kanji;
        }
    }

    std::string text;
    uint32_t signature = 0;
    uint32_t sum = 0;
    bool wordsOk = true;

    if (kanji)
    {
        for (int w = startWord; (w <= mw2) && (w < wordsPerFrame); w++)
        {
            if ((m_w[w].m_status == WordUncorrectable) || (m_w[w].m_status == WordNotReceived))
            {
                wordsOk = false;

                if (!((w == startWord) && initial)) {
                    text += '?';
                }
                continue;
            }

            if ((w == startWord) && initial)
            {
                signature = m_w[w].m_data & 0x7f;
                continue;
            }

            uint32_t ch16 = m_w[w].m_data & 0xffff;

            if (ch16 == 0) {
                continue;
            }

            text += (char) ((ch16 >> 8) & 0xff);
            text += (char) (ch16 & 0xff);
        }
    }
    else
    {
        for (int w = startWord; (w <= mw2) && (w < wordsPerFrame); w++)
        {
            if ((m_w[w].m_status == WordUncorrectable) || (m_w[w].m_status == WordNotReceived))
            {
                wordsOk = false;

                int lost = ((w == startWord) && initial) ? 2 : 3;

                text.append(lost, '?');
                continue;
            }

            uint32_t dw = m_w[w].m_data;

            for (int slot = 0; slot < 3; slot++)
            {
                uint32_t ch = (dw >> (7 * slot)) & 0x7f;

                if ((slot == 0) && (w == startWord) && initial)
                {
                    signature = ch;
                    continue;
                }

                if (ch != 0x03) {
                    sum += ch;
                }
                if ((ch != 0x03) && (ch != 0)) {
                    text += (char) ch;
                }
            }
        }
    }

    Message msg = proto;

    msg.m_type = secure ? TypeSecure : TypeAlphanumeric;
    msg.m_text = text;
    msg.m_fragment = kind;
    msg.m_fragmentNumber = hdrF;
    msg.m_messageNumber = hdrOk ? hdrN : -1;
    msg.m_retrieval = (hdrR != 0);
    msg.m_mailDrop = (hdrM != 0);
    msg.m_secureType = secureType;
    msg.m_kanji = kanji;
    msg.m_recovered = recovered;
    msg.m_fragmented = (kind == FragmentFirst) || (kind == FragmentMiddle) || (kind == FragmentLast);

    // Signature of the whole message in the first fragment. Checked here for a message in one piece
    if (initial)
    {
        if (hdrC == 0) {
            msg.m_signature = !wordsOk ? CheckIncomplete : (signature == ((~sum) & 0x7f)) ? CheckOk : CheckFailed;
        }
    }

    // K: checksum of the words of this fragment (the K field of the header counts as zero)
    if (hdrOk)
    {
        uint32_t kSum = 0;
        bool allOk = true;

        if (a.m_long) {
            kSum += groupSums(hdr & ~0x3ffu);
        }

        for (int w = mw1; (w <= mw2) && (w < wordsPerFrame); w++)
        {
            if ((m_w[w].m_status == WordUncorrectable) || (m_w[w].m_status == WordNotReceived))
            {
                allOk = false;
                break;
            }

            uint32_t dw = m_w[w].m_data;

            if (!a.m_long && (w == mw1)) {
                dw &= ~0x3ffu;
            }

            kSum += groupSums(dw);
        }

        msg.m_checksum = !allOk ? CheckIncomplete : (hdrK == ((~kSum) & 0x3ff)) ? CheckOk : CheckFailed;
    }
    else
    {
        msg.m_checksum = CheckIncomplete;
    }

    // Registration acknowledgment: secure message with operation code "="
    if (secure && (secureType == 0) && !text.empty() && (text[0] == '=')) {
        msg.m_registrationAck = true;
    }

    // Message words: for a long address the first one is the second vector word
    FragmentInfo info;

    if (a.m_long)
    {
        info.m_raw.push_back(m_w[j + 1].m_data);
        msg.m_wordMap += statusChar(m_w[j + 1].m_status);
    }

    for (int w = mw1; (w <= mw2) && (w < wordsPerFrame); w++)
    {
        info.m_raw.push_back(m_w[w].m_data);
        msg.m_wordMap += statusChar(m_w[w].m_status);
    }

    countWords(msg, mw1, mw2);
    info.m_signature = signature;
    info.m_signatureSum = sum;
    info.m_wordsOk = wordsOk;
    info.m_kanji = kanji;

    handleFragment(msg, a, vecType, info);
}

// Numeric messages (vectors 3, 4, 7): 4-bit digits. The vector holds the length (words - 1) and the low
// 4 bits of the checksum, the first message word the high 2 bits. A numbered numeric message has N, R and S
// in the first word
void RadioPagingFlexDecoder::decodeNumeric(Message& proto, const AddressContext& a, int j, int vecType, int mw1, int mw2)
{
    if ((mw1 > 87) || (mw2 > 87))
    {
        m_status.m_invalidVectors++;
        return;
    }

    const bool numbered = (vecType == 7);
    int skipBits = numbered ? 10 : 2;
    int number = -1;
    int retrieval = 0;
    int special = 0;
    int body0 = a.m_long ? (j + 1) : mw1;

    if (numbered && wordOk(body0))
    {
        uint32_t nh = m_w[body0].m_data;

        number = (nh >> 2) & 0x3f;
        retrieval = (nh >> 8) & 1;
        special = (nh >> 9) & 1;
    }

    bool recovered = false;

    if (numbered && (number >= 0))
    {
        int count = std::max(0, mw2 - mw1 + 1);
        int result = checkRetransmission(a.m_capcode, retrieval != 0, number, vecType, m_name - 'A', &m_w[mw1], count, 1,
            a.m_long, 0);

        if (result == 1) {
            return;
        }

        if (result == -1)
        {
            HistoryEntry *entry = historyFind(a.m_capcode, number, vecType, m_name - 'A');

            if (entry)
            {
                int copy = std::min(count, entry->m_wordCount);

                for (int i = 0; i < copy; i++) {
                    m_w[mw1 + i] = entry->m_words[i];
                }
            }

            recovered = true;
        }
    }

    // Digits: a continuous bit stream, 4 bits each, after the overhead bits of the first word
    std::string digits;
    std::vector<int> words;
    std::vector<uint32_t> raw;
    int bitCount = 0;
    uint8_t nibble = 0;

    if (a.m_long) {
        words.push_back(j + 1);
    }
    for (int w = mw1; (w <= mw2) && (w < wordsPerFrame); w++) {
        words.push_back(w);
    }

    for (int w : words)
    {
        raw.push_back(m_w[w].m_data);

        if ((m_w[w].m_status == WordUncorrectable) || (m_w[w].m_status == WordNotReceived))
        {
            int lostBits = 21;

            if (bitCount < skipBits) {
                lostBits -= (skipBits - bitCount);
            }

            digits.append(lostBits / 4, '?');
            bitCount += 21;
            continue;
        }

        uint32_t dw = m_w[w].m_data;

        for (int b = 0; b < 21; b++)
        {
            if (bitCount < skipBits)
            {
                bitCount++;
                continue;
            }

            int pos = (bitCount - skipBits) % 4;

            if (pos == 0) {
                nibble = 0;
            }

            nibble |= ((dw >> b) & 1) << pos;

            if (pos == 3) {
                digits += bcdChars[nibble & 0xf];
            }

            bitCount++;
        }
    }

    // K checksum: low 4 bits in the vector, high 2 bits in the first message word
    uint32_t kLow = (m_w[j].m_data >> 17) & 0xf;
    uint32_t kHigh = wordOk(body0) ? (m_w[body0].m_data & 3) : 0;
    uint32_t kReceived = (kHigh << 4) | kLow;
    uint32_t kSum = 0;
    bool allOk = true;

    if (a.m_long)
    {
        if (wordOk(body0)) {
            kSum += groupSums(m_w[body0].m_data & ~3u);
        } else {
            allOk = false;
        }
    }

    for (int w = mw1; (w <= mw2) && (w < wordsPerFrame); w++)
    {
        if ((m_w[w].m_status == WordUncorrectable) || (m_w[w].m_status == WordNotReceived))
        {
            allOk = false;
            break;
        }

        uint32_t dw = m_w[w].m_data;

        if (!a.m_long && (w == mw1)) {
            dw &= ~3u;
        }

        kSum += groupSums(dw);
    }

    Message msg = proto;

    if (allOk)
    {
        kSum &= 0xff;
        kSum = (kSum & 0x3f) + (kSum >> 6);
        msg.m_checksum = (kReceived == ((~kSum) & 0x3f)) ? CheckOk : CheckFailed;
    }
    else
    {
        msg.m_checksum = CheckIncomplete;
    }

    if (vecType == 4) {
        msg.m_type = TypeSpecialNumeric;
    } else if (numbered) {
        msg.m_type = special ? TypeNumberedSpecialNumeric : TypeNumberedNumeric;
    } else {
        msg.m_type = TypeNumeric;
    }

    msg.m_text = digits;
    msg.m_messageNumber = number;
    msg.m_retrieval = (retrieval != 0);
    msg.m_special = (special != 0) || (vecType == 4);
    msg.m_recovered = recovered;

    for (int w : words) {
        msg.m_wordMap += statusChar(m_w[w].m_status);
    }

    countWords(msg, mw1, mw2);
    setRaw(msg, raw);
    emit(msg);
}

// Hex / binary messages (vector 6). First word: K (12 bits), C, F, N. The first fragment has a second
// header word: R, M, D, H, B, I and the signature. The data bits follow as a continuous stream of 4-bit
// digits. In the last fragment the bits after the data are the inverse of the last data bit
void RadioPagingFlexDecoder::decodeBinary(Message& proto, const AddressContext& a, int j, int mw1, int mw2)
{
    if ((mw1 > 87) || (mw2 > 87))
    {
        m_status.m_invalidVectors++;
        return;
    }

    static const char hexDigits[17] = "0123456789ABCDEF";
    int hexC = 0;
    int hexF = 0;
    int hexN = 0;
    int hexR = 0;
    int hexM = 0;
    int blocking = 0;
    bool displayRtl = false;
    bool headerMessage = false;
    bool statusInfo = false;
    uint32_t signature = 0;
    bool hasSignature = false;
    bool initial = false;
    FragmentKind kind = FragmentUnknown;
    int dataStart = mw1;
    int hdrIdx = a.m_long ? (j + 1) : mw1;
    bool hdrOk = (hdrIdx < wordsPerFrame) && wordOk(hdrIdx);

    if (hdrOk)
    {
        uint32_t hdr = m_w[hdrIdx].m_data;

        hexC = (hdr >> 12) & 1;
        hexF = (hdr >> 13) & 3;
        hexN = (hdr >> 15) & 0x3f;
        initial = (hexF == 3);

        if (hexC == 0) {
            kind = initial ? FragmentComplete : FragmentLast;
        } else {
            kind = initial ? FragmentFirst : FragmentMiddle;
        }

        // The second header word of the first fragment is the first word of the message field
        // for a long address, the second one for a short address
        int hdr2Idx = a.m_long ? mw1 : (mw1 + 1);

        if (initial)
        {
            dataStart = hdr2Idx + 1;

            if ((hdr2Idx <= mw2) && wordOk(hdr2Idx))
            {
                uint32_t hdr2 = m_w[hdr2Idx].m_data;

                hexR = hdr2 & 1;
                hexM = (hdr2 >> 1) & 1;
                displayRtl = ((hdr2 >> 2) & 1) != 0;
                headerMessage = ((hdr2 >> 3) & 1) != 0;
                blocking = (hdr2 >> 4) & 0xf;
                statusInfo = ((hdr2 >> 8) & 1) != 0;
                signature = (hdr2 >> 13) & 0xff;
                hasSignature = true;
            }
        }
        else
        {
            dataStart = a.m_long ? mw1 : (mw1 + 1);
        }
    }

    bool recovered = false;
    int phaseIndex = m_name - 'A';

    if (initial)
    {
        int count = std::max(0, mw2 - mw1 + 1);
        int result = checkRetransmission(a.m_capcode, hexR != 0, hexN, 6, phaseIndex, &m_w[mw1], count,
            (hexC == 0) ? 1 : 0, a.m_long, blocking);

        if (result == 1) {
            return;
        }

        if (result == -1)
        {
            HistoryEntry *entry = historyFind(a.m_capcode, hexN, 6, phaseIndex);

            if (entry)
            {
                int copy = std::min(count, entry->m_wordCount);

                for (int i = 0; i < copy; i++) {
                    m_w[mw1 + i] = entry->m_words[i];
                }
            }

            recovered = true;
        }
    }

    // Data bits as digits; lost words are left out of the stream
    std::string hex;
    int bitCount = 0;
    uint8_t nibble = 0;
    int lastDataWord = -1;

    for (int w = dataStart; (w <= mw2) && (w < wordsPerFrame); w++)
    {
        if ((m_w[w].m_status == WordUncorrectable) || (m_w[w].m_status == WordNotReceived)) {
            continue;
        }

        uint32_t dw = m_w[w].m_data;

        lastDataWord = w;

        for (int b = 0; b < 21; b++)
        {
            int pos = bitCount % 4;

            if (pos == 0) {
                nibble = 0;
            }

            nibble |= ((dw >> b) & 1) << pos;

            if (pos == 3) {
                hex += hexDigits[nibble & 0xf];
            }

            bitCount++;
        }
    }

    size_t digitCount = hex.size();

    auto bitsBefore = [&](int word) {
        int bits = 0;

        for (int w = dataStart; w < word; w++)
        {
            if ((m_w[w].m_status == WordUncorrectable) || (m_w[w].m_status == WordNotReceived)) {
                continue;
            }
            bits += 21;
        }

        return bits;
    };

    // Termination: an extra fill word, or fill bits at the end of the last word
    if ((lastDataWord >= 0) && (hexC == 0))
    {
        int stripWord = lastDataWord;

        if (stripWord > dataStart)
        {
            uint32_t lw = m_w[stripWord].m_data & 0x1fffff;

            if ((lw == 0) || (lw == 0x1fffff))
            {
                size_t digitsBefore = (size_t) (bitsBefore(stripWord) / 4);

                if ((digitsBefore > 0) && (digitsBefore <= digitCount))
                {
                    digitCount = digitsBefore;
                    stripWord = -1;
                }
            }
        }

        if ((stripWord >= dataStart) && (m_w[stripWord].m_status != WordUncorrectable)
            && (m_w[stripWord].m_status != WordNotReceived))
        {
            uint32_t lw = m_w[stripWord].m_data;
            uint32_t topBit = (lw >> 20) & 1;
            int fillStart = 21;

            for (int b = 20; b >= 0; b--)
            {
                if (((lw >> b) & 1) != topBit) {
                    break;
                }
                fillStart = b;
            }

            if ((fillStart < 21) && (fillStart > 0))
            {
                size_t realDigits = (size_t) ((bitsBefore(stripWord) + fillStart + 3) / 4);

                if (realDigits < digitCount) {
                    digitCount = realDigits;
                }
            }
        }
    }

    hex.resize(digitCount);

    // Signature of the data (8 bit sums, 1's complement), checked for a message in one piece
    Check signatureCheck = CheckNone;

    if (initial && hasSignature && (hexC == 0))
    {
        uint32_t sum = 0;
        int totalBits = (int) digitCount * 4;
        int bitPos = 0;
        uint8_t accum = 0;
        int accumBits = 0;
        bool lost = false;

        for (int w = dataStart; (w <= mw2) && (w < wordsPerFrame) && (bitPos < totalBits); w++)
        {
            if ((m_w[w].m_status == WordUncorrectable) || (m_w[w].m_status == WordNotReceived))
            {
                lost = true;
                continue;
            }

            for (int b = 0; (b < 21) && (bitPos < totalBits); b++)
            {
                accum |= (uint8_t) (((m_w[w].m_data >> b) & 1) << accumBits);
                accumBits++;
                bitPos++;

                if (accumBits == 8)
                {
                    sum += accum;
                    accum = 0;
                    accumBits = 0;
                }
            }
        }

        if (accumBits > 0) {
            sum += accum;
        }

        signatureCheck = lost ? CheckIncomplete : (signature == ((~sum) & 0xff)) ? CheckOk : CheckFailed;
    }

    Message msg = proto;

    msg.m_type = TypeBinary;
    msg.m_text = hex;
    msg.m_dataBits = (int) digitCount * 4;
    msg.m_fragment = kind;
    msg.m_fragmentNumber = hexF;
    msg.m_messageNumber = hdrOk ? hexN : -1;
    msg.m_retrieval = (hexR != 0);
    msg.m_mailDrop = (hexM != 0);
    msg.m_blocking = blocking;
    msg.m_displayRtl = displayRtl;
    msg.m_headerMessage = headerMessage;
    msg.m_statusInfo = statusInfo;
    msg.m_signature = signatureCheck;
    msg.m_recovered = recovered;
    msg.m_fragmented = (kind == FragmentFirst) || (kind == FragmentMiddle) || (kind == FragmentLast);

    // Continuation fragments take the blocking of the first fragment
    if (!initial && (blocking == 0) && hdrOk)
    {
        int slot = reassemblyFind(a.m_capcode, hexN);

        if (slot >= 0) {
            msg.m_blocking = pol().m_reassembly[slot].m_message.m_blocking;
        }
    }

    FragmentInfo info;

    if (a.m_long)
    {
        info.m_raw.push_back(m_w[j + 1].m_data);
        msg.m_wordMap += statusChar(m_w[j + 1].m_status);
    }

    for (int w = mw1; (w <= mw2) && (w < wordsPerFrame); w++)
    {
        info.m_raw.push_back(m_w[w].m_data);
        msg.m_wordMap += statusChar(m_w[w].m_status);
    }

    countWords(msg, mw1, mw2);
    handleFragment(msg, a, 6, info);
}
