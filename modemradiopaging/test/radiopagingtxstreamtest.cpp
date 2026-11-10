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

// Self test of the transmitter streams: block structure, POCSAG carousel, FLEX
// re-synchronization and frame timing. Reception of the streams is tested through the
// modulator and the demodulator in the modulator plugin test.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "radiopagingtxstream.h"

namespace {

int passes = 0;
int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (ok) {
        passes++;
    } else {
        failures++;
    }
}

const int batchBits = 32 * 17;

RadioPagingPocsag::TxMessage pocsag(uint32_t address, const char *text)
{
    RadioPagingPocsag::TxMessage m;
    m.m_address = address;
    m.m_function = 3;
    m.m_type = RadioPagingPocsag::Alphanumeric;
    m.m_text = text;
    return m;
}

RadioPagingFlexEncoder::TxMessage flex(uint64_t capcode, const char *text)
{
    RadioPagingFlexEncoder::TxMessage m;
    m.m_capcode = capcode;
    m.m_type = RadioPagingFlex::Alphanumeric;
    m.m_text = text;
    return m;
}

// Sign of each level: the bits of a POCSAG block in normal polarity (1 = negative)
std::vector<uint8_t> pocsagBits(const RadioPagingTxBlock& block)
{
    std::vector<uint8_t> bits;
    for (float l : block.m_levels) {
        bits.push_back(l < 0.0f ? 1 : 0);
    }
    return bits;
}

uint32_t word(const std::vector<uint8_t>& bits, size_t at)
{
    uint32_t w = 0;
    for (size_t i = 0; i < 32; i++) {
        w = (w << 1) | bits[at + i];
    }
    return w;
}

void testPocsagNetwork()
{
    RadioPagingPocsagTxStream stream(true, 1200, false);
    RadioPagingTxBlock block;
    std::string error;

    check(stream.hasWork() && stream.continuous(), "POCSAG network: always has work, continuous");
    stream.begin(0);

    // Preamble, then idle batches
    bool ok = stream.next(block);
    check(ok && (block.m_baud == 1200) && (block.m_levels.size() == (size_t) RadioPagingPocsag::m_preambleBits)
        && block.m_ids.empty(), "POCSAG network: starts with the preamble at the default group");

    ok = stream.next(block);
    std::vector<uint8_t> bits = pocsagBits(block);
    check(ok && (bits.size() == (size_t) batchBits) && (word(bits, 0) == RadioPagingPocsag::m_syncWord)
        && (word(bits, 32) == RadioPagingPocsag::m_idleWord) && (word(bits, 32 * 16) == RadioPagingPocsag::m_idleWord),
        "POCSAG network: idle batch (sync and 16 idle codewords) when nothing is queued");

    // A message of the current group goes in the next batches
    check(stream.add(pocsag(1234567, "NETWORK"), 1200, false, 7, error), "POCSAG network: message queued");
    ok = stream.next(block);
    bits = pocsagBits(block);
    check(ok && (block.m_ids.size() == 1) && (block.m_ids[0] == 7) && ((bits.size() % batchBits) == 0)
        && (word(bits, 0) == RadioPagingPocsag::m_syncWord) && (stream.pending() == 0),
        "POCSAG network: the message at the next batch, whole batches, no preamble");

    // A message of another group: two idle batches, then its preamble and its batches
    check(stream.add(pocsag(7654, "OTHER"), 512, true, 8, error), "POCSAG network: message of another group queued");
    RadioPagingTxBlock b1, b2, b3, b4;
    stream.next(b1);
    stream.next(b2);
    stream.next(b3);
    stream.next(b4);
    check((b1.m_baud == 1200) && (b1.m_levels.size() == (size_t) batchBits) && b1.m_ids.empty()
        && (b2.m_baud == 1200) && (b2.m_levels.size() == (size_t) batchBits) && b2.m_ids.empty(),
        "POCSAG network: two idle batches before switching group");
    check((b3.m_baud == 512) && (b3.m_levels.size() == (size_t) RadioPagingPocsag::m_preambleBits)
        && (b3.m_levels[0] == 1.0f), "POCSAG network: preamble of the next group (512 inverted: its first bit, 1, is positive)");
    std::vector<uint8_t> inv = pocsagBits(b4);
    for (auto& b : inv) {
        b ^= 1;     // Inverted polarity
    }
    check((b4.m_baud == 512) && (b4.m_ids.size() == 1) && (b4.m_ids[0] == 8) && (word(inv, 0) == RadioPagingPocsag::m_syncWord),
        "POCSAG network: its batches in its polarity");

    // Clear drops what is queued
    stream.add(pocsag(1, "A"), 2400, false, 9, error);
    std::vector<uint64_t> dropped = stream.clear();
    check((dropped.size() == 1) && (dropped[0] == 9) && (stream.pending() == 0), "POCSAG network: clear returns the dropped ids");
    check(!stream.add(pocsag(2007665, "IDLE"), 1200, false, 10, error) && !error.empty(),
        "POCSAG: address of the idle codeword refused");
}

