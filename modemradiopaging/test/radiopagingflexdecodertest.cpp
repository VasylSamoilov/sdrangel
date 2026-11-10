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

// Self test of the FLEX frame decoder. Frames of the transmitter of this directory are decoded and
// compared with the messages sent, frames of a reference transmitter (radiopagingflexdecoderdata.inc) are
// compared with the messages sent and with the messages a reference receiver decoded from them. Bit errors,
// erased codewords, lost fragments, duplicates and repeated subframes are tested with modified frames.
// Prints one line per check and returns non-zero if any check fails.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "radiopagingbch.h"
#include "radiopagingflex.h"
#include "radiopagingflexdecoder.h"
#include "radiopagingflexscheduler.h"

namespace {

int failures = 0;
int passes = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        failures++;
    }
}

typedef RadioPagingFlex Flex;
typedef RadioPagingFlexDecoder Dec;
typedef RadioPagingFlexEncoder Enc;

struct Exp
{
    unsigned long long m_cap;
    const char *m_type;
    const char *m_text;
    int m_number;
    int m_complete;
    int m_frags;
};

struct RefFrame
{
    const char *m_scenario;
    const char *m_name;
    int m_mode;
    int m_inverted;
    int m_cycle;
    int m_frame;
    const char *m_sync;
    const char *m_data;
    int m_nexp;
    Exp m_exp[16];
    int m_nref;
    const char *m_ref[64];
};

const RefFrame refFrames[] = {
#include "radiopagingflexdecoderdata.inc"
};

const size_t refFrameCount = sizeof(refFrames) / sizeof(refFrames[0]);

std::vector<uint8_t> fromHex(const char *hex)
{
    std::vector<uint8_t> out;

    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2)
    {
        unsigned v = 0;
        std::sscanf(hex + i, "%2x", &v);
        out.push_back((uint8_t) v);
    }

    return out;
}

// Slices the data part of a frame (second sync and phases) into the phases. This is the inverse of
// RadioPagingFlex::assemble
Dec::RxFrame toRxFrame(Flex::Mode mode, bool inverted, const std::vector<uint8_t>& sync, const std::vector<uint8_t>& data)
{
    static const int s2Bytes[4] = {5, 10, 10, 20};
    Dec::RxFrame frame;
    int phases = Flex::phaseCount(mode);
    size_t bit = 0;

    frame.m_mode = mode;
    frame.m_inverted = inverted;
    frame.m_hasFiw = (sync.size() >= 18);

    if (frame.m_hasFiw) {
        frame.m_fiw = ((uint32_t) sync[14] << 24) | ((uint32_t) sync[15] << 16) | ((uint32_t) sync[16] << 8) | sync[17];
    }

    frame.m_phases.resize(phases);

    for (int p = 0; p < phases; p++)
    {
        frame.m_phases[p].m_name = (phases == 1) ? 'A' : (phases == 2) ? (p ? 'C' : 'A') : (char) ('A' + p);
        frame.m_phases[p].m_words.assign(Flex::m_wordsPerFrame, 0);
    }

    size_t start = (size_t) s2Bytes[(int) mode] * 8;

    for (int w = 0; w < Flex::m_wordsPerFrame; w++)
    {
        for (int b = 31; b >= 0; b--)
        {
            for (int p = 0; p < phases; p++, bit++)
            {
                size_t pos = start + bit;

                if ((pos / 8 < data.size()) && ((data[pos / 8] >> (7 - pos % 8)) & 1)) {
                    frame.m_phases[p].m_words[w] |= 1u << b;
                }
            }
        }
    }

    return frame;
}

// Helpers working on deinterleaved codewords of a phase
std::vector<uint32_t> deinterleaved(const std::vector<uint32_t>& words)
{
    std::vector<uint32_t> out = words;

    for (int b = 0; b < Flex::m_blocksPerFrame; b++) {
        Flex::deinterleaveBlock(&out[b * Flex::m_wordsPerBlock]);
    }

    return out;
}

std::vector<uint32_t> interleaved(const std::vector<uint32_t>& words)
{
    std::vector<uint32_t> out = words;

    for (int b = 0; b < Flex::m_blocksPerFrame; b++) {
        Flex::interleaveBlock(&out[b * Flex::m_wordsPerBlock]);
    }

    return out;
}

void editWords(Dec::RxPhase& phase, const std::vector<std::pair<int, uint32_t>>& xors)
{
    std::vector<uint32_t> words = deinterleaved(phase.m_words);

    for (const auto& x : xors) {
        words[x.first] ^= x.second;
    }

    phase.m_words = interleaved(words);
}

std::vector<uint32_t> codewords(const Dec::RxPhase& phase)
{
    return deinterleaved(phase.m_words);
}

std::string numericText(const std::string& text)
{
    std::string out;

    for (char c : text)
    {
        if (c == '_') {
            out += '-';
        } else if (c == 'u') {
            out += 'U';
        } else {
            out += c;
        }
    }

    return out;
}

// Same message in the text of the reference receiver
std::string refRecord(const Dec::Message& m, bool& comparable)
{
    const char *tag = nullptr;
    std::string text = m.m_text;

    comparable = true;

    switch (m.m_type)
    {
    case Dec::TypeAlphanumeric: tag = "ALN"; break;
    case Dec::TypeSecure: tag = "SEC"; break;
    case Dec::TypeNumeric: tag = "NUM"; break;
    case Dec::TypeSpecialNumeric: tag = "SNUM"; break;
    case Dec::TypeNumberedNumeric: tag = "NNUM"; break;
    case Dec::TypeNumberedSpecialNumeric: tag = "NSNUM"; break;
    case Dec::TypeTone: tag = "TON"; break;
    case Dec::TypeShortMessage:
        tag = "SMSG";
        comparable = (m.m_shortType == 0);
        break;
    case Dec::TypeBinary:
        tag = "HEX";
        comparable = !m.m_fragmented;

        if (!text.empty())
        {
            char last = text.back();
            int nib = (last >= 'A') ? (last - 'A' + 10) : (last - '0');
            char fill = ((nib >> 3) & 1) ? '0' : 'F';

            text.append((text.size() % 2 == 0) ? 2 : 1, fill);
        }
        break;
    default:
        comparable = false;
        return "";
    }

    char head[32];
    std::snprintf(head, sizeof(head), "%010llu ", (unsigned long long) m.m_capcode);
    return std::string(head) + tag + "|" + text;
}

// Unused digit positions of the last word of a numeric message are spaces
std::string trimNumeric(const Dec::Message& m)
{
    std::string text = m.m_text;

    if ((m.m_type == Dec::TypeNumeric) || (m.m_type == Dec::TypeSpecialNumeric) || (m.m_type == Dec::TypeNumberedNumeric)
        || (m.m_type == Dec::TypeNumberedSpecialNumeric))
    {
        while (!text.empty() && (text.back() == ' ')) {
            text.pop_back();
        }
    }

    return text;
}

