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

#include "radiopagingflexreceiver.h"

#include <algorithm>
#include <cmath>

#include "radiopagingbch.h"

const int RadioPagingFlexReceiver::m_maxSyncErrors;
constexpr float RadioPagingFlexReceiver::m_erasureDb;

namespace {

// Mode codes A (32 bits) and inverted A, in transmission order, for the four modes
const uint32_t codeA[4] = {0x78f35939u, 0x84e75939u, 0x4f975939u, 0x215f5939u};
const uint32_t codeAInv[4] = {0x870ca6c6u, 0x7b18a6c6u, 0xb068a6c6u, 0xdea0a6c6u};
const uint32_t codeB = 0x5555u;
// Closing pattern of the second sync
const uint32_t syncCInv = 0x127bu;
// Gray decode of 4-level symbols to dibits: 0 -> 00, 1 -> 01, 2 -> 11, 3 -> 10
const unsigned grayToDibit[4] = {0, 1, 3, 2};
// Sync 1 (A, B, inverted A) lasts 80 bits at 1600 baud
const int syncBits = 80;

int popcount32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0f0f0f0fu;
    return (int) ((x * 0x01010101u) >> 24);
}

} // namespace

RadioPagingFlexReceiver::RadioPagingFlexReceiver() :
    m_sampleRate(38400.0),
    m_normalEnabled(true),
    m_invertedEnabled(true),
    m_syncs(0),
    m_frames(0),
    m_frameInfoFailures(0),
    m_lastCycle(0),
    m_lastFrame(0),
    m_lastQuality(0.0f)
{
    for (bool& enabled : m_modeEnabled) {
        enabled = true;
    }
    create(m_sampleRate);
}

void RadioPagingFlexReceiver::create(double sampleRate)
{
    m_sampleRate = sampleRate;
    m_syncDemod.create(sampleRate, RadioPagingFlex::m_syncBaud, 2);
    reset();
}

void RadioPagingFlexReceiver::reset()
{
    m_syncDemod.reset();
    m_decoder.reset();
    m_messages.clear();
    m_recentPower.clear();
    m_syncs = 0;
    m_frames = 0;
    m_frameInfoFailures = 0;
    m_mode = RadioPagingFlex::Mode1600_2;
    m_inverted = false;
    m_lastQuality = 0.0f;
    backToHunting();
}

void RadioPagingFlexReceiver::backToHunting()
{
    m_state = Hunting;
    m_regLo = 0;
    m_regHi = 0;
    m_syncDemod.setLocked(false);
}

RadioPagingFlexDecoder::Message RadioPagingFlexReceiver::takeMessage()
{
    RadioPagingFlexDecoder::Message message = m_messages.front();
    m_messages.pop_front();
    return message;
}

void RadioPagingFlexReceiver::flush()
{
    std::vector<RadioPagingFlexDecoder::Message> messages = m_decoder.flush();
    for (auto& message : messages) {
        m_messages.push_back(message);
    }
}

RadioPagingFlexReceiver::Status RadioPagingFlexReceiver::getStatus() const
{
    Status status;

    status.m_state = m_state;
    status.m_mode = m_mode;
    status.m_inverted = m_inverted;
    status.m_cycle = m_lastCycle;
    status.m_frame = m_lastFrame;
    status.m_syncs = m_syncs;
    status.m_frames = m_frames;
    status.m_frameInfoFailures = m_frameInfoFailures;
    status.m_quality = m_lastQuality;
    status.m_deviation = m_syncDemod.getDeviation();

    return status;
}