// A group fed at every batch does not hold the others; with nothing queued the network goes
// back to its default group, a new default too
void testPocsagNetworkTurns()
{
    RadioPagingPocsagTxStream stream(true, 1200, false);
    RadioPagingTxBlock block;
    std::string error;
    uint64_t id = 100;

    stream.begin(0);
    stream.next(block);     // Preamble
    stream.add(pocsag(7654, "WAITING"), 512, false, 1, error);

    int blocks1200 = 0;
    bool otherSent = false;

    for (int i = 0; (i < 20) && !otherSent; i++)
    {
        stream.add(pocsag(1000 + i, "BUSY"), 1200, false, id++, error);     // A message at every batch
        stream.next(block);
        if ((block.m_baud == 1200) && !block.m_ids.empty()) {
            blocks1200++;
        }
        otherSent = (block.m_baud == 512) && !block.m_ids.empty() && (block.m_ids[0] == 1);
    }

    check(otherSent && (blocks1200 == RadioPagingPocsagTxStream::m_maxBlocksPerTurn),
        "POCSAG network: a busy group gives way after its turn of message blocks");

    // Nothing queued: back to the default group after the idle batches; then a new default
    stream.clear();
    bool backTo1200 = false;
    for (int i = 0; (i < 10) && !backTo1200; i++)
    {
        stream.next(block);
        backTo1200 = (block.m_baud == 1200) && (block.m_levels.size() == (size_t) RadioPagingPocsag::m_preambleBits);
    }
    stream.setDefaultGroup(2400, true);
    bool to2400 = false;
    for (int i = 0; (i < 10) && !to2400; i++)
    {
        stream.next(block);
        to2400 = (block.m_baud == 2400) && (block.m_levels.size() == (size_t) RadioPagingPocsag::m_preambleBits);
    }
    check(backTo1200 && to2400, "POCSAG network: idle, it returns to the default group, a changed default is taken while running");
}

void testPocsagOneShot()
{
    RadioPagingPocsagTxStream stream(false, 1200, false);
    RadioPagingTxBlock block;
    std::string error;

    check(!stream.hasWork() && !stream.continuous(), "POCSAG one-shot: no work when nothing is queued");
    stream.add(pocsag(100, "ONE"), 512, false, 1, error);
    stream.add(pocsag(200, "TWO"), 2400, true, 2, error);
    check(stream.hasWork() && (stream.pending() == 2), "POCSAG one-shot: work when messages are queued");

    stream.begin(0);
    bool a = stream.next(block);
    bool first = a && (block.m_baud == 512) && (block.m_ids.size() == 1) && (block.m_ids[0] == 1)
        && (block.m_levels.size() > (size_t) RadioPagingPocsag::m_preambleBits);
    bool end = !stream.next(block);
    stream.begin(0);
    bool b = stream.next(block);
    bool second = b && (block.m_baud == 2400) && (block.m_ids.size() == 1) && (block.m_ids[0] == 2);
    bool end2 = !stream.next(block);
    check(first && end && second && end2 && !stream.hasWork(),
        "POCSAG one-shot: one transmission per message, preamble included, each ends");
}

