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
float RadioPagingFlexReceiver::s_minQuality2 = 0.84f;
float RadioPagingFlexReceiver::s_minQuality2Corrected = 0.92f;
float RadioPagingFlexReceiver::s_minQuality4 = 0.92f;
float RadioPagingFlexReceiver::s_minQuality4Corrected = 1.00f;

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
// Half a cycle of the emergency re-synchronization, BS and Ar (STD-43A Table 3.2.1-1); the
// other half is its inverse
const uint64_t ersHalf = 0xaaaacb205939ULL;
const uint64_t mask48 = 0xffffffffffffULL;
const int ersCycleBits = 96;
// Cycles missed in noise do not split a re-synchronization in two: a new one after 3 cycles
const int ersGapBits = 3 * ersCycleBits;

int popcount64(uint64_t x)
{
    int n = 0;
    for (; x; n++) {
        x &= x - 1;
    }
    return n;
}

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
    m_syncPower(1.0),
    m_syncSymbols(0),
    m_ersFirst(0),
    m_ersLast(0),
    m_ersDetections(0),
    m_syncs(0),
    m_frames(0),
    m_frameInfoFailures(0),
    m_lastCycle(0),
    m_lastFrame(0),
    m_lastQuality(0.0f)
{
    setPolarities(true, true);
    create(m_sampleRate);
}

void RadioPagingFlexReceiver::create(double sampleRate, const int inputWindows[NbInputs])
{
    m_sampleRate = sampleRate;
    for (int i = 0; i < NbInputs; i++) {
        m_inputWindows[i] = inputWindows ? inputWindows[i] : 1;
    }
    m_syncDemod.create(sampleRate, RadioPagingFlex::m_syncBaud, 2, m_inputWindows[Input1600x2]);
    reset();
}

void RadioPagingFlexReceiver::reset()
{
    m_syncDemod.reset();
    m_decoder.reset();
    m_levelsMeasured = false;
    m_messages.clear();
    m_dedup.reset();
    m_time = 0.0;
    m_recentPower.clear();
    m_syncs = 0;
    m_frames = 0;
    m_frameInfoFailures = 0;
    m_syncSymbols = 0;
    m_ersFirst = 0;
    m_ersLast = 0;
    m_ersDetections = 0;
    m_mode = RadioPagingFlex::Mode1600_2;
    m_inverted = false;
    m_lastQuality = 0.0f;
    backToHunting();
}

// keepHistory: the last bits stay in the sync register, for a sync 1 found false (its frame information word
// failed): the true one may already be on its way
void RadioPagingFlexReceiver::backToHunting(bool keepHistory)
{
    m_state = Hunting;
    if (!keepHistory)
    {
        m_regLo = 0;
        m_regHi = 0;
    }
    m_syncDemod.setLocked(false);
    m_syncDemod.setHoldLevels(false);
}

// A complete message whose content and message number are those of one of the last messages of the pager (address,
// polarity) is a duplicate: a network without numbering sends every message with N = 0 and R = 0, so its messages are
// told apart by content; a numbered message sent again with a new number is a new message to the pager. The decoder
// also marks the retransmissions of numbered messages (R = 0, the words of the stored copy). Fragments, incomplete
// messages and messages without content (tone only) are not remembered
RadioPagingFlexDecoder::Message RadioPagingFlexReceiver::takeMessage()
{
    RadioPagingFlexDecoder::Message message = m_messages.front();
    m_messages.pop_front();

    if (!message.m_isFragment && message.m_complete && !message.m_text.empty())
    {
        std::string content = std::to_string((int) message.m_type) + "|" + std::to_string(message.m_messageNumber) + "|"
            + message.m_text;
        std::vector<uint8_t> data(content.begin(), content.end());
        bool duplicate = m_dedup.check(message.m_inverted ? 1 : 0, RadioPagingFlex::addressText(message.m_capcode), data,
            (int) data.size() * 8, m_time);

        message.m_duplicate = message.m_duplicate || duplicate;
    }

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
    // In a frame the data demodulator has the levels (it tracks them on 2-level data)
    bool inFrame = (m_state == Sync2) || (m_state == Data);
    status.m_dc = inFrame ? m_dataDemod.getDC() : m_syncDemod.getDC();
    // BER and frame report of the last frame; network state of its polarity
    status.m_ber = m_decoder.status().m_ber;

    for (int p = 0; p < 4; p++) {
        status.m_phaseBer[p] = m_decoder.status().m_phaseBer[p];
    }
    status.m_frameReport = m_decoder.lastFrame();
    status.m_networkTime = m_decoder.networkTime(m_inverted);
    status.m_groups = m_decoder.groups(m_inverted);
    status.m_reassemblies = m_decoder.reassemblies(m_inverted);
    status.m_ers = ersActive();
    status.m_ersDetections = m_ersDetections;
    status.m_ersSeconds = (m_ersDetections > 0) ? (float) (m_ersLast - m_ersFirst) / RadioPagingFlex::m_syncBaud : 0.0f;

    return status;
}

