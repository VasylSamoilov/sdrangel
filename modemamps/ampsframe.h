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

#ifndef MODEMAMPS_AMPSFRAME_H_
#define MODEMAMPS_AMPSFRAME_H_

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "ampsfsk.h"
#include "export.h"

// The receiver of the reverse channels of a base station, as osmocom-analog's amps (dsp.c
// fsk_rx_bit, frame.c amps_decode_frame): after the word sync (either way up: a negative one turns
// the bits over) it takes the five repeats of a word, 240 bits. On the reverse control channel the
// first word comes after the coded DCC (247 bits); the words after it follow without a sync, as long
// as the handler wants more. On the reverse voice channel each repeat has its own dotting and sync:
// each one is taken after its sync (osmocom reads 240 bits after the first sync, so its second and
// fourth repeat are dotting and sync); a burst ends after 5 repeats or when the next sync does not
// come. The word is the vote of the repeats.
namespace modemamps
{

struct MODEMAMPS_API ReverseBurst
{
    bool first = true;                  // after a sync, else a word that followed
    int dcc = -1;                       // the decoded DCC of the first RECC word, -1 none or bad
    int repeats = 0;                    // repeats received: RECC 5, RVC up to 5
    std::array<uint64_t, 5> words{};    // the 48 bit repeats, the first bit sent the most significant
    std::array<bool, 5> ok{};           // their parity is right
    int okCount = 0;
    bool negative = false;              // the sync came upside down
    double level = 0.0;                 // the mean swing of the bits relative to twice the deviation
    double quality = 0.0;               // osmocom: the sum of the bit qualities over the sum of levels
    uint64_t voted = 0;                 // each bit as most of the repeats have it
    bool votedOk = false;               // its parity is right

    // The word: the vote of the repeats if its parity is right, else 0 (none). osmocom takes the first
    // repeat with a right parity; the vote decodes far more and takes less noise for a word
    // (measured: at 5 % bit errors 95 % of the words against 36 %, no wrong word against 28 in
    // 200000; random bits taken 2.4e-4 against 1.2e-3).
    uint64_t word() const { return votedOk ? voted : 0; }
    bool valid() const { return votedOk; }
};

class MODEMAMPS_API ReverseReceiver
{
public:
    enum Channel { ChannelRecc, ChannelRvc };

    // Returns true if another word follows without a sync (240 bits).
    using BurstHandler = std::function<bool(const ReverseBurst&)>;

    // tolerant: a sync with one bit wrong is taken (osmocom's --tolerant)
    ReverseReceiver(double sampleRate, bool tolerant);

    void setChannel(Channel channel);
    void setBurstHandler(BurstHandler handler) { m_handler = std::move(handler); }
    // the deviation in Hz, after the DC filter
    void process(const float* samples, int length) { m_demodulator.process(samples, length); }
    // forgets a burst being received
    void reset();

    // the DCC of a coded DCC with up to one bit wrong, -1 if none
    static int decodeDcc(uint8_t coded);

private:
    void receiveBit(int bit, double quality, double level);
    void decode();

    ManchesterDemodulator m_demodulator;
    BurstHandler m_handler;
    bool m_tolerant;
    Channel m_channel;
    uint32_t m_sync;
    bool m_inFrame;
    bool m_negative;
    bool m_first;
    int m_length;                       // bits of the burst being received
    std::array<uint8_t, 247> m_bits;
    int m_count;
    int m_repeats;                      // RVC: repeats received
    bool m_between;                     // RVC: waiting for the sync of the next repeat
    int m_gap;                          // bits of that wait
    double m_level;
    double m_quality;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSFRAME_H_