void RadioPagingFlexReceiver::process(float sample, float magsq)
{
    RadioPagingFskDemod::Symbol symbol;

    // The sync demodulator keeps its symbol clock running through the data part, so the
    // next frame's sync is found without a new acquisition
    if (m_syncDemod.process(sample, symbol) && ((m_state == Hunting) || (m_state == FrameInfo))) {
        syncSymbol(symbol, magsq);
    }

    if ((m_state == Sync2) || (m_state == Data))
    {
        if (m_dataDemod.process(sample, symbol)) {
            dataSymbol(symbol.m_value, symbol.m_error, magsq);
        }
        if ((m_state == Data) && (m_dataCount < m_dataSymbols))
        {
            int block = std::min(m_bitIndex / (32 * RadioPagingFlex::m_wordsPerBlock), RadioPagingFlex::m_blocksPerFrame - 1);
            m_blockPower[block] += magsq;
            m_blockSamples[block]++;
        }
    }

    if (m_state == Hunting)
    {
        m_recentPower.push_back(magsq);
        size_t keep = (size_t) (syncBits * m_sampleRate / RadioPagingFlex::m_syncBaud);
        while (m_recentPower.size() > keep) {
            m_recentPower.pop_front();
        }
    }
}

bool RadioPagingFlexReceiver::checkSync(RadioPagingFlex::Mode& mode, bool& inverted) const
{
    // Bits 79-48: A, 47-32: B, 31-0: inverted A
    uint32_t a = (uint32_t) (((m_regHi & 0xffff) << 16) | (m_regLo >> 48));
    uint32_t b = (uint32_t) ((m_regLo >> 32) & 0xffff);
    uint32_t aInv = (uint32_t) m_regLo;
    int best = syncBits + 1;
    int bestMode = -1;
    bool bestInverted = false;

    for (int pol = 0; pol < 2; pol++)
    {
        if ((pol == 0) && !m_normalEnabled) {
            continue;
        }
        if ((pol == 1) && !m_invertedEnabled) {
            continue;
        }

        uint32_t flip = pol ? 0xffffffffu : 0;
        int bErrors = popcount32((b ^ (flip & 0xffff)) ^ codeB);

        for (int m = 0; m < 4; m++)
        {
            int aErrors = popcount32((a ^ flip) ^ codeA[m]);
            int aInvErrors = popcount32((aInv ^ flip) ^ codeAInv[m]);
            int errors = bErrors + aErrors + aInvErrors;
            // A and inverted A are two copies of the same codeword. Within 2 bit errors a
            // copy is the codeword the code corrects it to; one copy may then have one more
            // error when the other confirms the same mode. B is not needed in that case
            bool copiesAgree = ((aErrors <= 2) && (aInvErrors <= 3)) || ((aErrors <= 3) && (aInvErrors <= 2));
            bool accepted = (errors <= m_maxSyncErrors) || copiesAgree;

            if (accepted && (errors < best))
            {
                best = errors;
                bestMode = m;
                bestInverted = (pol == 1);
            }
        }
    }

    if ((bestMode < 0) || !m_modeEnabled[bestMode]) {
        return false;
    }

    mode = (RadioPagingFlex::Mode) bestMode;
    inverted = bestInverted;
    return true;
}

void RadioPagingFlexReceiver::syncSymbol(const RadioPagingFskDemod::Symbol& symbol, float magsq)
{
    (void) magsq;

    if (m_state == Hunting)
    {
        m_regHi = (m_regHi << 1) | (m_regLo >> 63);
        m_regLo = (m_regLo << 1) | (uint64_t) (symbol.m_value & 1);

        RadioPagingFlex::Mode mode;
        bool inverted;

        if (checkSync(mode, inverted))
        {
            m_mode = mode;
            m_inverted = inverted;
            m_fiw = 0;
            m_fiwBits = 0;
            m_syncs++;
            m_state = FrameInfo;
            m_syncDemod.setLocked(true);

            double sum = 0.0;
            for (float p : m_recentPower) {
                sum += p;
            }
            m_syncPower = m_recentPower.empty() ? 1.0 : sum / m_recentPower.size();
        }
    }
    else if (m_state == FrameInfo)
    {
        int bit = mirror(symbol.m_value & 1, 2);
        m_fiw = (m_fiw << 1) | (uint32_t) bit;

        if (++m_fiwBits == 32)
        {
            RadioPagingBCH::Result result = RadioPagingBCH::decode(m_fiw);
            uint32_t data = RadioPagingFlex::wordData(result.m_word);

            if ((result.m_bitErrors < 0) || !RadioPagingFlex::checksumValid(data) || (((data >> 4) & 0xf) > 14))
            {
                m_frameInfoFailures++;
                backToHunting();
            }
            else
            {
                startData(symbol.m_offset);
            }
        }
    }
}

