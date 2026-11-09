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

#include "nmtmodem.h"

#include <algorithm>

namespace modemnmt
{

namespace
{

// last 5 bits of the bit sync and the 11 bits of the frame sync (NMT Doc 450-1, para 4.3.1)
const uint32_t kSyncPattern = 0xaf12;
const int kSyncWindowBits = 16;
// bits from the start of the bit sync to the first information bit
const int kPreambleBits = kBitSyncBits + kFrameSyncBits;

} // namespace

FrameReceiver::FrameReceiver() :
    m_sync(0),
    m_inSync(false),
    m_count(0),
    m_levels(),
    m_qualities(),
    m_bits(),
    m_bitsCount(0),
    m_frameStart(-1),
    m_lastFrameStart(-1)
{
}

void FrameReceiver::receiveBit(int bit, double quality, double level)
{
    // normalise to the signalling level of a base station
    level /= kTxPeakFsk;
    m_bitsCount++;

    if (!m_inSync)
    {
        m_sync = ((m_sync << 1) | static_cast<uint32_t>(bit & 1)) & 0xffff;
        m_levels[m_count & 0xff] = level;
        m_qualities[m_count & 0xff] = quality;
        m_count = (m_count + 1) & 0xff;

        if (m_sync != kSyncPattern) {
            return;
        }

        double averageQuality = 0.0;
        for (int i = 0; i < kSyncWindowBits; i++) {
            averageQuality += m_qualities[static_cast<size_t>((m_count - 1 - i + 256) & 0xff)];
        }
        averageQuality /= kSyncWindowBits;

        // do not accept garbage
        if (averageQuality < kMinSyncQuality) {
            return;
        }

        m_lastFrameStart = m_frameStart;
        m_frameStart = static_cast<int64_t>(m_bitsCount) - kPreambleBits;
        m_sync = 0;
        m_inSync = true;
        m_count = 0;
        return;
    }

    m_bits[static_cast<size_t>(m_count)] = static_cast<uint8_t>(bit & 1);
    m_levels[static_cast<size_t>(m_count)] = level;
    m_qualities[static_cast<size_t>(m_count)] = quality;
    if (++m_count != kEncodedBits) {
        return;
    }

    // end of frame
    m_inSync = false;
    m_count = 0;

    RxFrame frame;
    frame.bits = m_bits;
    frame.level = 0.0;
    frame.quality = 0.0;
    for (int i = 0; i < kEncodedBits; i++)
    {
        frame.level += m_levels[static_cast<size_t>(i)];
        frame.quality += m_qualities[static_cast<size_t>(i)];
    }
    frame.level /= kEncodedBits;
    frame.quality /= kEncodedBits;

    frame.leadingZeros = true;
    for (size_t i = 1; i < 12; i += 2) {
        frame.leadingZeros = frame.leadingZeros && m_bits[i] == 0;
    }

    // round to the nearest frame period
    frame.framesElapsed = 0;
    if (m_lastFrameStart >= 0) {
        frame.framesElapsed = static_cast<uint64_t>((m_frameStart - m_lastFrameStart + kFrameBits / 2) / kFrameBits);
    }

    if (m_sink) {
        m_sink(frame);
    }
}

NmtModem::NmtModem(double sampleRate, double deviationFactor) :
    m_modulator(sampleRate, kBitRate, kFreqBit0, kFreqBit1, kTxPeakFsk * deviationFactor),
    m_demodulator(sampleRate, kBitRate, kFreqBit0, kFreqBit1, kBitAdjust),
    m_receiver(),
    m_frame(),
    m_framePos(kFrameBits)
{
    m_modulator.setBitSource([this]() { return nextBit(); });
    m_demodulator.setBitSink([this](int bit, double quality, double level) {
        m_receiver.receiveBit(bit, quality, level);
    });
}

int NmtModem::nextBit()
{
    if (m_framePos >= kFrameBits)
    {
        if (!m_source || !m_source(m_frame)) {
            return -1;
        }
        m_framePos = 0;
    }

    return m_frame[static_cast<size_t>(m_framePos++)];
}

int NmtModem::render(float* samples, int length, bool add)
{
    const int count = m_modulator.render(samples, length, add);

    if (!add && count < length) {
        std::fill(samples + count, samples + length, 0.0f);
    }

    return count;
}

void NmtModem::receive(const float* samples, int length)
{
    m_demodulator.process(samples, length);
}

void NmtModem::resetTransmitter()
{
    m_modulator.reset();
    m_framePos = kFrameBits;
}

} // namespace modemnmt
