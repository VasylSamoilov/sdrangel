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

#include "nmtdms.h"

#include <algorithm>
#include <cstring>
#include <exception>

namespace modemnmt
{

namespace
{

const char kBitSync[] = "101010101010101";
const char kFrameSync[] = "00101000111";
const uint16_t kSyncValue = 0x147;                  // 00101000111
const int kCtId = 73;
const int kCtRand = 82;
// bits after the frame sync: label + 8 words + 2 CRC words + 2 CRC bits, and two labels + parity
const int kDtBody = 101;
const int kRrBody = 20;
const int kIdNumber = 3;                            // the id in the identity frame
const double kBusyLimit = 3.0;                      // T: seconds an NR holds new frames back (Figure 1)
const double kGapTime = 0.081;                      // no FFSK after a frame of the fourth repetition
const int kGapPasses = 4;                           // repetitions before the gaps
const double kSpeechClosed = 0.200;                 // speech closed from the start of a frame (6.6)

void putBits(std::vector<uint8_t>& out, unsigned value, int count)
{
    for (int i = count - 1; i >= 0; i--) {
        out.push_back(static_cast<uint8_t>((value >> i) & 1));
    }
}

void putText(std::vector<uint8_t>& out, const char* text)
{
    for (; *text; text++) {
        out.push_back(static_cast<uint8_t>(*text - '0'));
    }
}

unsigned parity7(unsigned label)
{
    unsigned p = 0;

    for (int i = 0; i < 7; i++) {
        p ^= (label >> i) & 1;
    }

    return p;
}

} // namespace

uint16_t NmtDmsLink::crc16(const uint8_t* bits, int len)
{
    const uint16_t generator = 0x1021;
    uint16_t crc = 0;

    // the bits are followed by 16 zero bits: the remainder of the division is the check
    for (int i = 0; i < len + 16; i++)
    {
        const uint8_t bit = (i < len) ? bits[i] : 0;
        const bool top = (crc & 0x8000) != 0;

        crc = static_cast<uint16_t>((crc << 1) | bit);
        if (top) {
            crc ^= generator;
        }
    }

    return crc;
}

std::vector<uint8_t> NmtDmsLink::encodeDt(bool d, bool control, int n, const uint8_t data[kDataWords], bool bitSync)
{
    std::vector<uint8_t> out;
    uint8_t words[9];
    uint8_t checkBits[63 + 0];

    // label: d s p p n n n with pp = 11 (DT); s = 1 for user data
    words[0] = static_cast<uint8_t>(((d ? 1 : 0) << 6) | ((control ? 0 : 1) << 5) | (3 << 3) | (n & 7));
    for (int i = 0; i < kDataWords; i++) {
        words[1 + i] = data[i] & 0x7f;
    }
    for (int i = 0; i < 9; i++)
    {
        for (int j = 0; j < 7; j++) {
            checkBits[i * 7 + j] = static_cast<uint8_t>((words[i] >> (6 - j)) & 1);
        }
    }
    const uint16_t crc = crc16(checkBits, 63);

    if (bitSync) {
        putText(out, kBitSync);
    }
    putText(out, kFrameSync);
    for (int i = 0; i < 9; i++)
    {
        putBits(out, words[i], 7);
        putBits(out, 3, 2);
    }
    putBits(out, (crc >> 9) & 0x7f, 7);
    putBits(out, 3, 2);
    putBits(out, (crc >> 2) & 0x7f, 7);
    putBits(out, 3, 2);
    putBits(out, crc & 3, 2);

    return out;
}

std::vector<uint8_t> NmtDmsLink::encodeRr(bool d, bool nr, bool control, int n)
{
    std::vector<uint8_t> out;
    const unsigned label = (static_cast<unsigned>(d ? 1 : 0) << 6) | (static_cast<unsigned>(control ? 0 : 1) << 5)
                         | ((nr ? 2u : 1u) << 3) | static_cast<unsigned>(n & 7);
    const unsigned p = parity7(label);

    putText(out, kBitSync);
    for (int half = 0; half < 2; half++)
    {
        putText(out, kFrameSync);
        putBits(out, label, 7);
        putBits(out, 3, 2);
        putBits(out, label, 7);
        putBits(out, 3, 2);
        putBits(out, p, 1);
        putBits(out, p, 1);
    }

    return out;
}

NmtDmsLink::NmtDmsLink(bool eightBits) :
    m_eightBits(eightBits),
    m_eightBitsDefault(eightBits),
    m_randomSet(false),
    m_epoch(0),
    m_minQuality(0.65)
{
    reset();
    std::memcpy(m_identity, "F000000", 8);
    m_random[0] = 0x5a;
    m_random[1] = 0xa5;
    m_random[2] = 0x3c;
}

void NmtDmsLink::reset()
{
    m_eightBits = m_eightBitsDefault;
    m_epoch++;
    m_ready = true;
    m_nrSent = false;
    m_maskedBytes = 0;
    m_positiveResponse = false;
    m_pending.clear();
    m_lastRrLabel = -1;
    m_bitsSinceRr = 0;
    m_caller = false;
    m_dKnown = false;
    m_dTx = false;
    m_established = false;
    m_randAcked = false;
    m_randReceived = false;
    m_randSent = false;
    m_peerBusy = false;
    m_peerBusyTime = 0.0;
    m_peerIdentity.clear();
    m_txList.clear();
    m_nTx = 0;
    m_txIndex = 0;
    m_passes = 0;
    m_gapPending = false;
    m_gapTime = 0.0;
    m_speechClosed = 0.0;
    m_rrPending = false;
    m_rrNr = false;
    m_txBits.clear();
    m_txPos = 0;
    m_dataQueued = false;
    m_nRx = 0;
    m_shift = 0;
    // before the first bits there is no bad news
    std::fill(m_qualitySum, m_qualitySum + 16, 1.0);
    m_qualityPos = 0;
    m_qualityTotal = 16.0;
    m_inFrame = false;
    m_rxBits.clear();
    m_rxNeed = 0;
    m_framesReceived = 0;
    m_framesRejected = 0;
}

void NmtDmsLink::setIdentity(const char* id7)
{
    if (!id7) {
        return;
    }
    std::memset(m_identity, 0, sizeof(m_identity));
    std::strncpy(m_identity, id7, 7);
}

void NmtDmsLink::queueFrame(bool control, const uint8_t data[kDataWords])
{
    TxFrame frame;

    frame.control = control;
    frame.n = m_nTx;
    std::memcpy(frame.data, data, kDataWords);
    m_nTx = (m_nTx + 1) & 7;
    m_txList.push_back(frame);
}

// format: '8' or '7', the last byte of the frame
void NmtDmsLink::queueRand(char format)
{
    uint8_t data[kDataWords];

    if (!m_randomSet)
    {
        try
        {
            std::random_device device;

            for (uint8_t& r : m_random) {
                r = static_cast<uint8_t>(device());
            }
        }
        catch (const std::exception&)
        {
            // no entropy source: the numbers of the last call move on
            for (uint8_t& r : m_random) {
                r = static_cast<uint8_t>(r * 37 + 11);
            }
        }
    }

    data[0] = kCtRand;
    for (int i = 0; i < 3; i++)
    {
        data[1 + 2 * i] = static_cast<uint8_t>((m_random[i] >> 7) << 6);
        data[2 + 2 * i] = m_random[i] & 0x7f;
    }
    data[7] = static_cast<uint8_t>(format);
    queueFrame(true, data);
    m_randSent = true;
}

void NmtDmsLink::startAsCaller()
{
    uint8_t data[kDataWords];

    // the other side called first: the direction is settled
    if (m_dKnown) {
        return;
    }
    m_caller = true;
    m_dKnown = true;
    m_dTx = false;

    // identity, then the random number
    std::memset(data, 0, sizeof(data));
    data[0] = kCtId;
    data[1] = kIdNumber;
    queueFrame(true, data);
    queueRand(m_eightBits ? '8' : '7');
}

bool NmtDmsLink::queueData(const uint8_t* data, size_t length)
{
    if (length == 0) {
        return true;
    }
    if (!data || length > kMaxQueued || m_pending.size() + m_txList.size() * 8 > kMaxQueued - length) {
        return false;
    }

    m_dataQueued = true;
    m_pending.insert(m_pending.end(), data, data + length);
    if (m_established) {
        flushPending();
    }

    return true;
}

// The bytes become frames, in the format that the random numbers settled.
void NmtDmsLink::flushPending()
{
    uint8_t words[kDataWords];
    size_t pos = 0;
    const size_t perFrame = m_eightBits ? 7 : 8;

    while (pos < m_pending.size())
    {
        size_t count = std::min(perFrame, m_pending.size() - pos);

        // a zero at the end of a frame would be taken for filling: it starts the next frame
        // (a 7-bit character is zero if its low 7 bits are)
        while (count > 1 && (m_pending[pos + count - 1] & (m_eightBits ? 0xff : 0x7f)) == 0) {
            count--;
        }
        std::memset(words, 0, sizeof(words));
        if (!m_eightBits)
        {
            for (size_t i = 0; i < count; i++) {
                m_maskedBytes += (m_pending[pos + i] & 0x80) ? 1 : 0;
            }
        }
        if (m_eightBits)
        {
            for (size_t i = 0; i < count; i++)
            {
                words[0] = static_cast<uint8_t>(words[0] | (((m_pending[pos + i] >> 7) & 1) << (6 - i)));
                words[1 + i] = m_pending[pos + i] & 0x7f;
            }
        }
        else
        {
            for (size_t i = 0; i < count; i++) {
                words[i] = m_pending[pos + i] & 0x7f;
            }
        }
        queueFrame(false, words);
        pos += count;
    }
    m_pending.clear();
}

void NmtDmsLink::setReady(bool ready)
{
    // a receiver that was full says so when it can take data again
    if (ready && !m_ready && m_nrSent && m_dKnown)
    {
        m_rrPending = true;
        m_rrNr = false;
    }
    m_ready = ready;
}

// The data may go when the other side's random number came or our own was acknowledged.
void NmtDmsLink::checkEstablished()
{
    if (m_established || !(m_randAcked || m_randReceived)) {
        return;
    }

    m_established = true;
    flushPending();
    if (m_onEstablished) {
        m_onEstablished();
    }
}

// An RR(n) acknowledges the frames up to the number before n, if that is one of the first 4 frames
// of the list. True if it acknowledged something.
bool NmtDmsLink::ack(int n)
{
    const size_t limit = std::min(m_txList.size(), static_cast<size_t>(kWindow));
    size_t distance = 0;

    for (size_t i = 0; i < limit; i++)
    {
        if (m_txList[i].n == ((n - 1) & 7))
        {
            distance = i + 1;
            break;
        }
    }
    if (distance == 0) {
        return false;
    }
    const uint32_t epoch = m_epoch;

    for (size_t i = 0; i < distance; i++)
    {
        if (m_txList.front().control && m_txList.front().data[0] == kCtRand) {
            m_randAcked = true;
        }
        m_txList.pop_front();
    }
    // the frame that goes next stays, if it is still in the list; the repetitions count again
    m_txIndex = (m_txIndex >= distance) ? m_txIndex - distance : 0;
    m_passes = 0;

    checkEstablished();
    if (epoch != m_epoch) {
        return true;                    // a callback reset the link
    }
    if (m_txList.empty() && m_pending.empty() && m_dataQueued && m_established)
    {
        m_dataQueued = false;
        if (m_onAllSent) {
            m_onAllSent();
        }
    }

    return true;
}

// An RR/NR(n) that acknowledges nothing (Annex 21 Figure 1, note 2): n = NTX to NTX+4 refers to
// the frames under transmission and the next one, n = NTX-1 to NTX-3 to previous frames. Those
// are gone from the list, so the peer lost count: transmission is restarted with NTX = n, the
// frames that are not acknowledged are numbered again from n.
void NmtDmsLink::restartFrom(int n)
{
    const int first = m_txList.empty() ? m_nTx : m_txList.front().n;

    if (((n - first) & 7) <= 4) {
        return;
    }
    int next = n & 7;

    for (TxFrame& frame : m_txList)
    {
        frame.n = next;
        next = (next + 1) & 7;
    }
    m_nTx = next;
    m_txIndex = 0;
    m_passes = 0;
}

void NmtDmsLink::advance(double seconds)
{
    if (!(seconds > 0.0)) {
        return;
    }
    m_speechClosed = std::max(0.0, m_speechClosed - seconds);
    if (m_gapTime > 0.0) {
        m_gapTime = std::max(0.0, m_gapTime - seconds);
    }
    if (m_peerBusy)
    {
        m_peerBusyTime += seconds;
        if (m_peerBusyTime >= kBusyLimit) {
            m_peerBusy = false;
        }
    }
}

// Makes the next frame: an acknowledgement first, then the frames that are not acknowledged yet.
// Frames that are not acknowledged go out again, back to back. When they have to be repeated
// a fourth time, every frame of the repetition is followed by 81 ms without FFSK, and so on
// until something is acknowledged (Annex 21 para 6.5); the frame after a gap has the bit sync,
// as every frame here.
bool NmtDmsLink::scheduleFrame()
{
    m_txBits.clear();
    m_txPos = 0;

    if (!m_dKnown || m_gapTime > 0.0) {
        return false;
    }

    if (m_rrPending)
    {
        m_rrPending = false;
        m_txBits = encodeRr(m_dTx, m_rrNr, false, m_nRx);
        return true;
    }

    // a peer that said NR gets no frame but an RR until it sends an RR or a DT, or T is over
    if (m_peerBusy || m_txList.empty()) {
        return false;
    }

    // before the random numbers are exchanged only control frames go out
    size_t sendable = 0;

    while (sendable < m_txList.size() && sendable < static_cast<size_t>(kWindow)
        && (m_established || m_txList[sendable].control)) {
        sendable++;
    }
    if (sendable == 0) {
        return false;
    }
    if (m_txIndex >= sendable) {
        m_txIndex = 0;
    }

    const TxFrame& frame = m_txList[m_txIndex];

    m_txBits = encodeDt(m_dTx, frame.control, frame.n, frame.data, true);
    m_gapPending = (m_passes >= kGapPasses);
    m_txIndex++;
    if (m_txIndex >= sendable)
    {
        // all frames that may go went out once more
        m_txIndex = 0;
        if (m_passes < kGapPasses) {
            m_passes++;
        }
    }

    return true;
}

bool NmtDmsLink::pullBit(int& bit)
{
    if (m_txPos >= m_txBits.size())
    {
        if (!scheduleFrame())
        {
            m_txBits.clear();
            m_txPos = 0;
            return false;
        }
        // a frame starts going out: the speech is closed for 200 ms in both directions
        m_speechClosed = kSpeechClosed;
    }

    bit = m_txBits[m_txPos++];
    // the gap begins when the last bit of the frame is out
    if (m_txPos >= m_txBits.size() && m_gapPending)
    {
        m_gapPending = false;
        m_gapTime = kGapTime;
    }
    return true;
}

bool NmtDmsLink::directionOk(bool d)
{
    // our own frames, heard back, carry our direction bit
    return !m_dKnown || d != m_dTx;
}

void NmtDmsLink::abortFrame()
{
    m_inFrame = false;
    m_rxBits.clear();
    m_framesRejected++;
}

void NmtDmsLink::pushBit(int bit, double quality, double level)
{
    (void) level;

    if (!(quality >= 0.0)) {
        quality = 0.0;                      // also a NaN
    } else if (quality > 1.0) {
        quality = 1.0;
    }
    m_shift = static_cast<uint16_t>(((m_shift << 1) | (bit & 1)) & 0x7ff);
    m_qualitySum[m_qualityPos] = quality;
    m_qualityPos = (m_qualityPos + 1) & 15;
    m_qualityTotal = 0.0;
    for (double q : m_qualitySum) {
        m_qualityTotal += q;
    }
    if (m_bitsSinceRr < 1000) {
        m_bitsSinceRr++;
    }

    // The frame sync cannot show up inside a frame (the 1 1 after every 7 bits see to that): a
    // sync that does is the start of a new frame, the one before was cut off.
    if (m_shift == kSyncValue && m_qualityTotal / 16.0 >= m_minQuality)
    {
        if (m_inFrame) {
            m_framesRejected++;
        }
        // a frame of the other side: the speech is closed for 200 ms from its start, which was
        // 11 bits ago for a frame without bit sync
        m_speechClosed = std::max(m_speechClosed, kSpeechClosed - 11.0 / 1200.0);
        m_inFrame = true;
        m_rxBits.clear();
        m_rxNeed = 9;
        return;
    }
    if (!m_inFrame) {
        return;
    }

    m_rxBits.push_back(static_cast<uint8_t>(bit & 1));
    if (static_cast<int>(m_rxBits.size()) < m_rxNeed) {
        return;
    }

    if (m_rxNeed == 9)
    {
        // the label tells the kind of frame
        unsigned label = 0;

        for (int i = 0; i < 7; i++) {
            label = (label << 1) | m_rxBits[static_cast<size_t>(i)];
        }
        const unsigned pp = (label >> 3) & 3;

        // a frame with the wrong direction is dropped at once, so the next one can be found
        if (pp == 0 || !directionOk((label >> 6) & 1)) {
            abortFrame();
            return;
        }
        m_rxNeed = (pp == 3) ? kDtBody : kRrBody;
        if (static_cast<int>(m_rxBits.size()) < m_rxNeed) {
            return;
        }
    }

    rxFrameDone();
    m_inFrame = false;
    m_rxBits.clear();
}

void NmtDmsLink::rxFrameDone()
{
    auto word = [this](int index) {
        unsigned value = 0;

        for (int i = 0; i < 7; i++) {
            value = (value << 1) | m_rxBits[static_cast<size_t>(index * 9 + i)];
        }
        return value;
    };
    // the 1 1 after every 7 bits are not looked at
    const unsigned label = word(0);
    const bool d = ((label >> 6) & 1) != 0;
    const bool control = ((label >> 5) & 1) == 0;
    const unsigned pp = (label >> 3) & 3;
    const int n = static_cast<int>(label & 7);

    if (pp == 3)
    {
        uint8_t checkBits[63];
        uint8_t words[kDataWords];

        for (int i = 0; i < 9; i++)
        {
            const unsigned w = word(i);

            for (int j = 0; j < 7; j++) {
                checkBits[i * 7 + j] = static_cast<uint8_t>((w >> (6 - j)) & 1);
            }
            if (i > 0) {
                words[i - 1] = static_cast<uint8_t>(w);
            }
        }
        const unsigned received = (word(9) << 9) | (word(10) << 2) | (static_cast<unsigned>(m_rxBits[99]) << 1) | m_rxBits[100];

        if (crc16(checkBits, 63) != received)
        {
            m_framesRejected++;
            return;
        }
        m_framesReceived++;
        rxDt(d, control, n, words);
        return;
    }

    // RR / NR: the two labels agree, the parity bits are right
    const unsigned second = word(1);

    if (second != label || m_rxBits[18] != parity7(label) || m_rxBits[19] != parity7(label))
    {
        m_framesRejected++;
        return;
    }
    // the frame is sent twice (the second half follows right after the first): it counts once
    if (m_lastRrLabel == static_cast<int>(label) && m_bitsSinceRr <= 20 + 11 + 2) {
        return;
    }
    m_lastRrLabel = static_cast<int>(label);
    m_bitsSinceRr = 0;
    m_framesReceived++;
    rxRr(d, pp == 2, control, n);
}

void NmtDmsLink::rxDt(bool d, bool control, int n, const uint8_t* words)
{
    // the first frame of the other side tells the direction bit of this side
    if (!m_dKnown)
    {
        m_dKnown = true;
        m_dTx = !d;
    }

    // a DT of the other side also ends its NR (Annex 21 para 5.3)
    m_peerBusy = false;

    // every frame is answered with the number of the next one wanted: an acknowledgement, or a
    // request for the one that got lost; NR if this side cannot take it
    m_rrPending = true;
    m_rrNr = false;
    m_nrSent = false;

    if (n != m_nRx) {
        return;
    }
    if (!m_ready)
    {
        m_rrNr = true;
        m_nrSent = true;
        return;
    }
    m_nRx = (m_nRx + 1) & 7;

    // a callback may reset the link: nothing of the old session is used after it
    const uint32_t epoch = m_epoch;

    if (control)
    {
        const uint8_t type = words[0];

        if (type == kCtId)
        {
            m_peerIdentity.assign(words + 1, words + kDataWords);
        }
        else if (type == kCtRand)
        {
            m_randReceived = true;
            // our random number is answered once, always asking for 8 bits
            if (!m_randSent) {
                queueRand('8');
            }
            m_eightBits = (words[7] == '8');
            checkEstablished();
        }
        if (epoch == m_epoch && m_onControl) {
            m_onControl(type, words + 1);
        }
        return;
    }

    deliver(words);
}

void NmtDmsLink::deliver(const uint8_t* words)
{
    std::vector<uint8_t> data;

    if (m_eightBits)
    {
        // the first word holds the 8th bit of the 7 bytes that follow
        for (int i = 0; i < 7; i++) {
            data.push_back(static_cast<uint8_t>((((words[0] >> (6 - i)) & 1) << 7) | (words[1 + i] & 0x7f)));
        }
    }
    else
    {
        for (int i = 0; i < kDataWords; i++) {
            data.push_back(words[i] & 0x7f);
        }
    }
    // the end of a frame is filled with zeros; a frame of nothing else is one zero
    while (data.size() > 1 && data.back() == 0) {
        data.pop_back();
    }
    if (m_onData) {
        m_onData(data);
    }
}

void NmtDmsLink::rxRr(bool d, bool nr, bool positive, int n)
{
    (void) d;

    const bool randWasAcked = m_randAcked;
    const uint32_t epoch = m_epoch;
    const bool acknowledged = ack(n);

    if (epoch != m_epoch) {
        return;                         // a callback reset the link
    }
    if (!acknowledged) {
        restartFrom(n);
    }
    if (nr)
    {
        // NR acknowledges like an RR; the other side is full: no new frames until an RR or a
        // DT, at most T; every NR starts T again
        m_peerBusy = true;
        m_peerBusyTime = 0.0;
        return;
    }
    // any RR ends an NR (Figure 1: RR(N) -> HOLD = 0), also one that acknowledges nothing new
    m_peerBusy = false;
    if (!acknowledged) {
        return;                         // an old RR
    }
    // an RR with s = 0 that acknowledges our random number: the peer wants scrambled data
    if (!randWasAcked && m_randAcked && positive) {
        m_positiveResponse = true;
    }
}

} // namespace modemnmt
