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

#ifndef MODEMRADIOPAGING_RADIOPAGINGFLEXRECEIVER_H
#define MODEMRADIOPAGING_RADIOPAGINGFLEXRECEIVER_H

#include <cstdint>
#include <deque>
#include <vector>

#include "export.h"
#include "radiopagingflex.h"
#include "radiopagingflexdecoder.h"
#include "radiopagingfskdemod.h"

// FLEX receiver: FM discriminator samples to frames and messages.
//
// A demodulator at 1600 baud with 2 levels runs all the time and hunts for sync 1 on its
// own, in both polarities: the mode code A, the 16 bit pattern B and the inverted mode
// code are compared with the code of each mode. Sync 1 is accepted when the 80 bits have
// at most m_maxSyncErrors bit errors, or when the two copies of the mode code (A and
// inverted A) each decode to the same mode (up to 2 bit errors in one copy and 3 in the
// other), whatever B holds. Only the received bits of the frame are used: nothing is
// predicted from earlier frames. The frame information word follows at the same rate. A second demodulator then
// continues the symbol clock at the rate and number of levels of the frame mode,
// starting with the DC offset and deviation measured during sync 1. The second sync is
// skipped by its nominal length (corrected by one symbol when its closing pattern is
// received without errors one symbol early or late), then the data part is sliced into
// the codewords of each phase. Blocks received with a carrier much weaker than during
// sync 1 are marked erased. Each frame is decoded by RadioPagingFlexDecoder.
//
// Input samples are discriminator output scaled so that the outer deviation (4800 Hz)
// is about +/-1. Magnitude squared samples are optional (used for erasures).
class MODEMRADIOPAGING_API RadioPagingFlexReceiver
{
public:
    enum State {
        Hunting,        //!< Looking for sync 1
        FrameInfo,      //!< Receiving the frame information word
        Sync2,          //!< Receiving the second sync
        Data            //!< Receiving the data part
    };

    struct Status
    {
        State m_state;
        RadioPagingFlex::Mode m_mode;   //!< Mode of the current or last frame
        bool m_inverted;
        unsigned m_cycle;               //!< Of the last frame
        unsigned m_frame;
        uint32_t m_syncs;               //!< Sync 1 detections
        uint32_t m_frames;              //!< Frames decoded (frame information word valid)
        uint32_t m_frameInfoFailures;   //!< Sync 1 found but the frame information word failed
        float m_quality;                //!< Of the last frame, 0-1
        RadioPagingBerWindow m_ber;     //!< Of the recent frames of the polarity of the last frame
        // Network of the polarity of the last frame
        RadioPagingFlexDecoder::FrameReport m_frameReport;
        RadioPagingFlexDecoder::NetworkTime m_networkTime;
        std::vector<RadioPagingFlexDecoder::GroupSlot> m_groups;
        int m_reassemblies;
        float m_deviation;              //!< Measured outer deviation, discriminator units
    };

    //!< Number of bit errors allowed over the 80 bits of A, B and inverted A
    static const int m_maxSyncErrors = 8;
    //!< Blocks with a mean power this many dB below sync 1 are erased
    static constexpr float m_erasureDb = 15.0f;

    RadioPagingFlexReceiver();

    void create(double sampleRate);
    void reset();
    //!< Enable or disable the network of each polarity. There is no mode enable: the system
    //!< chooses the mode of each frame
    void setPolarities(bool normal, bool inverted) { m_enabled[0] = normal; m_enabled[1] = inverted; }
    //!< Also output each fragment of a fragmented message, before the reassembled message. Default off
    void setReportFragments(bool enable)
    {
        m_decoder[0].setReportFragments(enable);
        m_decoder[1].setReportFragments(enable);
    }

    //!< Process one discriminator sample with its magnitude squared (pass 1 if not known)
    void process(float sample, float magsq = 1.0f);

    bool hasMessage() const { return !m_messages.empty(); }
    RadioPagingFlexDecoder::Message takeMessage();
    //!< Partial messages still being reassembled, marked incomplete
    void flush();

    Status getStatus() const;
    RadioPagingFlexDecoder& decoder(bool inverted) { return m_decoder[inverted ? 1 : 0]; }

private:
    double m_sampleRate;
    bool m_enabled[2];                  //!< Normal (0) and inverted (1)

    RadioPagingFskDemod m_syncDemod;    //!< 1600 baud, 2 levels, always running
    RadioPagingFskDemod m_dataDemod;    //!< Rate and levels of the frame mode
    //!< One per polarity: the same capcode on the other polarity is another network, with its
    //!< own frame timing, fragments, groups and retransmissions
    RadioPagingFlexDecoder m_decoder[2];

    State m_state;
    uint64_t m_regLo;                   //!< Last 64 received sync rate bits, newest in bit 0
    uint64_t m_regHi;                   //!< The 64 bits before
    double m_syncPower;                 //!< Mean magnitude squared over sync 1
    std::deque<float> m_recentPower;    //!< Magnitude squared of the last sync 1 duration

    RadioPagingFlex::Mode m_mode;
    bool m_inverted;
    uint32_t m_fiw;
    int m_fiwBits;

    int m_s2Symbols;                    //!< Nominal second sync length in symbols
    int m_s2Count;
    uint32_t m_s2Reg;                   //!< Bits of the second sync, newest in bit 0
    std::vector<uint8_t> m_s2Tail;      //!< Symbols around the nominal end of the second sync
    int m_s2TailStart;
    int m_s2ExactEnd;                   //!< Symbol count at which inverted C ended without errors, -1 if not seen

    int m_dataSymbols;                  //!< Data symbols of the mode
    int m_dataCount;
    int m_bitIndex;                     //!< Bit index in the phase words
    bool m_secondOfPair;                //!< 6400/4 and 3200/2: second symbol of a pair
    std::vector<std::vector<uint32_t>> m_words;
    std::vector<double> m_blockPower;
    std::vector<int> m_blockSamples;
    double m_errorSum;
    int m_errorCount;

    uint32_t m_syncs;
    uint32_t m_frames;
    uint32_t m_frameInfoFailures;
    unsigned m_lastCycle;
    unsigned m_lastFrame;
    float m_lastQuality;

    std::deque<RadioPagingFlexDecoder::Message> m_messages;

    void syncSymbol(const RadioPagingFskDemod::Symbol& symbol, float magsq);
    bool checkSync(RadioPagingFlex::Mode& mode, bool& inverted) const;
    void startData(float offset);
    void dataSymbol(int symbol, float error, float magsq);
    void sync2Symbol(int symbol);
    void storeDataSymbol(int symbol, float magsq);
    void endFrame();
    void backToHunting();
    int mirror(int symbol, int levels) const { return m_inverted ? (levels - 1 - symbol) : symbol; }
};

#endif // MODEMRADIOPAGING_RADIOPAGINGFLEXRECEIVER_H