void testFlexPlan()
{
    uint64_t frame;
    int ers;

    // Collapse 0: at least 1.875 s, to the frame boundary after
    RadioPagingFlexTxStream::plan(1000, 0, frame, ers);
    check((frame == 2) && (ers == 4400), "FLEX plan: start at 1.000 s, collapse 0: first frame 2 (3.750 s), 4400 bits of re-synchronization");

    // On a boundary: exactly the minimum
    RadioPagingFlexTxStream::plan(3750, 0, frame, ers);
    check((frame == 3) && (ers == 3000), "FLEX plan: start on a frame boundary (3.750 s): one frame of re-synchronization, first frame 3");

    // Collapse 3: 8 frames
    RadioPagingFlexTxStream::plan(0, 3, frame, ers);
    check((frame == 8) && (ers == 24000), "FLEX plan: collapse 3: 8 frames (15 s) of re-synchronization");

    // The time grid: frame and cycle of an hour (cycle = minute / 4)
    RadioPagingFlexScheduler::FrameTime t = RadioPagingFlexScheduler::frameTime(RadioPagingFlexTxStream::frameIndex(
        (int64_t) (3600 + 4 * 60 + 3) * 1000));
    check((t.m_cycle == 1) && (t.m_frame == 1), "FLEX time grid: 1 h 04 min 03 s is cycle 1 frame 1");
}

void testFlexErs()
{
    RadioPagingFlex::SymbolBlock ers = RadioPagingFlex::ers(200, false);
    // BS, Ar, inverted BS, inverted Ar (Table 3.2.1-1), transmission order
    const char *cycle = "1010101010101010" "11001011001000000101100100111001" "0101010101010101" "00110100110111111010011011000110";
    bool ok = (ers.m_baud == 1600) && (ers.m_levels == 2) && (ers.m_symbols.size() == 200);

    for (int i = 0; ok && (i < 200); i++) {
        ok = (ers.m_symbols[i] == (cycle[i % 96] - '0'));
    }
    check(ok, "FLEX re-synchronization: BS, Ar, inverted BS, inverted Ar repeated (STD-43A Table 3.2.1-1)");
    check(RadioPagingFlex::ersMinBits(7) == 3000 * 128, "FLEX re-synchronization: collapse 7 is 128 frames");
}

double seconds(const RadioPagingTxBlock& block)
{
    return (double) block.m_levels.size() / block.m_baud;
}

void testFlexOneShot()
{
    RadioPagingFlexTxStream stream(false, RadioPagingFlex::Mode1600_2, false, 0);
    RadioPagingTxBlock block;
    std::string error;

    check(!stream.hasWork(), "FLEX one-shot: no work when nothing is queued");
    check(stream.add(flex(123456, "FLEX ONE SHOT"), false, 5, error), "FLEX one-shot: message queued");
    check(!stream.add(flex(0, "BAD"), false, 6, error) && !error.empty(), "FLEX: capcode 0 refused");

    stream.begin(1000);
    bool ok = stream.next(block);
    check(ok && (block.m_baud == 1600) && (block.m_levels.size() == 4400) && block.m_ids.empty(),
        "FLEX one-shot: starts with the re-synchronization to the frame boundary");

    // Frames until the message is sent, each 1.875 s, then the end
    double frameSeconds = 0.0;
    int frames = 0;
    std::vector<uint64_t> sent;
    bool durationsOk = true;

    while (stream.next(block) && (frames < 10))
    {
        frameSeconds += seconds(block);
        sent.insert(sent.end(), block.m_ids.begin(), block.m_ids.end());

        if (std::fabs(frameSeconds - 1.875) < 1e-9)
        {
            frames++;
            frameSeconds = 0.0;
        }
        else if (frameSeconds > 1.875 + 1e-9)
        {
            durationsOk = false;
            break;
        }
    }

    check(durationsOk && (frames == 1) && (frameSeconds == 0.0), "FLEX one-shot: frames of exactly 1.875 s; collapse 0: one frame");
    check((sent.size() == 1) && (sent[0] == 5) && !stream.hasWork(), "FLEX one-shot: the message sent, then the transmission ends");

    // One-shot collapse 3: the re-synchronization reaches pagers of collapse 3 (8 frames),
    // the frames are scheduled with collapse 0: the message is in the first frame
    RadioPagingFlexTxStream collapsed(false, RadioPagingFlex::Mode1600_2, false, 3);
    uint64_t firstFrame;
    int ersBits;
    RadioPagingFlexTxStream::plan(1000, 3, firstFrame, ersBits);
    collapsed.add(flex(123457, "FLEX ONE SHOT"), false, 7, error);
    collapsed.begin(1000);
    bool ers = collapsed.next(block) && block.m_ers && ((int) block.m_levels.size() == ersBits)
        && (ersBits >= RadioPagingFlex::ersMinBits(3));
    double firstSeconds = 0.0;
    bool inFirst = false;

    while (collapsed.next(block) && (firstSeconds < 1.875 - 1e-9))
    {
        firstSeconds += seconds(block);
        inFirst = inFirst || (!block.m_ers && !block.m_ids.empty() && (block.m_ids[0] == 7));
    }

    check((collapsed.scheduleCollapse() == 0) && (collapsed.ersCollapse() == 3) && ers && inFirst,
        "FLEX one-shot collapse 3: re-synchronization of 8 frames, flagged; message in the first frame (collapse 0)");

    // Network collapse 2: both
    RadioPagingFlexTxStream network(true, RadioPagingFlex::Mode1600_2, false, 2);
    check((network.scheduleCollapse() == 2) && (network.ersCollapse() == 2), "FLEX network: the collapse schedules the frames and sets the re-synchronization");
}