// The last 96 bits are a cycle of the re-synchronization, starting at either half
bool RadioPagingFlexReceiver::checkErs() const
{
    uint64_t newer = m_regLo & mask48;
    uint64_t older = (((m_regHi & 0xffffffffULL) << 16) | (m_regLo >> 48)) & mask48;
    uint64_t other = ~ersHalf & mask48;
    int errors = std::min(popcount64(older ^ ersHalf) + popcount64(newer ^ other),
        popcount64(older ^ other) + popcount64(newer ^ ersHalf));

    return errors <= m_maxErsErrors;
}

// A cycle matches every half cycle while it lasts: active until two halves are missed
bool RadioPagingFlexReceiver::ersActive() const
{
    return (m_ersDetections > 0) && (m_syncSymbols - m_ersLast <= (uint64_t) ersCycleBits);
}

void RadioPagingFlexReceiver::process(float sample, float magsq)
{
    const float inputs[NbInputs] = {sample, sample, sample, sample};
    process(inputs, magsq);
}

RadioPagingFlexReceiver::Input RadioPagingFlexReceiver::dataInput() const
{
    bool fast = RadioPagingFlex::baud(m_mode) > RadioPagingFlex::m_syncBaud;

    if (RadioPagingFlex::levels(m_mode) == 4) {
        return fast ? Input3200x4 : Input1600x4;
    } else {
        return fast ? Input3200x2 : Input1600x2;
    }
}