bool matches(const Dec::Message& m, const Exp& e)
{
    if ((m.m_capcode != e.m_cap) || (m.m_typeName != e.m_type) || (trimNumeric(m) != e.m_text)) {
        return false;
    }
    if ((e.m_number != -2) && (m.m_messageNumber != e.m_number)) {
        return false;
    }
    if (m.m_complete != (e.m_complete != 0)) {
        return false;
    }
    return (e.m_frags == 0) || (m.m_fragmentCount == e.m_frags);
}

std::string describe(const Dec::Message& m)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%llu/%s/n=%d/c=%d/f=%d/", (unsigned long long) m.m_capcode, m.m_typeName.c_str(),
        m.m_messageNumber, m.m_complete ? 1 : 0, m.m_fragmentCount);
    return std::string(buf) + m.m_text;
}

// ---------------------------------------------------------------------------
// Frames of the reference transmitter
// ---------------------------------------------------------------------------

void testReferenceFrames()
{
    Dec decoder;
    std::string scenario;
    int frames = 0;
    int expectOk = 0;
    int refOk = 0;
    int refCompared = 0;
    int refDifferent = 0;
    std::map<std::string, std::pair<int, int>> perScenario;

    for (size_t i = 0; i < refFrameCount; i++)
    {
        const RefFrame& f = refFrames[i];

        if (scenario != f.m_scenario)
        {
            scenario = f.m_scenario;
            decoder.reset();
            decoder.setReferenceYear(2026);
        }

        Dec::RxFrame rx = toRxFrame((Flex::Mode) f.m_mode, f.m_inverted != 0, fromHex(f.m_sync), fromHex(f.m_data));
        std::vector<Dec::Message> out = decoder.decodeFrame(rx);
        frames++;

        // The reference lists have no retransmissions: duplicates are output, marked, and left out here
        out.erase(std::remove_if(out.begin(), out.end(), [](const Dec::Message& m) { return m.m_duplicate; }), out.end());

        // Messages sent
        bool ok = (out.size() == (size_t) f.m_nexp);
        // Incomplete messages are not reported by the reference receiver
        std::vector<Dec::Message> outComplete;

        for (const Dec::Message& m : out)
        {
            if (m.m_complete) {
                outComplete.push_back(m);
            }
        }
        std::vector<bool> used(out.size(), false);

        for (int e = 0; e < f.m_nexp; e++)
        {
            bool found = false;

            for (size_t k = 0; k < out.size(); k++)
            {
                if (!used[k] && matches(out[k], f.m_exp[e]))
                {
                    used[k] = true;
                    found = true;
                    break;
                }
            }

            if (!found) {
                std::printf("   no match for expected message %d\n", e);
            }

            ok = ok && found;
        }

        if (ok) {
            expectOk++;
        } else {
            std::printf("FAIL: sent messages %s / %s: expected %d got %zu\n", f.m_scenario, f.m_name, f.m_nexp, out.size());

            for (int e = 0; e < f.m_nexp; e++)
            {
                std::printf("   expected %llu/%s/n=%d/c=%d/f=%d/%s\n", f.m_exp[e].m_cap, f.m_exp[e].m_type, f.m_exp[e].m_number,
                    f.m_exp[e].m_complete, f.m_exp[e].m_frags, f.m_exp[e].m_text);
            }
            for (const Dec::Message& m : out) {
                std::printf("   got      %s\n", describe(m).c_str());
            }
        }

        perScenario[f.m_scenario].first += ok ? 1 : 0;
        perScenario[f.m_scenario].second++;

        // Messages of the reference receiver
        std::vector<std::string> mine;

        for (const Dec::Message& m : outComplete)
        {
            bool comparable;
            std::string rec = refRecord(m, comparable);

            if (comparable) {
                mine.push_back(rec);
            }
        }

        std::vector<std::string> theirs;

        for (int r = 0; r < f.m_nref; r++)
        {
            // Hex messages in fragments are not compared (the reference pads each fragment)
            std::string rec = f.m_ref[r];

            if ((rec.find("|") != std::string::npos) && (rec.find(" HEX|") != std::string::npos) && (std::string(f.m_scenario) == "fragments")) {
                continue;
            }

            theirs.push_back(rec);
        }

        std::sort(mine.begin(), mine.end());
        std::sort(theirs.begin(), theirs.end());
        refCompared++;

        if (mine == theirs)
        {
            refOk++;
        }
        else
        {
            refDifferent++;
            std::printf("FAIL: reference receiver %s / %s\n", f.m_scenario, f.m_name);

            for (const std::string& s : theirs) {
                std::printf("   reference %s\n", s.c_str());
            }
            for (const std::string& s : mine) {
                std::printf("   decoder   %s\n", s.c_str());
            }
        }
    }

    for (const auto& s : perScenario)
    {
        std::string what = "reference frames, messages sent: scenario " + s.first;
        char buf[64];

        std::snprintf(buf, sizeof(buf), " (%d frames)", s.second.second);
        check(s.second.first == s.second.second, (what + buf).c_str());
    }

    char buf[128];

    std::snprintf(buf, sizeof(buf), "reference frames: %d of %d match the messages sent", expectOk, frames);
    check(expectOk == frames, buf);
    std::snprintf(buf, sizeof(buf), "reference frames: %d of %d match the reference receiver", refOk, refCompared);
    check(refDifferent == 0, buf);
}