void testFlexNetwork()
{
    RadioPagingFlexTxStream stream(true, RadioPagingFlex::Mode3200_4, false, 0);
    RadioPagingTxBlock block;
    std::string error;

    stream.begin(0);
    stream.next(block);     // Re-synchronization
    uint64_t first = stream.currentFrame();

    // Idle frames without end
    double total = 0.0;
    for (int i = 0; i < 6; i++)
    {
        stream.next(block);
        total += seconds(block);
    }
    check((std::fabs(total - 3 * 1.875) < 1e-9) && (stream.currentFrame() == first + 3),
        "FLEX network: idle frames one after the other, numbered by their time");

    // A message of the network: the frame in the mode of the network
    stream.add(flex(123456, "FAST"), false, 11, error);
    RadioPagingTxBlock sync, data;
    stream.next(sync);
    stream.next(data);
    check((sync.m_baud == 1600) && (data.m_baud == 1600) && (data.m_ids.size() == 1) && (data.m_ids[0] == 11)
        && (std::fabs(seconds(sync) + seconds(data) - 1.875) < 1e-9),
        "FLEX network: the frame in the mode of the network (3200/4 is 1600 baud 4 levels), 1.875 s");
    bool fourLevels = false;
    for (float l : data.m_levels) {
        fourLevels = fourLevels || (std::fabs(std::fabs(l) - 1.0f / 3.0f) < 1e-6);
    }
    check(fourLevels, "FLEX network: 4-level data part");

    // Collapse 2: a capcode listens to one frame in 4; the message waits for it
    RadioPagingFlexTxStream c2(true, RadioPagingFlex::Mode1600_2, false, 2);
    c2.begin(0);
    c2.next(block);
    uint64_t capcode = 16 * 3 + 1;      // Frame 3 of each cycle, 3 mod 4
    c2.add(flex(capcode, "WAIT"), false, 12, error);
    uint64_t sentIn = 0;
    for (int i = 0; (i < 20) && (sentIn == 0); i++)
    {
        uint64_t frameIndex = c2.currentFrame();
        RadioPagingTxBlock s, d;
        c2.next(s);
        c2.next(d);
        if (!d.m_ids.empty()) {
            sentIn = frameIndex;
        }
    }
    check((sentIn != 0) && ((sentIn % 128) % 4 == 3), "FLEX network collapse 2: the message in a frame its capcode listens to");

    // The other polarity: the frame its message is due in goes to it (the network has nothing
    // mandatory), the other frames stay in the network polarity
    RadioPagingFlexTxStream mixed(true, RadioPagingFlex::Mode1600_2, false, 2);
    mixed.begin(0);
    mixed.next(block);
    mixed.add(flex(capcode, "INVERTED"), true, 30, error);
    mixed.add(flex(16 * 1 + 1, "NORMAL"), false, 31, error);    // Frame 1 mod 4
    uint64_t invertedIn = 0, normalIn = 0;
    int invertedFrames = 0;
    for (int i = 0; (i < 8) && ((invertedIn == 0) || (normalIn == 0)); i++)
    {
        uint64_t frameIndex = mixed.currentFrame();
        RadioPagingTxBlock s, d;
        mixed.next(s);
        mixed.next(d);
        // Sync 1 of the frame: its bit sync starts 1,0 in normal polarity (a positive level first)
        bool inverted = s.m_levels[0] < 0.0f;
        invertedFrames += inverted ? 1 : 0;
        if (!d.m_ids.empty() && (d.m_ids[0] == 30) && inverted) {
            invertedIn = frameIndex;
        }
        if (!d.m_ids.empty() && (d.m_ids[0] == 31) && !inverted) {
            normalIn = frameIndex;
        }
    }
    check((invertedIn % 4 == 3) && (normalIn % 4 == 1) && (invertedFrames == 1) && !mixed.inverted(),
        "FLEX network: a frame goes to the other polarity for its message, the others stay in the network polarity");
}