void RadioPagingFlexReceiver::process(const float inputs[NbInputs], float magsq, const int decisions[NbInputs], const float qualities[NbInputs])
{
    RadioPagingFskDemod::Symbol symbol;

    m_time += 1.0 / m_sampleRate;

    // The sync demodulator keeps its symbol clock running through the data part, so the
    // next frame's sync is found without a new acquisition
    if (m_syncDemod.process(inputs[Input1600x2], symbol))
    {
        m_syncSymbols++;

        if ((m_state == Hunting) || (m_state == FrameInfo)) {
            syncSymbol(symbol, magsq);
        }
    }

    if ((m_state == Sync2) || (m_state == Data))
    {
        float sample = inputs[dataInput()];
        int decision = decisions ? decisions[dataInput()] : -1;

        if (m_dataDemod.process(sample, decision, symbol))
        {
            if ((m_state == Data) && (m_dataCount < m_dataSymbols) && qualities && !std::isnan(qualities[dataInput()]))
            {
                int block = std::min(m_bitIndex / (32 * RadioPagingFlex::m_wordsPerBlock), RadioPagingFlex::m_blocksPerFrame - 1);
                m_blockQuality[block] += qualities[dataInput()];
                m_blockQualityCount[block]++;
            }
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

// Known levels of A, B and inverted A of a mode, most significant bit first; a 1 is a positive
// deviation in normal polarity
void RadioPagingFlexReceiver::fitSync1(RadioPagingFlex::Mode mode, bool inverted)
{
    std::vector<float> known;
    auto append = [&](uint32_t word, int bits) {
        for (int i = bits - 1; i >= 0; i--) {
            known.push_back((((word >> i) & 1) != 0) != inverted ? 1.0f : -1.0f);
        }
    };

    append(codeA[(int) mode], 32);
    append(codeB, 16);
    append(codeAInv[(int) mode], 32);

    if (m_syncDemod.fitLevels(known))
    {
        m_levelsMeasured = true;
        m_measuredDc = m_syncDemod.getDC();
    }

    m_syncDemod.setHoldLevels(true);
}

bool RadioPagingFlexReceiver::takeLevelsMeasured(float& dc)
{
    if (!m_levelsMeasured) {
        return false;
    }

    m_levelsMeasured = false;
    dc = m_measuredDc;
    return true;
}

void RadioPagingFlexReceiver::offsetLevels(float delta)
{
    m_syncDemod.offsetLevels(delta);
    m_dataDemod.offsetLevels(delta);
}

bool RadioPagingFlexReceiver::checkSync(RadioPagingFlex::Mode& mode, bool& inverted) const
{
    // Bits 79-48: A, 47-32: B, 31-0: inverted A; bits 95-80: the end of bit sync 1, reversals
    uint32_t a = (uint32_t) (((m_regHi & 0xffff) << 16) | (m_regLo >> 48));
    uint32_t b = (uint32_t) ((m_regLo >> 32) & 0xffff);
    uint32_t aInv = (uint32_t) m_regLo;
    uint32_t bitSync = (uint32_t) ((m_regHi >> 16) & 0xffff);
    int bitSyncErrors = std::min(popcount32(bitSync ^ 0xaaaau), popcount32(bitSync ^ 0x5555u));
    int best = syncBits + 1;
    int bestMode = -1;
    bool bestInverted = false;

    // Both polarities are scored, so the sync of a disabled one is rejected rather than
    // taken for the other
    for (int pol = 0; pol < 2; pol++)
    {
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
            // Sync 1 follows bit sync 1: a sync with bit errors must have the reversals before A. Without this
            // the reversals and A of a sync 1 to come pass as an inverted B and inverted A, 48 bits early
            bool accepted = ((errors <= m_maxSyncErrors) || copiesAgree) && ((errors == 0) || (bitSyncErrors <= m_maxBitSyncErrors));

            if (accepted && (errors < best))
            {
                best = errors;
                bestMode = m;
                bestInverted = (pol == 1);
            }
        }
    }

    if ((bestMode < 0) || !m_enabled[bestInverted ? 1 : 0]) {
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
        bool sync = checkSync(mode, inverted);

        // Sync 1 is closer to the re-synchronization than random bits: not checked on it
        if (!sync && (m_enabled[0] || m_enabled[1]) && checkErs())
        {
            // A new one after a gap of more than ersGapBits; it started a cycle ago
            if ((m_ersDetections == 0) || (m_syncSymbols - m_ersLast > (uint64_t) ersGapBits))
            {
                m_ersDetections++;
                m_ersFirst = m_syncSymbols - std::min<uint64_t>(m_syncSymbols, ersCycleBits);
            }
            m_ersLast = m_syncSymbols;
        }

        if (sync)
        {
            m_mode = mode;
            m_inverted = inverted;
            m_fiw = 0;
            m_fiwBits = 0;
            m_syncs++;
            m_state = FrameInfo;
            m_syncDemod.setLocked(true);
            // DC offset and deviation are measured on A, B and inverted A, the last 80
            // symbols, and held through the frame (the sync demodulator then sees symbols of
            // the frame's rate and levels) until it hunts again
            fitSync1(mode, inverted);

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
        // The sync register goes on: if this sync 1 is false, the true one may be in these bits
        m_regHi = (m_regHi << 1) | (m_regLo >> 63);
        m_regLo = (m_regLo << 1) | (uint64_t) (symbol.m_value & 1);

        if (++m_fiwBits == 32)
        {
            RadioPagingBCH::Result result = RadioPagingBCH::decode(m_fiw);
            uint32_t data = RadioPagingFlex::wordData(result.m_word);

            if ((result.m_bitErrors < 0) || !RadioPagingFlex::checksumValid(data) || (((data >> 4) & 0xf) > 14))
            {
                m_frameInfoFailures++;
                backToHunting(true);
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

    m_dataDemod.create(m_sampleRate, baud, levels, m_inputWindows[dataInput()]);
    m_dataDemod.setLevels(m_syncDemod.getDC(), m_syncDemod.getDeviation());
    m_dataDemod.setLocked(true);
    // The levels measured on sync 1 are kept for the frame
    m_dataDemod.setHoldLevels(true);
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
    m_blockQuality.assign(RadioPagingFlex::m_blocksPerFrame, 0.0);
    m_blockQualityCount.assign(RadioPagingFlex::m_blocksPerFrame, 0);
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
    const bool fourLevel = RadioPagingFlex::levels(m_mode) == 4;
    const float minQuality = fourLevel ? s_minQuality4 : s_minQuality2;
    const float minQualityCorrected = fourLevel ? s_minQuality4Corrected : s_minQuality2Corrected;
    std::vector<float> blockQuality(RadioPagingFlex::m_blocksPerFrame, NAN);

    for (int block = 0; block < RadioPagingFlex::m_blocksPerFrame; block++)
    {
        if (m_blockQualityCount[block] > 0) {
            blockQuality[block] = (float) (m_blockQuality[block] / m_blockQualityCount[block]);
        }

        // A block too weak (power below sync 1) or of a signal too weak for the corrections to be right
        bool weak = (m_blockSamples[block] > 0) && ((m_blockPower[block] / m_blockSamples[block]) < threshold);
        bool poor = !std::isnan(blockQuality[block]) && (blockQuality[block] < minQuality);

        if (weak || poor)
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

        // A word corrected in two bits or more is taken from a higher quality: a word of a signal too weak
        // becomes a wrong codeword that way
        for (int w = 0; w < RadioPagingFlex::m_wordsPerFrame; w++)
        {
            float quality = blockQuality[std::min(w / RadioPagingFlex::m_wordsPerBlock, RadioPagingFlex::m_blocksPerFrame - 1)];

            if (!phase.m_erased[w] && !std::isnan(quality) && (quality < minQualityCorrected))
            {
                RadioPagingBCH::Result r = RadioPagingBCH::decode(phase.m_words[w]);
                if ((r.m_bitErrors >= 0) && (r.m_bitErrors + (r.m_parityError ? 1 : 0) >= 2)) {
                    phase.m_erased[w] = 1;
                }
            }
        }

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