// Network time and identification from the frames of the scenario "biw"
void testBiw()
{
    Dec decoder;
    bool any = false;

    decoder.setReferenceYear(2026);

    for (size_t i = 0; i < refFrameCount; i++)
    {
        const RefFrame& f = refFrames[i];

        if (std::string(f.m_scenario) != "biw") {
            continue;
        }

        any = true;
        decoder.decodeFrame(toRxFrame((Flex::Mode) f.m_mode, false, fromHex(f.m_sync), fromHex(f.m_data)));

        if (std::string(f.m_name) == "biw 0")
        {
            const Dec::FrameReport& r = decoder.lastFrame();
            check(r.m_fiwValid && (r.m_cycle == 3) && (r.m_frame == 8), "biw: frame information word");
            check(!r.m_blocks.empty() && r.m_blocks[0].m_valid && (r.m_blocks[0].m_extra.size() == 3),
                "biw: three extra block information words");

            if (!r.m_blocks.empty() && (r.m_blocks[0].m_extra.size() == 3))
            {
                const Dec::ExtraBiw& d = r.m_blocks[0].m_extra[0];
                const Dec::ExtraBiw& t = r.m_blocks[0].m_extra[1];
                const Dec::ExtraBiw& s = r.m_blocks[0].m_extra[2];

                check((d.m_kind == Dec::ExtraBiw::Date) && d.m_dateValid && (d.m_year == 2026) && (d.m_month == 11) && (d.m_day == 9),
                    "biw: date word 2026-11-09");
                check((t.m_kind == Dec::ExtraBiw::Time) && t.m_timeValid && (t.m_hour == 14) && (t.m_minute == 13) && (t.m_second7_5 == 2),
                    "biw: time word 14:13:15");
                check((s.m_kind == Dec::ExtraBiw::Ssid1) && (s.m_localId == 300) && (s.m_coverageZone == 17), "biw: SSID1 word");
            }
        }
    }

    Dec::NetworkTime t = decoder.networkTime();

    check(any, "biw: scenario present");
    check(t.m_dateValid && (t.m_year == 2026) && (t.m_month == 11) && (t.m_day == 9), "network time: date confirmed after repeated reception");
    check(t.m_timeValid && (t.m_hour == 14) && (t.m_minute == 13) && (t.m_second7_5 == 2), "network time: time confirmed");
    check(t.m_zoneValid && (t.m_zone == 2) && (t.m_zoneMinutes == 120) && (t.m_dst == 1) && (t.m_extendedSeconds == 1),
        "network time: time zone confirmed");
    check(t.m_ssid1Valid && (t.m_localId == 300) && (t.m_coverageZone == 17), "network identification: SSID1");
    check(t.m_ssid2Valid && (t.m_country == 262) && (t.m_tmf == 5), "network identification: SSID2");

    Dec::NetworkTime other = decoder.networkTime(true);
    check(!other.m_dateValid && !other.m_timeValid && !other.m_ssid1Valid, "network time: inverted polarity is a separate network");

    // Time is valid only after repeated reception
    Dec once;

    for (size_t i = 0; i < refFrameCount; i++)
    {
        const RefFrame& f = refFrames[i];

        if ((std::string(f.m_scenario) == "biw") && (std::string(f.m_name) == "biw 0")) {
            once.decodeFrame(toRxFrame((Flex::Mode) f.m_mode, false, fromHex(f.m_sync), fromHex(f.m_data)));
        }
    }

    check(!once.networkTime().m_dateValid && !once.networkTime().m_timeValid, "network time: one reception is not enough");
}

// ---------------------------------------------------------------------------
// Frames of the transmitter of this directory
// ---------------------------------------------------------------------------

struct Sent
{
    uint64_t m_capcode;
    Flex::MessageType m_type;
    std::string m_text;
};

std::string typeName(Flex::MessageType type)
{
    return (type == Flex::Tone) ? "tone" : (type == Flex::Numeric) ? "numeric" : "alphanumeric";
}

std::vector<Dec::Message> runEncoder(Enc& enc, Dec& dec, std::vector<Dec::RxFrame> *framesOut = nullptr, int *frameCount = nullptr)
{
    std::vector<Dec::Message> all;
    int frames = 0;

    for (uint64_t index = 0; (index < 4000) && (enc.pending() > 0); index++)
    {
        Enc::Frame f = enc.buildFrame(index);

        if (f.m_reserved) {
            continue;
        }

        Dec::RxFrame rx = toRxFrame(f.m_mode, f.m_inverted, f.m_bytes.m_sync, f.m_bytes.m_data);
        std::vector<Dec::Message> out = dec.decodeFrame(rx);

        all.insert(all.end(), out.begin(), out.end());
        frames++;

        if (framesOut) {
            framesOut->push_back(rx);
        }
    }

    if (frameCount) {
        *frameCount = frames;
    }

    return all;
}