void RadioPagingFlexReceiver::startData(float offset)
{
    int baud = RadioPagingFlex::baud(m_mode);
    int levels = RadioPagingFlex::levels(m_mode);
    double samplesPerSymbol = m_sampleRate / baud;

    m_dataDemod.create(m_sampleRate, baud, levels);
    m_dataDemod.setLevels(m_syncDemod.getDC(), m_syncDemod.getDeviation());
    m_dataDemod.setLocked(true);
    // The first symbol of the second sync ends one symbol after the end of the frame
    // information word. Its matched filter fills during that symbol
    m_dataDemod.startSymbols(samplesPerSymbol - offset);

    m_s2Symbols = baud * 25 / 1000;
    m_s2Count = 0;
    m_s2Reg = 0;
    m_s2Tail.clear();
    m_s2TailStart = m_s2Symbols - 2;
    m_s2ExactEnd = -1;

    m_dataSymbols = baud * 1760 / 1000;
    m_dataCount = 0;
    m_bitIndex = 0;
    m_secondOfPair = false;
    m_words.assign(RadioPagingFlex::phaseCount(m_mode), std::vector<uint32_t>(RadioPagingFlex::m_wordsPerFrame, 0));
    m_blockPower.assign(RadioPagingFlex::m_blocksPerFrame, 0.0);
    m_blockSamples.assign(RadioPagingFlex::m_blocksPerFrame, 0);
    m_errorSum = 0.0;
    m_errorCount = 0;

    m_state = Sync2;
}

void RadioPagingFlexReceiver::dataSymbol(int symbol, float error, float magsq)
{
    int levels = RadioPagingFlex::levels(m_mode);
    int sym = mirror(symbol, levels);

    if (m_state == Sync2)
    {
        sync2Symbol(sym);
        return;
    }

    m_errorSum += (double) error * error;
    m_errorCount++;
    storeDataSymbol(sym, magsq);

    if (++m_dataCount >= m_dataSymbols) {
        endFrame();
    }
}

void RadioPagingFlexReceiver::sync2Symbol(int sym)
{
    int levels = RadioPagingFlex::levels(m_mode);

    if (levels == 4) {
        m_s2Reg = ((m_s2Reg << 2) | grayToDibit[sym & 3]) & 0xffff;
    } else {
        m_s2Reg = ((m_s2Reg << 1) | (unsigned) (sym & 1)) & 0xffff;
    }

    m_s2Count++;

    if ((m_s2Count >= m_s2TailStart) && (m_s2Tail.size() < 4)) {
        m_s2Tail.push_back((uint8_t) sym);
    }
    if ((m_s2ExactEnd < 0) && (m_s2Count >= m_s2Symbols - 1) && (m_s2Reg == syncCInv)) {
        m_s2ExactEnd = m_s2Count;
    }

    // Decide where the data starts, one symbol after the nominal end at the latest
    if (m_s2Count >= m_s2Symbols + 1)
    {
        int boundary = m_s2Symbols;

        if ((m_s2ExactEnd >= m_s2Symbols - 1) && (m_s2ExactEnd <= m_s2Symbols + 1)) {
            boundary = m_s2ExactEnd;
        }

        m_state = Data;

        // Symbols received after the boundary are data
        for (size_t k = 0; k < m_s2Tail.size(); k++)
        {
            int position = m_s2TailStart + (int) k;
            if (position > boundary)
            {
                storeDataSymbol(m_s2Tail[k], (float) m_syncPower);
                m_dataCount++;
            }
        }
    }
}

