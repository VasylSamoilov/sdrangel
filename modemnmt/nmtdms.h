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

#ifndef MODEMNMT_NMTDMS_H_
#define MODEMNMT_NMTDMS_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <random>
#include <vector>

#include "export.h"

namespace modemnmt
{

// The data link of the NMT data and message service (DMS, NMT Doc 450-3 Annex 21): user data in the
// speech path of a traffic channel, 1200 bit/s FFSK, in frames of its own. No timers and no audio:
// the caller pulls the bits to send and pushes the bits it receives. Not thread safe.
//
// A DT frame is 127 bits: bit sync (15), frame sync 00101000111 (11), a label d s p p n n n and 8 words
// of 7 bits (user data, or a control type and 7 words), a CRC-16 of 16 bits. After every 7 bits two
// 1 bits are inserted. RR and NR are 77 bits: the sync and the label twice, with parity bits.
// Frames are numbered modulo 8, up to 4 DT frames are sent before an acknowledgement is needed.
class MODEMNMT_API NmtDmsLink
{
public:
    static const int kDataWords = 8;
    static const int kWindow = 4;
    static const int kDtBits = 127;
    static const int kRrBits = 77;
    static const size_t kMaxQueued = 65536;     // bytes that may wait

    // The 16 bit CRC of the 63 bits label and data (the first bits[len]) as the standard defines it.
    static uint16_t crc16(const uint8_t* bits, int len);
    // The bits of the frames on air, for the tests and for logging.
    static std::vector<uint8_t> encodeDt(bool d, bool control, int n, const uint8_t data[kDataWords], bool bitSync);
    static std::vector<uint8_t> encodeRr(bool d, bool nr, bool control, int n);

    using DataHandler = std::function<void(const std::vector<uint8_t>& data)>;
    using ControlHandler = std::function<void(uint8_t type, const uint8_t data[kDataWords - 1])>;
    using EventHandler = std::function<void()>;

    // eightBits: user data are bytes (7 per frame), else 7-bit characters (8 per frame).
    explicit NmtDmsLink(bool eightBits = true);

    void reset();
    void setDataHandler(DataHandler handler) { m_onData = std::move(handler); }
    void setControlHandler(ControlHandler handler) { m_onControl = std::move(handler); }
    // All queued data are acknowledged.
    void setAllSentHandler(EventHandler handler) { m_onAllSent = std::move(handler); }
    // The random numbers have been exchanged: data can be sent.
    void setEstablishedHandler(EventHandler handler) { m_onEstablished = std::move(handler); }
    // The random number we send (3 bytes); without this it is random.
    void setRandom(uint8_t r1, uint8_t r2, uint8_t r3) { m_random[0] = r1; m_random[1] = r2; m_random[2] = r3; m_randomSet = true; }
    // The identity we send, 7 characters (country digit and six digits).
    void setIdentity(const char* id7);
    // The receiver cannot take more data (NR is sent instead of RR). The sender waits at most 3 s.
    void setReady(bool ready);

    // This side calls: it sends the first frames (identity, random number) and uses d = 0.
    void startAsCaller();
    // User data to send; it is cut into frames when the random numbers have been exchanged, in the
    // format they ask for. False if there is too much waiting. The receiver of the other side
    // takes a frame of nothing but zeros for one zero; trailing zeros inside a frame are filling,
    // so a message that ends in zero bytes reaches the peer without them if its own end is
    // not told by a length: an upper layer has to cut and fill to its own lengths.
    bool queueData(const uint8_t* data, size_t length);

    // The next bit to send. False if there is nothing to send now.
    bool pullBit(int& bit);
    // A bit from the demodulator with its quality and level (0..1). Quality of the last 16 bits has
    // to reach `minQuality` to take a frame sync.
    void pushBit(int bit, double quality, double level);
    void setMinQuality(double quality) { m_minQuality = (quality >= 0.0) ? quality : 0.0; }
    // Seconds that went by: the pause before frames are sent again (81 ms) and the time the
    // receiver of the other side may be full (3 s) run on this clock. It has to be called.
    void advance(double seconds);