// A transmission cut in a frame: the ids of the frame are returned, not lost
void testFlexCut()
{
    RadioPagingFlexTxStream stream(false, RadioPagingFlex::Mode1600_2, false, 0);
    RadioPagingTxBlock block;
    std::string error;

    stream.add(flex(123456, "CUT"), false, 20, error);
    stream.begin(0);
    stream.next(block);     // Re-synchronization
    stream.next(block);     // Sync part of the frame: the message is out of the queue
    std::vector<uint64_t> cut = stream.cut();
    check((cut.size() == 1) && (cut[0] == 20) && (stream.pending() == 0) && stream.clear().empty() && !stream.next(block),
        "FLEX cut in a frame: its message id returned once, nothing left");
}

} // namespace

// ---------------------------------------------------------------------------
// FLEX mixed with POCSAG
// ---------------------------------------------------------------------------

struct Step
{
    RadioPagingTxBlock m_block;
    int64_t m_start;        //!< Ticks since the first frame, after the re-synchronization
};

// The blocks after the re-synchronization, with their start times; stops after count blocks
std::vector<Step> mixRun(RadioPagingMixTxStream& stream, int count)
{
    std::vector<Step> steps;
    RadioPagingTxBlock block;
    int64_t t = 0;

    for (int i = 0; i < count; i++)
    {
        stream.next(block);
        Step s;
        s.m_block = block;
        s.m_start = t;
        t += block.ticks();
        steps.push_back(s);
    }

    return steps;
}

RadioPagingMixTxStream::Config mixConfig()
{
    RadioPagingMixTxStream::Config c;
    c.m_flexMode = RadioPagingFlex::Mode1600_2;
    c.m_collapse = 0;
    c.m_pocsagBaud = 1200;
    return c;
}