void RadioPagingFlexReceiver::storeDataSymbol(int sym, float magsq)
{
    (void) magsq;

    int levels = RadioPagingFlex::levels(m_mode);
    int phases = RadioPagingFlex::phaseCount(m_mode);
    int totalBits = 32 * RadioPagingFlex::m_wordsPerFrame;

    if (m_bitIndex >= totalBits) {
        return;
    }

    int word = m_bitIndex >> 5;
    int bit = 31 - (m_bitIndex & 31);

    auto setBit = [&](int phase, unsigned value) {
        if (value) {
            m_words[phase][word] |= (1u << bit);
        }
    };

    if (phases == 1)
    {
        setBit(0, sym & 1);
        m_bitIndex++;
    }
    else if (levels == 2)
    {
        // 3200/2: bits alternate between phase A and phase C
        setBit(m_secondOfPair ? 1 : 0, sym & 1);
        if (m_secondOfPair) {
            m_bitIndex++;
        }
        m_secondOfPair = !m_secondOfPair;
    }
    else if (phases == 2)
    {
        // 3200/4: a symbol carries a bit of phase A and a bit of phase C
        unsigned dibit = grayToDibit[sym & 3];
        setBit(0, dibit >> 1);
        setBit(1, dibit & 1);
        m_bitIndex++;
    }
    else
    {
        // 6400/4: symbols alternate between (A, B) and (C, D)
        unsigned dibit = grayToDibit[sym & 3];
        int first = m_secondOfPair ? 2 : 0;
        setBit(first, dibit >> 1);
        setBit(first + 1, dibit & 1);
        if (m_secondOfPair) {
            m_bitIndex++;
        }
        m_secondOfPair = !m_secondOfPair;
    }
}

void RadioPagingFlexReceiver::endFrame()
{
    static const char names2[2] = {'A', 'C'};
    static const char names4[4] = {'A', 'B', 'C', 'D'};

    RadioPagingFlexDecoder::RxFrame frame;
    int phases = RadioPagingFlex::phaseCount(m_mode);
    double threshold = m_syncPower * std::pow(10.0, -m_erasureDb / 10.0);

    frame.m_mode = m_mode;
    frame.m_inverted = m_inverted;
    frame.m_hasFiw = true;
    frame.m_fiw = m_fiw;
    double rms = (m_errorCount > 0) ? std::sqrt(m_errorSum / m_errorCount) : 1.0;
    frame.m_quality = std::max(0.0, std::min(1.0, 1.0 - 1.5 * rms));

    std::vector<uint8_t> erased(RadioPagingFlex::m_wordsPerFrame, 0);
    for (int block = 0; block < RadioPagingFlex::m_blocksPerFrame; block++)
    {
        if ((m_blockSamples[block] > 0) && ((m_blockPower[block] / m_blockSamples[block]) < threshold))
        {
            for (int w = 0; w < RadioPagingFlex::m_wordsPerBlock; w++) {
                erased[block * RadioPagingFlex::m_wordsPerBlock + w] = 1;
            }
        }
    }

    for (int p = 0; p < phases; p++)
    {
        RadioPagingFlexDecoder::RxPhase phase;
        phase.m_name = (phases == 1) ? 'A' : (phases == 2) ? names2[p] : names4[p];
        phase.m_words = m_words[p];
        phase.m_erased = erased;
        frame.m_phases.push_back(phase);
    }

    std::vector<RadioPagingFlexDecoder::Message> messages = m_decoder.decodeFrame(frame);
    for (auto& message : messages) {
        m_messages.push_back(message);
    }

    uint32_t data = RadioPagingFlex::wordData(RadioPagingBCH::decode(m_fiw).m_word);
    m_lastCycle = (data >> 4) & 0xf;
    m_lastFrame = (data >> 8) & 0x7f;
    m_lastQuality = (float) frame.m_quality;
    m_frames++;

    backToHunting();
}
