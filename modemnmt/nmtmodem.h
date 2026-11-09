///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016/2017 Andreas Eversberg <jolly@eversberg.eu>                //
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

#ifndef MODEMNMT_NMTMODEM_H_
#define MODEMNMT_NMTMODEM_H_

#include <array>
#include <cstdint>
#include <functional>

#include "ffsk.h"
#include "hagelbarger.h"
#include "nmtframe.h"
#include "export.h"

namespace modemnmt
{

// NMT signalling on the voice channel: 1200 bit/s FFSK, bit 1 = 1200 Hz, bit 0 = 1800 Hz.
constexpr double kBitRate = 1200.0;
constexpr double kFreqBit0 = 1800.0;
constexpr double kFreqBit1 = 1200.0;
// Deviation of the signalling tone relative to speech deviation (3 kHz at 1 kHz). The signalling
// deviation is 4.2 kHz at 1800 Hz, which with pre-emphasis equals 2.33 kHz at 1 kHz.
constexpr double kSpeechDeviation = 3000.0;
constexpr double kTxPeakFsk = 4200.0 / 1800.0 * 1000.0 / kSpeechDeviation;
// How far the receive bit clock follows a tone change.
constexpr double kBitAdjust = 0.1;
// Frames with a lower average bit quality at the sync word are dropped.
constexpr double kMinSyncQuality = 0.65;

// One frame as seen by the receiver.
struct RxFrame
{
    EncodedBits bits;            // the 140 bits after the frame sync
    double quality;              // 0..1 average over the frame
    double level;                // 1.0 = signalling level of a base station
    uint64_t framesElapsed;      // frame periods since the previous frame, 0 for the first one
    // The first six information bits (Y2, Y4 .. Y12) are 0, checked before error correction
    // (NMT Doc 450-1 para 4.6.8). It does not decide whether the frame is decoded: the standard
    // decodes it anyway. It tells the exchange to split the line and a mobile station to mute
    // its audio until 160 ms after the last frame sync.
    bool leadingZeros;
};

// Finds the frame sync in a bit stream and collects the 140 bits after it.
class MODEMNMT_API FrameReceiver
{
public:
    using FrameSink = std::function<void(const RxFrame&)>;

    FrameReceiver();
    void setFrameSink(FrameSink sink) { m_sink = std::move(sink); }
    // quality and level as given by FfskDemodulator
    void receiveBit(int bit, double quality, double level);

private:
    FrameSink m_sink;
    uint32_t m_sync;
    bool m_inSync;
    int m_count;
    std::array<double, 256> m_levels;
    std::array<double, 256> m_qualities;
    std::array<uint8_t, kEncodedBits> m_bits;
    uint64_t m_bitsCount;        // every bit passed in, including the ones that were not sampled
    int64_t m_frameStart;        // position of the start of the last frame, -1 if none
    int64_t m_lastFrameStart;
};

// FFSK modem for one NMT voice channel. Audio is mono float at any sample rate from about 8 kHz.
class MODEMNMT_API NmtModem
{
public:
    // Fills the next frame to send. Returns false if there is nothing to send, the modem then stops
    // after the current frame.
    using FrameSource = std::function<bool(FrameBits&)>;

    // deviationFactor scales the signalling level to the deviation of the transmitter.
    NmtModem(double sampleRate, double deviationFactor);

    void setFrameSource(FrameSource source) { m_source = std::move(source); }
    void setFrameSink(FrameReceiver::FrameSink sink) { m_receiver.setFrameSink(std::move(sink)); }

    // Renders the frame stream into the buffer, silence in the gaps. Returns the number of
    // samples that carried signalling.
    int render(float* samples, int length, bool add);
    void receive(const float* samples, int length);
    void resetTransmitter();

private:
    int nextBit();

    FfskModulator m_modulator;
    FfskDemodulator m_demodulator;
    FrameReceiver m_receiver;
    FrameSource m_source;
    FrameBits m_frame;
    int m_framePos;              // kFrameBits if no frame is loaded
};

} // namespace modemnmt

#endif // MODEMNMT_NMTMODEM_H_