void testRoundTrip()
{
    std::mt19937 rng(12345);
    const Flex::Mode modes[4] = {Flex::Mode1600_2, Flex::Mode3200_2, Flex::Mode3200_4, Flex::Mode6400_4};
    int total = 0;
    int bad = 0;

    for (int m = 0; m < 4; m++)
    {
        for (int inv = 0; inv < 2; inv++)
        {
            for (int collapse = 0; collapse <= 7; collapse += 7)
            {
                Enc enc;
                Dec dec;
                std::string err;
                std::vector<Sent> sent;

                enc.setMode(modes[m]);
                enc.setInverted(inv != 0);
                enc.setCollapse(collapse, err);

                for (int i = 0; i < 40; i++)
                {
                    Enc::TxMessage tx;
                    int kind = rng() % 3;
                    bool isLong = (rng() % 4) == 0;

                    tx.m_capcode = isLong ? (2101249ULL + rng() % 1000000000ULL) : (1 + rng() % 1933311);
                    tx.m_priority = (rng() % 5) == 0;

                    if (kind == 0)
                    {
                        tx.m_type = Flex::Tone;
                    }
                    else if (kind == 1)
                    {
                        tx.m_type = Flex::Numeric;
                        int len = 1 + rng() % 41;

                        for (int c = 0; c < len; c++) {
                            tx.m_text += "0123456789 -[]U."[rng() % 15];
                        }
                    }
                    else
                    {
                        tx.m_type = Flex::Alphanumeric;
                        int len = 1 + rng() % ((i % 7 == 0) ? 248 : 60);

                        for (int c = 0; c < len; c++) {
                            tx.m_text += (char) (0x20 + rng() % 0x5f);
                        }
                    }

                    Enc::EnqueueResult r = enc.enqueue(tx);

                    if (r.m_ok) {
                        sent.push_back({tx.m_capcode, tx.m_type, tx.m_text});
                    }
                }

                std::vector<Dec::Message> got = runEncoder(enc, dec);
                std::vector<std::string> a;
                std::vector<std::string> b;

                for (const Sent& s : sent)
                {
                    std::string text = (s.m_type == Flex::Numeric) ? numericText(s.m_text) : (s.m_type == Flex::Tone) ? "" : s.m_text;
                    while ((s.m_type == Flex::Numeric) && !text.empty() && (text.back() == ' ')) {
                        text.pop_back();
                    }

                    a.push_back(std::to_string(s.m_capcode) + "/" + typeName(s.m_type) + "/" + text);
                }
                for (const Dec::Message& g : got)
                {
                    bool good = g.m_complete && (g.m_wordsUncorrectable == 0) && (g.m_wordsCorrected == 0);
                    b.push_back(std::to_string(g.m_capcode) + "/" + g.m_typeName + "/" + trimNumeric(g));

                    if (!good) {
                        bad++;
                    }
                }

                std::sort(a.begin(), a.end());
                std::sort(b.begin(), b.end());
                total++;

                if (a != b)
                {
                    int shown = 0;

                    for (const std::string& s : a)
                    {
                        if (!std::binary_search(b.begin(), b.end(), s) && (shown++ < 3)) {
                            std::printf("   sent but not decoded: %s (%zu)\n", s.c_str(), s.size());
                        }
                    }
                    for (const std::string& s : b)
                    {
                        if (!std::binary_search(a.begin(), a.end(), s) && (shown++ < 6)) {
                            std::printf("   decoded but not sent: %s (%zu)\n", s.c_str(), s.size());
                        }
                    }
                }

                char buf[128];
                std::snprintf(buf, sizeof(buf), "round trip %s %s collapse %d: %zu messages", Flex::modeName(modes[m]),
                    inv ? "inverted" : "normal", collapse, sent.size());
                check(a == b, buf);
            }
        }
    }

    check(bad == 0, "round trip: no word errors reported on clean frames");
    (void) total;

    // Idle frames and collapse
    Enc enc;
    Dec dec;
    Enc::Frame f = enc.buildFrame(5);
    std::vector<Dec::Message> out = dec.decodeFrame(toRxFrame(f.m_mode, f.m_inverted, f.m_bytes.m_sync, f.m_bytes.m_data));

    check(f.m_idle && out.empty() && dec.lastFrame().m_fiwValid && dec.lastFrame().m_blocks.size() == 1 && dec.lastFrame().m_blocks[0].m_idle,
        "idle frame: no messages, block information word seen");

    std::string err;
    enc.setCollapse(5, err);
    out = dec.decodeFrame(toRxFrame(f.m_mode, f.m_inverted, f.m_bytes.m_sync, f.m_bytes.m_data));
    Enc::Frame g = enc.buildFrame(6);
    dec.decodeFrame(toRxFrame(g.m_mode, g.m_inverted, g.m_bytes.m_sync, g.m_bytes.m_data));
    check(dec.lastFrame().m_blocks[0].m_collapse == 5 && dec.lastFrame().m_cycle == 0 && dec.lastFrame().m_frame == 6,
        "collapse and frame number from block and frame information words");
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

Dec::RxFrame encoderFrame(Enc& enc, const std::vector<Enc::TxMessage>& msgs, uint64_t index = 0)
{
    for (const Enc::TxMessage& m : msgs) {
        enc.enqueue(m);
    }

    for (; index < 4000; index++)
    {
        Enc::Frame f = enc.buildFrame(index);

        if (!f.m_idle && !f.m_reserved) {
            return toRxFrame(f.m_mode, f.m_inverted, f.m_bytes.m_sync, f.m_bytes.m_data);
        }
    }

    return Dec::RxFrame();
}

void testErrors()
{
    Enc enc;
    Enc::TxMessage tx;

    tx.m_capcode = 4321;
    tx.m_type = Flex::Alphanumeric;
    tx.m_text = "Bit errors in every codeword of this message";

    Enc::TxMessage tx2;

    tx2.m_capcode = 4322;
    tx2.m_type = Flex::Numeric;
    tx2.m_text = "0123456789";

    Dec::RxFrame clean = encoderFrame(enc, {tx, tx2});
    std::vector<uint32_t> cw = codewords(clean.m_phases[0]);

    // Words in use: until the last word that is not idle
    int used = 0;
    {
        Dec d;
        std::vector<Dec::Message> out = d.decodeFrame(clean);
        check(out.size() == 2, "errors: clean frame decodes");
        const Dec::BlockInfo& b = d.lastFrame().m_blocks[0];
        used = (int) b.m_vectorStart + 8;
    }

    std::mt19937 rng(777);

    // One and two bit errors in every codeword of the used part (bits 1-31)
    for (int errors = 1; errors <= 2; errors++)
    {
        Dec::RxFrame bad = clean;
        std::vector<std::pair<int, uint32_t>> xors;

        for (int w = 0; w < used; w++)
        {
            uint32_t mask = 0;

            while (__builtin_popcount(mask) < errors) {
                mask |= 1u << (1 + rng() % 31);
            }

            xors.push_back({w, mask});
        }

        editWords(bad.m_phases[0], xors);

        Dec d;
        std::vector<Dec::Message> out = d.decodeFrame(bad);
        bool ok = (out.size() == 2);
        int corrected = 0;

        for (const Dec::Message& m : out)
        {
            ok = ok && m.m_complete && (m.m_wordsUncorrectable == 0) && (m.m_checksum == Dec::CheckOk);
            corrected += m.m_wordsCorrected;

            if (m.m_typeName == "alphanumeric") {
                ok = ok && (m.m_text == tx.m_text) && (m.m_signature == Dec::CheckOk);
            } else {
                ok = ok && (m.m_text == tx2.m_text);
            }
        }

        char buf[96];
        std::snprintf(buf, sizeof(buf), "errors: %d bit error(s) in every codeword corrected (%d words in the messages)", errors, corrected);
        check(ok && (corrected > 0) && (d.status().m_codewordsCorrected >= (uint32_t) used), buf);
    }

    // Wrong parity bit alone
    {
        Dec::RxFrame bad = clean;
        std::vector<std::pair<int, uint32_t>> xors;

        for (int w = 0; w < used; w++) {
            xors.push_back({w, 1u});
        }

        editWords(bad.m_phases[0], xors);

        Dec d;
        std::vector<Dec::Message> out = d.decodeFrame(bad);
        bool ok = (out.size() == 2);

        for (const Dec::Message& m : out) {
            ok = ok && (m.m_wordsUncorrectable == 0) && (m.m_parityCorrections == m.m_wordsCorrected) && (m.m_wordsCorrected > 0);
        }

        check(ok, "errors: wrong parity bit alone is corrected and counted as parity");
    }

    // Three bit errors in one word: not corrected, the text shows lost characters
    {
        Dec::RxFrame bad = clean;
        int target = -1;
        const Dec::BlockInfo *blk;
        Dec probe;

        probe.decodeFrame(clean);
        blk = &probe.lastFrame().m_blocks[0];

        // Message field word: first word at or after the end of the vector field
        target = (int) blk->m_vectorStart + 2 + 1;

        // A pattern of bit errors that the code does not correct
        uint32_t mask = 0x00000e00u;

        while (true)
        {
            RadioPagingBCH::Result r = RadioPagingBCH::decode(cw[target] ^ mask);

            if ((r.m_bitErrors < 0) || ((r.m_bitErrors > 0) && r.m_parityError)) {
                break;
            }

            mask += 0x00010400u;
        }

        Dec::RxFrame bad2 = clean;
        editWords(bad2.m_phases[0], {{target, mask}});

        Dec d;
        std::vector<Dec::Message> out = d.decodeFrame(bad2);
        bool found = false;

        for (const Dec::Message& m : out)
        {
            if (m.m_typeName == "alphanumeric")
            {
                found = true;
                check((m.m_wordsUncorrectable == 1) && (m.m_text.find('?') != std::string::npos) && (m.m_signature != Dec::CheckOk)
                    && (m.m_checksum == Dec::CheckIncomplete) && (m.m_wordMap.find('X') != std::string::npos),
                    "errors: uncorrectable word reported, lost characters marked");
            }
        }

        check(found && (d.status().m_codewordsUncorrectable == 1), "errors: uncorrectable counter");

        // Erased codewords
        Dec::RxFrame erased = clean;
        erased.m_phases[0].m_erased.assign(Flex::m_wordsPerFrame, 0);
        erased.m_phases[0].m_erased[target] = 1;

        Dec e;
        out = e.decodeFrame(erased);
        found = false;

        for (const Dec::Message& m : out)
        {
            if (m.m_typeName == "alphanumeric") {
                found = (m.m_wordsUncorrectable == 1) && (m.m_text.find('?') != std::string::npos);
            }
        }

        check(found && (e.status().m_codewordsErased == 1), "errors: erased codeword handled like a lost one");

        // Erased address word: the message is lost, the others stay
        Dec::RxFrame addr = clean;
        addr.m_phases[0].m_erased.assign(Flex::m_wordsPerFrame, 0);
        addr.m_phases[0].m_erased[1] = 1;

        Dec a;
        out = a.decodeFrame(addr);
        check(out.size() == 1 && out[0].m_capcode == 4322, "errors: lost address word loses only that message");

        // Lost block information word
        Dec::RxFrame biw = clean;
        biw.m_phases[0].m_erased.assign(Flex::m_wordsPerFrame, 0);
        biw.m_phases[0].m_erased[0] = 1;

        Dec b;
        out = b.decodeFrame(biw);
        check(out.empty() && !b.lastFrame().m_blocks[0].m_valid, "errors: lost first block information word, no messages");
    }

    // Frame information word
    {
        Dec::RxFrame bad = clean;
        Dec d;

        bad.m_fiw ^= 0x00000600u;
        check(d.decodeFrame(bad).size() == 2 && d.lastFrame().m_fiwValid, "errors: two bit errors in the frame information word corrected");

        bad.m_fiw = clean.m_fiw ^ 0x0420a000u;
        check(d.decodeFrame(bad).empty() && (d.status().m_fiwFailures == 1), "errors: bad frame information word, frame dropped");

        // Valid codeword with a wrong checksum
        bad.m_fiw = Flex::encodeWord(0x12340);
        check(d.decodeFrame(bad).empty() && (d.status().m_fiwFailures == 2), "errors: frame information word checksum");

        Dec::RxFrame noFiw = clean;
        noFiw.m_hasFiw = false;
        noFiw.m_cycle = 14;
        noFiw.m_frame = 127;
        check(d.decodeFrame(noFiw).size() == 2 && d.lastFrame().m_cycle == 14 && d.lastFrame().m_frame == 127,
            "frame number given without frame information word");
    }
}

// Fragments, duplicates and timeouts with the frames of the reference transmitter
const RefFrame *findFrame(const char *scenario, const char *name)
{
    for (size_t i = 0; i < refFrameCount; i++)
    {
        if (!std::strcmp(refFrames[i].m_scenario, scenario) && !std::strcmp(refFrames[i].m_name, name)) {
            return &refFrames[i];
        }
    }

    return nullptr;
}

Dec::RxFrame rxOf(const char *scenario, const char *name, int cycleShift = 0)
{
    const RefFrame *f = findFrame(scenario, name);
    Dec::RxFrame rx = toRxFrame((Flex::Mode) f->m_mode, false, std::vector<uint8_t>(), fromHex(f->m_data));

    rx.m_hasFiw = false;
    rx.m_cycle = (f->m_cycle + cycleShift) % 15;
    rx.m_frame = f->m_frame;
    return rx;
}

void testFragments()
{
    // Lost middle fragment: gap detected, signature fails
    {
        Dec d;
        d.decodeFrame(rxOf("fragments", "fragment 0"));
        d.decodeFrame(rxOf("fragments", "fragment 1"));
        std::vector<Dec::Message> out = d.decodeFrame(rxOf("fragments", "fragment 2"));
        check(out.size() == 1 && out[0].m_complete && (out[0].m_fragmentCount == 3) && (out[0].m_signature == Dec::CheckOk)
            && (out[0].m_fragmented) && (out[0].m_fragment == Dec::FragmentLast) && (out[0].m_messageNumber == 9),
            "fragments: three fragments reassembled, signature of the whole message ok");
    }
    {
        Dec d;
        d.decodeFrame(rxOf("fragments", "fragment 0"));
        std::vector<Dec::Message> out = d.decodeFrame(rxOf("fragments", "fragment 2"));
        check(out.size() == 1 && !out[0].m_complete && out[0].m_fragmentGap && (out[0].m_fragmentCount == 2) && (out[0].m_signature == Dec::CheckFailed),
            "fragments: lost fragment shows as gap, message incomplete");
    }
    {
        Dec d;
        d.decodeFrame(rxOf("fragments", "fragment 0"));
        d.decodeFrame(rxOf("fragments", "fragment 1"));
        std::vector<Dec::Message> out = d.flush();
        check(out.size() == 1 && !out[0].m_complete && (out[0].m_fragmentCount == 2) && !out[0].m_text.empty(),
            "fragments: flush returns the partial message marked incomplete");
        check(d.flush().empty(), "fragments: flush twice returns nothing");
    }
    {
        Dec d;
        d.decodeFrame(rxOf("fragments", "fragment 0"));
        // 100 frames later (another cycle): timeout
        Dec::RxFrame later = rxOf("fragments", "long fragment 0");
        later.m_frame = 120;
        std::vector<Dec::Message> out = d.decodeFrame(later);
        check(out.size() == 1 && !out[0].m_complete && (out[0].m_capcode == 2500), "fragments: timeout releases the partial message");
    }
    {
        Dec d;
        std::vector<Dec::Message> out = d.decodeFrame(rxOf("fragments", "fragment 2"));
        check(out.size() == 1 && !out[0].m_complete && out[0].m_fragmented, "fragments: last fragment without the first is an incomplete message");
    }
    {
        Dec d;
        d.setReportFragments(true);
        d.decodeFrame(rxOf("fragments", "fragment 0"));
        std::vector<Dec::Message> a = d.decodeFrame(rxOf("fragments", "fragment 1"));
        std::vector<Dec::Message> b = d.decodeFrame(rxOf("fragments", "fragment 2"));
        check(a.size() == 1 && a[0].m_isFragment && b.size() == 2 && b[0].m_isFragment && !b[1].m_isFragment, "fragments: report fragments option");
    }
    {
        // A complete message of an address with an open stream abandons the stream (reported incomplete)
        Dec d;
        d.decodeFrame(rxOf("fragments", "open stream"));
        std::vector<Dec::Message> out = d.decodeFrame(rxOf("fragments", "abandon"));
        check(out.size() == 2 && !out[0].m_complete && out[1].m_complete && (out[1].m_text == "complete message"),
            "fragments: new message abandons an open stream");
    }
}

void testDuplicates()
{
    {
        Dec d;
        d.decodeFrame(rxOf("duplicates", "original"));
        std::vector<Dec::Message> out = d.decodeFrame(rxOf("duplicates", "retransmission"));
        bool marked = (out.size() == 3);
        for (const Dec::Message& m : out) {
            marked = marked && m.m_duplicate && !m.m_recovered;
        }
        check(marked && (d.status().m_duplicates == 3), "duplicates: retransmission (R = 0) of complete messages output again, marked duplicate");
        out = d.decodeFrame(rxOf("duplicates", "same number other message"));
        check(out.size() == 1, "duplicates: same message number with other content is a new message");
    }
    {
        // First copy with a lost word, retransmission repairs it
        Dec::RxFrame first = rxOf("duplicates", "original");
        Dec probe;
        probe.decodeFrame(first);
        std::vector<Dec::Message> clean;
        Dec::RxFrame bad = first;
        bad.m_phases[0].m_erased.assign(Flex::m_wordsPerFrame, 0);

        int body = (int) probe.lastFrame().m_blocks[0].m_vectorStart + 3 + 2;

        bad.m_phases[0].m_erased[body] = 1;

        Dec d;
        std::vector<Dec::Message> a = d.decodeFrame(bad);
        bool lost = false;

        for (const Dec::Message& m : a) {
            lost = lost || (m.m_wordsUncorrectable > 0);
        }

        std::vector<Dec::Message> b = d.decodeFrame(rxOf("duplicates", "retransmission"));
        bool repaired = false;

        for (const Dec::Message& m : b) {
            repaired = repaired || (m.m_recovered && (m.m_wordsUncorrectable == 0) && (m.m_text == "retransmitted message"));
        }

        check(lost && repaired && (d.status().m_recovered == 1), "duplicates: retransmission repairs lost words of the first copy");
        (void) clean;
    }
    {
        // Polarities are separate networks
        Dec d;
        d.decodeFrame(rxOf("duplicates", "original"));
        Dec::RxFrame inv = rxOf("duplicates", "retransmission");
        inv.m_inverted = true;
        std::vector<Dec::Message> out = d.decodeFrame(inv);
        bool fresh = (out.size() == 3);
        for (const Dec::Message& m : out) {
            fresh = fresh && !m.m_duplicate;
        }
        check(fresh, "duplicates: history is kept per polarity (same messages on the other polarity are not duplicates)");
    }
}

void testGroup()
{
    Dec d;
    std::vector<Dec::Message> setup = d.decodeFrame(rxOf("group", "setup"));
    bool ok = (setup.size() == 5);

    for (const Dec::Message& m : setup) {
        ok = ok && (m.m_type == Dec::TypeShortInstruction) && (m.m_instructionType == 0) && m.m_groupTracked
            && ((m.m_instructionSlot == 5) || (m.m_instructionSlot == 6)) && (m.m_instructionFrame == ((m.m_instructionSlot == 5) ? 20 : 18));
    }

    check(ok, "group: assignments decoded as short instructions with slot and frame");

    std::vector<Dec::Message> delivery = d.decodeFrame(rxOf("group", "delivery alpha"));
    check(delivery.size() == 1 && delivery[0].m_groupTracked && (delivery[0].m_tempSlot == 5) && (delivery[0].m_groupMembers.size() == 3)
        && (delivery[0].m_groupMembers[0] == 3000) && (delivery[0].m_groupMembers[2] == 3002) && (delivery[0].m_addressType == Dec::AddressTemporary),
        "group: delivery message carries the members of the group");

    delivery = d.decodeFrame(rxOf("group", "delivery numeric"));
    check(delivery.size() == 1 && delivery[0].m_groupTracked && delivery[0].m_groupMembers.size() == 2, "group: second group");

    // Assignment expires without delivery
    Dec e;
    e.decodeFrame(rxOf("group", "setup"));
    Dec::RxFrame late = rxOf("group", "delivery alpha");
    late.m_cycle = (late.m_cycle + 2) % 15;
    delivery = e.decodeFrame(late);
    check(delivery.size() == 1 && !delivery[0].m_groupTracked, "group: assignment expires after 128 frames");

    // Delivery torn down: the next one is not tracked
    delivery = d.decodeFrame(rxOf("group", "delivery alpha"));
    check(delivery.size() == 1 && !delivery[0].m_groupTracked, "group: assignment ends with the group message");

    // The other polarity is another network with its own frame numbering: its frames do not
    // expire the assignments of this one
    Dec g;
    Dec::RxFrame invertedSetup = rxOf("group", "setup");
    invertedSetup.m_inverted = true;
    g.decodeFrame(invertedSetup);
    Dec::RxFrame otherNetwork = rxOf("group", "delivery numeric");
    otherNetwork.m_cycle = (otherNetwork.m_cycle + 2) % 15;
    g.decodeFrame(otherNetwork);
    Dec::RxFrame invertedDelivery = rxOf("group", "delivery alpha");
    invertedDelivery.m_inverted = true;
    delivery = g.decodeFrame(invertedDelivery);
    check(delivery.size() == 1 && delivery[0].m_groupTracked && (delivery[0].m_groupMembers.size() == 3) && delivery[0].m_inverted,
        "group: a frame of the other polarity 256 frames later does not expire the assignments of this one");
}

// Multiple transmission (STD-43A 3.4.2, Fig. 3.4.2-3), 2x with collapse 1: repeat interval 2
// frames. Subframe 1 of frame f is the first transmission of a content, subframe 2 of frame
// f + 2 its second transmission; the two subframes of one frame are different contents
void testSubframes()
{
    std::string error;
    Enc enc;
    Enc encB;
    Enc::TxMessage tx;
    Enc::TxMessage tx2;
    Enc::TxMessage txB;

    enc.setCollapse(1, error);
    encB.setCollapse(1, error);
    tx.m_capcode = 5000;
    tx.m_type = Flex::Alphanumeric;
    tx.m_text = "Subframe copies repair each other";
    tx2.m_capcode = 5001;
    tx2.m_type = Flex::Numeric;
    tx2.m_text = "424242";
    txB.m_capcode = 6000;
    txB.m_type = Flex::Alphanumeric;
    txB.m_text = "Another page";

    // Content A (two messages) and content B (one message), each fits a 44 word subframe
    Dec::RxFrame base = encoderFrame(enc, {tx, tx2});
    std::vector<uint32_t> a = codewords(base.m_phases[0]);
    std::vector<uint32_t> b = codewords(encoderFrame(encB, {txB}).m_phases[0]);
    std::vector<uint32_t> idle(44, Flex::encodeWord(0));

    a.resize(44);
    b.resize(44);
    idle[0] = Flex::blockInfoWord(0, 0, 1, 0, 1);   // No addresses

    const int target = 12;                          // A message word of content A

    // Frame of 2 subframes; erased word positions are frame positions
    auto frameOf = [&](unsigned frame, const std::vector<uint32_t>& sub1, const std::vector<uint32_t>& sub2,
        const std::vector<int>& erasedWords) {
        Dec::RxFrame f = base;
        std::vector<uint32_t> words(sub1);
        std::vector<uint8_t> erased(88, 0);

        words.insert(words.end(), sub2.begin(), sub2.end());
        for (int i : erasedWords) {
            erased[i] = 1;
        }
        f.m_hasFiw = false;
        f.m_cycle = 2;
        f.m_frame = frame;
        f.m_repeat = true;
        f.m_traffic = 1;                            // r = 1, t1 t0 = 01: 2 transmissions
        f.m_phases[0].m_words = interleaved(words);
        f.m_phases[0].m_erased = erased;
        return f;
    };
    auto count = [](const std::vector<Dec::Message>& out, uint64_t capcode) {
        int n = 0;
        for (const Dec::Message& m : out) {
            n += (m.m_capcode == capcode) ? 1 : 0;
        }
        return n;
    };
    auto lost = [](const std::vector<Dec::Message>& out) {
        int n = 0;
        for (const Dec::Message& m : out) {
            n += m.m_wordsUncorrectable;
        }
        return n;
    };

    {
        // A in frame 8, its second transmission in frame 10 next to the first of B
        Dec d;
        std::vector<Dec::Message> first = d.decodeFrame(frameOf(8, a, idle, {}));
        std::vector<Dec::Message> second = d.decodeFrame(frameOf(10, b, a, {}));
        check((first.size() == 2) && (lost(first) == 0) && (d.lastFrame().m_transmissions == 2)
            && (second.size() == 1) && (count(second, 6000) == 1),
            "subframes: a clean first transmission decodes at once, its second transmission is not output again");
    }
    {
        // Damaged A in subframe 1 and a clean B (second transmission, first one missed) in
        // subframe 2: they are not combined; B decodes from its copy, A waits for its own
        Dec d;
        std::vector<Dec::Message> first = d.decodeFrame(frameOf(8, a, b, {target}));
        bool waiting = d.lastFrame().m_blocks[0].m_waiting;
        std::vector<Dec::Message> second = d.decodeFrame(frameOf(10, idle, a, {}));
        check((first.size() == 1) && (count(first, 6000) == 1) && (lost(first) == 0) && waiting,
            "subframes: the two subframes of a frame are different contents, a content is decoded from its second transmission");
        check((second.size() == 2) && (count(second, 5000) == 1) && (lost(second) == 0),
            "subframes: the second transmission repairs the first, two frames later");
        bool frameOk = !second.empty();
        for (const Dec::Message& m : second) {
            frameOk = frameOk && (m.m_frame == 8);
        }
        check(frameOk, "subframes: the content is given the frame of its first transmission");
    }
    {
        // Words lost in each transmission are repaired by the other
        Dec d;
        std::vector<Dec::Message> first = d.decodeFrame(frameOf(8, a, idle, {target}));
        std::vector<Dec::Message> second = d.decodeFrame(frameOf(10, idle, a, {44 + target + 1}));
        check(first.empty() && (second.size() == 2) && (lost(second) == 0),
            "subframes: words lost in different transmissions are repaired by the other");
    }
    {
        // The same word lost in both transmissions: decoded at the last one with the loss
        Dec d;
        std::vector<Dec::Message> first = d.decodeFrame(frameOf(8, a, idle, {target}));
        std::vector<Dec::Message> second = d.decodeFrame(frameOf(10, idle, a, {44 + target}));
        check(first.empty() && (second.size() == 2) && (lost(second) == 1),
            "subframes: the same word lost in both transmissions stays lost");
    }
    {
        // Last transmission not received: decoded when it is due
        Dec d;
        std::vector<Dec::Message> first = d.decodeFrame(frameOf(8, a, idle, {target}));
        std::vector<Dec::Message> later = d.decodeFrame(frameOf(12, idle, idle, {}));
        check(first.empty() && (later.size() == 2) && (lost(later) == 1),
            "subframes: a content whose next transmission is missed is decoded with the words received");
    }
    {
        // Signal lost: flush decodes what was received
        Dec d;
        std::vector<Dec::Message> first = d.decodeFrame(frameOf(8, a, idle, {target}));
        std::vector<Dec::Message> f = d.flush();
        check(first.empty() && (f.size() == 2), "subframes: flush decodes the transmissions received");
    }
}

void testMisc()
{
    Dec d;

    check(std::string(Dec::messageTypeName(Dec::TypeNumberedSpecialNumeric)) == "numbered_special_numeric", "type names");
    check(std::string(Dec::addressTypeName(Dec::AddressOperator)) == "operator", "address type names");

    // Raw payload packing of a numeric message
    Enc enc;
    Enc::TxMessage tx;

    tx.m_capcode = 600;
    tx.m_type = Flex::Numeric;
    tx.m_text = "1234";

    std::vector<Dec::Message> out = d.decodeFrame(encoderFrame(enc, {tx}));

    check(out.size() == 1 && (out[0].m_rawBits == 21) && (out[0].m_rawData.size() == 3) && (out[0].m_address == "0000000600"),
        "raw payload: one numeric word, 21 bits in 3 bytes, address of 10 digits");

    if (out.size() == 1)
    {
        // Bits d0 first in the most significant bit: the digits 1234 are sent as 4 bit groups after 2 check bits
        uint32_t word = 0;

        for (int b = 0; b < 21; b++) {
            word |= (uint32_t) ((out[0].m_rawData[b / 8] >> (7 - b % 8)) & 1) << b;
        }

        check(((word >> 2) & 0xf) == 1 && ((word >> 6) & 0xf) == 2 && ((word >> 10) & 0xf) == 3 && ((word >> 14) & 0xf) == 4,
            "raw payload: bit order d0 first");
    }

    // Binary message
    const RefFrame *f = findFrame("binary", "hex 8 digits");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && (out[0].m_blocking == 8) && (out[0].m_dataBits == 32) && out[0].m_signature == Dec::CheckOk
        && out[0].m_checksum == Dec::CheckNone, "binary: blocking, data bits and signature");

    f = findFrame("binary", "hex 21 digits");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && (out[0].m_blocking == 7) && out[0].m_displayRtl && out[0].m_headerMessage && !out[0].m_statusInfo,
        "binary: display direction and header flags");

    f = findFrame("types", "alpha mail drop");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && out[0].m_mailDrop && out[0].m_retrieval && out[0].m_checksum == Dec::CheckOk && out[0].m_signature == Dec::CheckOk,
        "alphanumeric: mail drop and retrieval flags, checksum and signature");

    f = findFrame("types", "numbered numeric");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && out[0].m_retrieval && !out[0].m_special && (out[0].m_checksum == Dec::CheckOk), "numbered numeric: flags and checksum");

    f = findFrame("short", "short numbered");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && (out[0].m_shortType == 2) && (out[0].m_shortSource == 3) && out[0].m_retrieval && (out[0].m_messageNumber == 17),
        "short message: source, number and retrieval flag");

    f = findFrame("short", "instruction event");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && (out[0].m_instructionType == 1) && (out[0].m_eventFlags == 3) && (out[0].m_addressType == Dec::AddressOperator)
        && (out[0].m_operatorSubtype == "system_event"), "short instruction: system event flags on an operator address");

    f = findFrame("special", "operator alpha");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && (out[0].m_addressType == Dec::AddressOperator) && (out[0].m_operatorSubtype == "all")
        && (out[0].m_operatorCategory == "system_message"), "operator address: subtype and category");

    f = findFrame("special", "info service alpha");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && (out[0].m_addressType == Dec::AddressInfoService), "information service address");

    f = findFrame("special", "network alpha");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.size() == 1 && (out[0].m_addressType == Dec::AddressNetwork), "network address");

    f = findFrame("special", "reserved short");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));
    check(out.empty(), "reserved short address ignored");

    f = findFrame("mixed", "mixed priority");
    out = d.decodeFrame(toRxFrame((Flex::Mode) f->m_mode, false, fromHex(f->m_sync), fromHex(f->m_data)));

    int priority = 0;
    for (const Dec::Message& m : out) {
        priority += m.m_priority ? 1 : 0;
    }
    check(out.size() == 6 && priority == 2 && d.lastFrame().m_blocks[0].m_priorityWords == 2 && d.lastFrame().m_blocks[0].m_collapse == 2,
        "priority addresses flagged, collapse from the block information word");

    f = findFrame("mode 6400/4", "inverted");
    Dec::RxFrame rx = toRxFrame((Flex::Mode) f->m_mode, true, fromHex(f->m_sync), fromHex(f->m_data));
    out = d.decodeFrame(rx);
    check(out.size() == 1 && out[0].m_inverted && out[0].m_mode == Flex::Mode6400_4 && out[0].m_baud == 3200 && out[0].m_levels == 4
        && out[0].m_bitRate == 6400 && out[0].m_phase == 'A' && out[0].m_cycle == 5, "reception fields: mode, baud, levels, polarity, phase, frame");

    rx.m_quality = 0.25;
    out = d.decodeFrame(rx);
    check(!out.empty() && out[0].m_quality == 0.25, "frame quality copied to messages");

    d.reset();
    check(d.status().m_frames == 0 && d.status().m_codewords == 0, "reset clears the counters");
}

} // namespace