void testMixed()
{
    const int64_t F = RadioPagingMixTxStream::m_frameTicks;
    std::string error;
    RadioPagingTxBlock block;

    // Nothing queued: FLEX idle frames, one after the other
    {
        RadioPagingMixTxStream stream(mixConfig());
        stream.begin(0);
        stream.next(block);     // Re-synchronization
        check(block.m_ers && (block.m_deviation == 4800.0), "Mixed: starts with the re-synchronization, FLEX deviation");
        std::vector<Step> steps = mixRun(stream, 12);
        int64_t total = 0;
        bool flexOnly = true;
        for (const Step& s : steps) {
            total += s.m_block.ticks();
            flexOnly = flexOnly && (s.m_block.m_deviation == 4800.0) && (s.m_block.m_baud == 1600);
        }
        check(flexOnly && (total == 6 * F), "Mixed: nothing queued, FLEX idle frames fill the time, 1.875 s each");
    }

    // POCSAG in the gap after the mandatory frames 1-3: switch pattern, preamble and batches, then
    // 1,0 up to the frame boundary; every FLEX frame stays on its grid
    {
        RadioPagingMixTxStream stream(mixConfig());
        stream.begin(0);
        stream.next(block);
        uint64_t first = stream.currentFrame();     // Frame 1 of the cycle
        check(stream.addPocsag(pocsag(1234567, "MIXED"), 1200, false, 50, error), "Mixed: POCSAG message queued");
        std::vector<Step> steps = mixRun(stream, 14);

        int sw = -1, data = -1, fill = -1;
        for (size_t i = 0; i < steps.size(); i++)
        {
            const RadioPagingTxBlock& b = steps[i].m_block;
            if ((b.m_baud == 750.0) && (sw < 0)) {
                sw = (int) i;
            } else if (!b.m_ids.empty() && (data < 0)) {
                data = (int) i;
            } else if ((data >= 0) && (fill < 0) && (b.m_levels.size() < 3000) && (b.m_baud > 1500.0) && (b.m_baud < 1700.0)
                       && (b.m_baud != 1600.0)) {
                fill = (int) i;
            }
        }

        bool ok = (sw >= 0) && (data == sw + 1) && (fill == data + 1);
        check(ok, "Mixed: switch pattern, POCSAG block, 1,0 fill");

        if (ok)
        {
            const RadioPagingTxBlock& s = steps[sw].m_block;
            const RadioPagingTxBlock& d = steps[data].m_block;
            const RadioPagingTxBlock& f = steps[fill].m_block;
            // 200 ms at 750 bps for 1200, POCSAG parameters, the POCSAG start frame is after frames 1-3
            check((s.m_levels.size() == 150) && (s.m_deviation == 4500.0) && (d.m_baud == 1200.0) && (d.m_deviation == 4500.0)
                && (d.m_ids[0] == 50) && (d.m_shaping == 1200.0) && (f.m_deviation == 4800.0),
                "Mixed: 200 ms switch pattern for 1200, POCSAG deviation and shaping, FLEX deviation on the fill");
            int64_t run = s.ticks() + d.ticks() + f.ticks();
            int64_t start = steps[sw].m_start;
            check((start % F == 0) && (run == F) && (start / F == 3), "Mixed: the POCSAG run starts at frame 4 and ends on the frame grid");
        }

        // Every FLEX frame starts on the grid: after the POCSAG run the blocks add up to whole frames
        int64_t total = 0;
        for (const Step& st : steps) {
            total += st.m_block.ticks();
        }
        for (int i = 0; (i < 4) && ((total % F) != 0); i++)
        {
            stream.next(block);
            total += block.ticks();
        }
        check(((total % F) == 0) && (stream.currentFrame() == first + (uint64_t) (total / F)) && (stream.ticksToBoundary() == 0),
            "Mixed: all blocks add up to whole frames, the frame counter follows");
    }

    // Two groups in one gap: one switch pattern, the second group has its preamble only
    {
        RadioPagingMixTxStream stream(mixConfig());
        stream.begin(0);
        stream.next(block);
        stream.addPocsag(pocsag(100, "ONE"), 1200, false, 60, error);
        stream.addPocsag(pocsag(200, "TWO"), 512, true, 61, error);
        stream.addPocsag(pocsag(300, "THREE"), 2400, false, 62, error);
        std::vector<Step> steps = mixRun(stream, 30);
        int switches = 0, dataBlocks = 0;
        std::vector<uint64_t> order;
        for (const Step& s : steps)
        {
            switches += (s.m_block.m_baud == 750.0) ? 1 : 0;
            if (!s.m_block.m_ids.empty())
            {
                dataBlocks++;
                order.push_back(s.m_block.m_ids[0]);
            }
        }
        // Carousel: 1200n, 512n, 2400n, 1200i, 512i, 2400i: from 1200n the next with messages is 2400n
        check((switches == 1) && (dataBlocks == 3) && (order == std::vector<uint64_t>{60, 62, 61}),
            "Mixed: three groups (1200, 2400, 512 inverted) in one gap, one switch pattern, carousel order");
    }

    // 2400 and 512 switch patterns: 100 ms and 400 ms
    {
        RadioPagingMixTxStream::Config c = mixConfig();
        c.m_pocsagBaud = 2400;
        RadioPagingMixTxStream s2400(c);
        s2400.begin(0);
        s2400.next(block);
        s2400.addPocsag(pocsag(100, "FAST"), 2400, false, 70, error);
        std::vector<Step> a = mixRun(s2400, 14);
        size_t n2400 = 0;
        for (const Step& s : a) {
            n2400 = (s.m_block.m_baud == 750.0) ? s.m_block.m_levels.size() : n2400;
        }

        c.m_pocsagBaud = 512;
        RadioPagingMixTxStream s512(c);
        s512.begin(0);
        s512.next(block);
        s512.addPocsag(pocsag(100, "SLOW"), 512, false, 71, error);
        std::vector<Step> b = mixRun(s512, 14);
        size_t n512 = 0;
        for (const Step& s : b) {
            n512 = (s.m_block.m_baud == 750.0) ? s.m_block.m_levels.size() : n512;
        }
        check((n2400 == 75) && (n512 == 300), "Mixed: switch pattern of 100 ms for 2400 and 400 ms for 512");
    }

    // A FLEX message due: its frame is FLEX, the POCSAG message waits behind it
    {
        RadioPagingMixTxStream stream(mixConfig());
        stream.begin(0);
        stream.next(block);
        stream.addPocsag(pocsag(100, "LATE"), 1200, false, 80, error);
        stream.addFlex(flex(123456, "FIRST"), false, 81, error);
        std::vector<Step> steps = mixRun(stream, 20);
        int flexAt = -1, pocsagAt = -1;
        for (size_t i = 0; i < steps.size(); i++)
        {
            const RadioPagingTxBlock& b = steps[i].m_block;
            if (!b.m_ids.empty() && (b.m_ids[0] == 81) && (flexAt < 0)) {
                flexAt = (int) i;
            }
            if (!b.m_ids.empty() && (b.m_ids[0] == 80) && (pocsagAt < 0)) {
                pocsagAt = (int) i;
            }
        }
        // Frames 1-3 are FLEX anyway: the FLEX message goes in frame 1
        check((flexAt >= 0) && (pocsagAt > flexAt) && (steps[flexAt].m_start < 2 * F), "Mixed: the FLEX message in the first frame, POCSAG after the mandatory frames");
    }

    // 7.1.2 plan: 2 frames a minute, collapse 5; capcodes of the other frames refused; a frame not
    // transmitted is filled with 1,0, never with the re-synchronization
    {
        RadioPagingMixTxStream::Config c = mixConfig();
        c.m_framesPerMinute = 2;
        RadioPagingMixTxStream stream(c);
        stream.begin(0);
        stream.next(block);
        std::string e1, e2;
        bool refused = !stream.addFlex(flex(16 * 10 + 1, "NO"), false, 90, e1) && !e1.empty();
        bool accepted = stream.addFlex(flex(16 * 1 + 1, "YES"), false, 91, e2);
        check(refused && accepted && (stream.flex().scheduleCollapse() == 5), "Mixed 7.1.2: capcode of an untransmitted frame refused, system collapse 5");

        std::vector<Step> steps = mixRun(stream, 60);
        bool noErs = true;
        int fills = 0;
        for (const Step& s : steps)
        {
            noErs = noErs && !s.m_block.m_ers;
            fills += ((s.m_block.m_baud > 1599.0) && (s.m_block.m_baud < 1601.0) && (s.m_block.m_levels.size() == 3000) && (s.m_block.m_levels[0] > 0.0f)
                && (s.m_block.m_levels[1] < 0.0f)) ? 1 : 0;
        }
        check(noErs && (fills > 0), "Mixed 7.1.2: frames not transmitted are filled with 1,0 at 1600 bps");
    }

    // Time kept at 512 and 2400 bps (no residual dropped): after every POCSAG or fill block the
    // time the blocks took is the time the stream counts
    for (int baud : {512, 2400, 1200})
    {
        RadioPagingMixTxStream::Config c = mixConfig();
        c.m_pocsagBaud = baud;
        c.m_collapse = 2;
        RadioPagingMixTxStream stream(c);
        stream.begin(0);
        stream.next(block);
        uint64_t first = stream.currentFrame();
        int64_t total = 0;
        bool consistent = true;
        int checked = 0;

        for (int i = 0; i < 300; i++)
        {
            if ((i % 3) == 0)
            {
                std::string text(1 + (i * 37) % 200, 'A');
                stream.addPocsag(pocsag(1000 + i, text.c_str()), baud, false, 500 + i, error);
            }

            stream.next(block);
            total += block.ticks();
            bool pocsagBlock = (block.m_deviation == 4500.0);
            // A fill alternates all through (the bit sync of a FLEX frame is only the start of its sync part)
            bool fillBlock = (block.m_deviation == 4800.0) && (block.m_levels.size() >= 2);
            for (size_t k = 0; fillBlock && (k < block.m_levels.size()); k++) {
                fillBlock = ((k & 1) ? (block.m_levels[k] < 0.0f) : (block.m_levels[k] > 0.0f));
            }

            // After a pair (switch pattern and data) the time of the data is counted too
            if ((pocsagBlock && (block.m_baud != 750.0)) || fillBlock)
            {
                int64_t counted = (int64_t) (stream.currentFrame() - first) * F - stream.ticksToBoundary();
                consistent = consistent && (counted == total);
                checked++;
            }
        }

        char what[160];
        std::snprintf(what, sizeof(what), "Mixed %d bps: the time of the blocks is the time counted (%d POCSAG and fill blocks)", baud, checked);
        check(consistent && (checked > 20), what);
    }

    // A message longer than the longest gap, or a plan with no time for POCSAG, is refused
    {
        RadioPagingMixTxStream::Config c = mixConfig();
        c.m_collapse = 4;
        RadioPagingMixTxStream longGap(c);
        std::string text(1900, 'A');
        std::string e1, e2, e3;
        bool tooLong = !longGap.addPocsag(pocsag(100, text.c_str()), 512, false, 1, e1) && !e1.empty();
        bool fits = longGap.addPocsag(pocsag(100, "SHORT"), 512, false, 2, e2);

        c.m_collapse = 5;       // 32 frames a minute: nothing left
        RadioPagingMixTxStream noTime(c);
        bool none = !noTime.addPocsag(pocsag(100, "NO TIME"), 1200, false, 3, e3) && !e3.empty();
        check(tooLong && fits && none, "Mixed: a message longer than the longest gap, or a plan with no time for POCSAG, is refused");
    }

    // A group refilled at every run does not hold the others across the gaps
    {
        RadioPagingMixTxStream stream(mixConfig());
        stream.begin(0);
        stream.next(block);
        stream.addPocsag(pocsag(7654, "WAITING"), 512, false, 600, error);
        bool sent = false;

        for (int i = 0; (i < 120) && !sent; i++)
        {
            stream.addPocsag(pocsag(1000 + i, "BUSY"), 1200, false, 700 + i, error);
            stream.next(block);
            sent = !block.m_ids.empty() && (block.m_ids[0] == 600);
        }

        check(sent, "Mixed: a group refilled at every run gives way to the one waiting");
    }

    // 7.1.2: a capcode of the class 2 or 3 is refused too (frames 2 and 3 are sent, the pager would
    // see one frame in 4 minutes)
    {
        RadioPagingMixTxStream::Config c = mixConfig();
        c.m_framesPerMinute = 2;
        RadioPagingMixTxStream stream(c);
        std::string e1, e2;
        bool class2 = !stream.addFlex(flex(16 * 2 + 1, "NO"), false, 1, e1) && !e1.empty();
        bool class1 = stream.addFlex(flex(16 * 1 + 1, "YES"), false, 2, e2);
        check(class2 && class1, "Mixed 7.1.2: a capcode of the frames 2 or 3 of the minute is refused with 2 frames a minute");
    }

    // Clear behind the switch pattern: the message is returned, its time kept as fill
    {
        RadioPagingMixTxStream stream(mixConfig());
        stream.begin(0);
        stream.next(block);
        uint64_t first = stream.currentFrame();
        stream.addPocsag(pocsag(100, "CLEARED"), 1200, false, 99, error);
        int64_t total = 0;
        bool gotSwitch = false;

        for (int i = 0; (i < 12) && !gotSwitch; i++)
        {
            stream.next(block);
            total += block.ticks();
            gotSwitch = (block.m_baud == 750.0);
        }

        std::vector<uint64_t> cleared = stream.clear();
        stream.next(block);
        total += block.ticks();
        bool filled = block.m_ids.empty() && (block.m_deviation == 4800.0);
        stream.next(block);     // The fill to the boundary, or the FLEX frame
        total += block.ticks();
        check(gotSwitch && (cleared.size() == 1) && (cleared[0] == 99) && filled && (stream.pending() == 0),
            "Mixed: clear behind the switch pattern returns the message, its time is filled");
    }

    // Cut: the POCSAG block behind the switch pattern is returned, not lost
    {
        RadioPagingMixTxStream stream(mixConfig());
        stream.begin(0);
        stream.next(block);
        stream.addPocsag(pocsag(100, "CUT"), 1200, false, 95, error);
        bool gotSwitch = false;
        for (int i = 0; (i < 12) && !gotSwitch; i++)
        {
            stream.next(block);
            gotSwitch = (block.m_baud == 750.0);
        }
        std::vector<uint64_t> cut = stream.cut();
        check(gotSwitch && (cut.size() == 1) && (cut[0] == 95) && (stream.pending() == 0), "Mixed: cut after the switch pattern returns the POCSAG message");
    }
}

int main()
{
    testMixed();
    testPocsagNetwork();
    testPocsagNetworkTurns();
    testPocsagOneShot();
    testFlexPlan();
    testFlexErs();
    testFlexOneShot();
    testFlexNetwork();
    testFlexCut();

    std::printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