    bool established() const { return m_established; }
    bool callerSide() const { return m_caller; }
    bool eightBits() const { return m_eightBits; }
    bool idle() const { return m_txList.empty() && m_pending.empty() && !m_rrPending && m_txPos >= m_txBits.size(); }
    bool peerBusy() const { return m_peerBusy; }
    // The peer answered our random number with a positive response (s = 0): it wants scrambled data,
    // which this link does not do.
    bool positiveResponse() const { return m_positiveResponse; }
    // frames that wait for an acknowledgement; bytes that wait for the exchange of the random numbers
    size_t unacknowledged() const { return m_txList.size(); }
    size_t pendingBytes() const { return m_pending.size(); }
    // bytes with the eighth bit set that went out in the 7-bit format the peer asked for
    size_t maskedBytes() const { return m_maskedBytes; }
    // Counters for the tests and the log
    uint64_t framesReceived() const { return m_framesReceived; }
    uint64_t framesRejected() const { return m_framesRejected; }
    // the number of the next DT expected from the peer, and of the next one we number
    int nextRx() const { return m_nRx; }
    int nextTx() const { return m_nTx; }
    // 7 characters of the identity the peer sent, empty if none
    const std::vector<uint8_t>& peerIdentity() const { return m_peerIdentity; }

private:
    struct TxFrame
    {
        bool control;
        int n;
        uint8_t data[kDataWords];
    };

    void queueFrame(bool control, const uint8_t data[kDataWords]);
    void queueRand();
    bool ack(int n);
    void checkEstablished();
    void flushPending();
    bool scheduleFrame();
    void rxFrameDone();
    void rxDt(bool d, bool control, int n, const uint8_t* words);
    void rxRr(bool d, bool nr, bool positive, int n);
    void deliver(const uint8_t* words);
    void abortFrame();
    bool directionOk(bool d);

    bool m_eightBits;
    bool m_eightBitsDefault;
    bool m_caller;
    bool m_dKnown;                  // the direction bit of this side is known
    bool m_dTx;
    bool m_established;
    bool m_randAcked;               // our random number was acknowledged
    bool m_randReceived;
    bool m_randSent;
    bool m_randomSet;
    bool m_peerData;                // a user data frame came from the peer
    bool m_positiveResponse;
    bool m_ready;
    bool m_nrSent;                  // the last answer was an NR
    bool m_gapPending;              // the pause starts when the frame that is going out is done
    size_t m_maskedBytes;
    uint32_t m_epoch;               // counts reset(): a callback that resets the link ends the frame handling
    std::vector<uint8_t> m_pending; // user bytes that wait for the exchange of the random numbers
    bool m_peerBusy;
    double m_peerBusyTime;
    uint8_t m_random[3];
    char m_identity[8];
    std::vector<uint8_t> m_peerIdentity;

    // transmission
    std::deque<TxFrame> m_txList;   // not acknowledged, in order, the first at m_txAck
    int m_nTx;                      // number of the next frame we make
    size_t m_txIndex;               // which of the first frames goes next
    double m_gapTime;               // seconds of pause before the next frame
    int m_passes;                   // times all frames went out since the last acknowledgement
    size_t m_sentCount;             // frames of the list that went out at least once
    bool m_rrPending;
    bool m_rrNr;
    bool m_burst;                   // frames follow each other: no bit sync
    std::vector<uint8_t> m_txBits;
    size_t m_txPos;
    bool m_dataQueued;              // user data waits for the acknowledgement

    // reception
    int m_nRx;
    uint16_t m_shift;
    double m_qualitySum[16];
    int m_qualityPos;
    double m_qualityTotal;
    bool m_inFrame;
    std::vector<uint8_t> m_rxBits;
    int m_rxNeed;
    int m_lastRrLabel;              // the label of the last RR / NR taken, -1 for none
    int m_bitsSinceRr;
    uint64_t m_framesReceived;
    uint64_t m_framesRejected;
    double m_minQuality;

    DataHandler m_onData;
    ControlHandler m_onControl;
    EventHandler m_onAllSent;
    EventHandler m_onEstablished;
};

} // namespace modemnmt

#endif // MODEMNMT_NMTDMS_H_