// ---------------------------------------------------------------------------
// Roaming (system ID words)
// ---------------------------------------------------------------------------

void testRoaming()
{
    // Bit layout of STD-43A table 3.7.2-1 (and osmocom flex/frame.c): the check of the words is the same as
    // any information word, so compare the data bits after decoding the codeword
    uint32_t w1 = Flex::ssid1Word(0x155, 0x0b);
    uint32_t w2 = Flex::ssid2Word(0x2a5, 0x9);
    uint32_t d1 = Flex::wordData(w1);
    uint32_t d2 = Flex::wordData(w2);
    bool ok1 = true, ok2 = true;

    check(ok1 && ((d1 >> 4) & 7) == 0 && ((d1 >> 7) & 0x1f) == 0x0b && ((d1 >> 12) & 0x1ff) == 0x155, "ssid1 word layout");
    check(ok2 && ((d2 >> 4) & 7) == 7 && ((d2 >> 7) & 0xf) == 0x9 && ((d2 >> 11) & 0x3ff) == 0x2a5, "ssid2 word layout");

    const Flex::Mode modes[] = {Flex::Mode1600_2, Flex::Mode3200_2, Flex::Mode3200_4, Flex::Mode6400_4};

    for (Flex::Mode mode : modes)
    {
        Enc enc;
        Dec dec;
        Enc::Roaming r;
        r.m_enabled = true;
        r.m_localId = 0x155;
        r.m_coverageZone = 0x0b;
        r.m_countryCode = 0x2a5;
        r.m_trafficFlags = 0x9;
        enc.setMode(mode);
        enc.setRoaming(r);

        // Messages in every frame so that the capacity with the extra words is exercised
        std::vector<Enc::TxMessage> sent;
        for (int i = 0; i < 12; i++)
        {
            Enc::TxMessage m;
            m.m_capcode = 1000000 + i * 7;
            m.m_type = Flex::Alphanumeric;
            m.m_text = "roaming message " + std::to_string(i);
            sent.push_back(m);
            enc.enqueue(m);
        }

        std::vector<Dec::Message> all;
        int phases = Flex::phaseCount(mode);
        bool layoutOk = true;
        bool flagOk = true;

        for (uint64_t index = 0; index < 12 || enc.pending() > 0; index++)
        {
            if (index > 4000) {
                break;
            }

            Enc::Frame f = enc.buildFrame(index);

            if (f.m_reserved) {
                continue;
            }

            std::vector<Dec::Message> out = dec.decodeFrame(toRxFrame(f.m_mode, f.m_inverted, f.m_bytes.m_sync, f.m_bytes.m_data));
            all.insert(all.end(), out.begin(), out.end());
            const Dec::FrameReport& rep = dec.lastFrame();
            flagOk = flagOk && rep.m_roaming;

            unsigned frame = rep.m_frame;
            int expect = Flex::ssidPhase(mode, frame);
            int seen1 = 0, seen2 = 0;

            for (size_t b = 0; b < rep.m_blocks.size(); b++)
            {
                for (const Dec::ExtraBiw& e : rep.m_blocks[b].m_extra)
                {
                    bool here = ((int) b == expect);

                    if (e.m_kind == Dec::ExtraBiw::Ssid1)
                    {
                        seen1++;
                        layoutOk = layoutOk && here && (e.m_localId == 0x155) && (e.m_coverageZone == 0x0b);
                    }
                    else if (e.m_kind == Dec::ExtraBiw::Ssid2)
                    {
                        seen2++;
                        layoutOk = layoutOk && here && (e.m_country == 0x2a5) && (e.m_tmf == 0x9);
                    }
                    else
                    {
                        layoutOk = false;
                    }
                }
            }

            int want2 = (index <= 3) ? 1 : 0;
            layoutOk = layoutOk && (seen1 == 1) && (seen2 == want2) && ((int) rep.m_blocks.size() == phases);
        }

        char buf[128];
        std::snprintf(buf, sizeof(buf), "roaming %s: frame information flag", Flex::modeName(mode));
        check(flagOk, buf);
        std::snprintf(buf, sizeof(buf), "roaming %s: system ID words in frames and phases", Flex::modeName(mode));
        check(layoutOk, buf);

        std::vector<std::string> a, b;
        for (const Enc::TxMessage& m : sent) {
            a.push_back(std::to_string(m.m_capcode) + m.m_text);
        }
        for (const Dec::Message& m : all) {
            b.push_back(std::to_string(m.m_capcode) + m.m_text);
        }
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        std::snprintf(buf, sizeof(buf), "roaming %s: messages still delivered (%zu)", Flex::modeName(mode), all.size());
        check(a == b, buf);
    }

    // Off by default: no flag, no extra words
    Enc enc;
    Dec dec;
    Enc::Frame f = enc.buildFrame(0);
    dec.decodeFrame(toRxFrame(f.m_mode, f.m_inverted, f.m_bytes.m_sync, f.m_bytes.m_data));
    check(!dec.lastFrame().m_roaming && dec.lastFrame().m_blocks[0].m_extra.empty(), "roaming off: no flag and no extra words");
}

int main()
{
    testReferenceFrames();
    testRoaming();
    testBiw();
    testRoundTrip();
    testErrors();
    testFragments();
    testDuplicates();
    testGroup();
    testSubframes();
    testMisc();

    std::printf("%d checks passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
