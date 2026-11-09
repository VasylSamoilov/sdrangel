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

// Self test of the sequential Golay paging format. Prints one line per check and
// returns non-zero if any check fails.

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "radiopaginggsc.h"
#include "radiopaginggscdecoder.h"
#include "radiopaginggscreceiver.h"

namespace {

int passes = 0;
int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passes++;
    } else {
        failures++;
    }
}

typedef RadioPagingGsc G;
typedef RadioPagingGscDecoder D;

void testCodes()
{
    bool encodeOk = true;
    bool errorsOk = true;
    bool complementOk = true;
    long patterns = 0;

    for (uint32_t data = 0; data < 4096; data++)
    {
        uint32_t cw = G::golayEncode((uint16_t) data);

        if ((G::golaySyndrome(cw) != 0) || ((cw & 0xfff) != data)) {
            encodeOk = false;
        }
        if (G::golaySyndrome(cw ^ 0x7fffff) != 0) {
            complementOk = false;
        }

        G::GolayResult clean = G::golayDecode(cw);
        if ((clean.m_word != cw) || (clean.m_data != data) || (clean.m_bitErrors != 0)) {
            errorsOk = false;
        }

        for (int i = 0; i < 23; i++)
        {
            uint32_t e1 = 1u << i;
            G::GolayResult r1 = G::golayDecode(cw ^ e1);
            patterns++;
            if ((r1.m_word != cw) || (r1.m_bitErrors != 1)) {
                errorsOk = false;
            }

            for (int j = i + 1; j < 23; j++)
            {
                uint32_t e2 = e1 | (1u << j);
                G::GolayResult r2 = G::golayDecode(cw ^ e2);
                patterns++;
                if ((r2.m_word != cw) || (r2.m_bitErrors != 2)) {
                    errorsOk = false;
                }

                for (int k = j + 1; k < 23; k++)
                {
                    G::GolayResult r3 = G::golayDecode(cw ^ e2 ^ (1u << k));
                    patterns++;
                    if ((r3.m_word != cw) || (r3.m_bitErrors != 3)) {
                        errorsOk = false;
                    }
                }
            }
        }
    }

    check(encodeOk, "Golay: all 4096 data words encode to codewords with data in bits 11-0");
    check(complementOk, "Golay: the complement of every codeword is a codeword");
    check(errorsOk, "Golay: all error patterns of weight 0-3 on all 4096 codewords decode to the original");
    std::printf("      %ld error patterns checked\n", patterns);

    check(G::golayEncode(0) == 0, "Golay: zero data gives the zero codeword");

    bool bchEncodeOk = true;
    bool bchErrorsOk = true;
    bool bchBeyond = true;
    long bchPatterns = 0;

    for (uint32_t data = 0; data < 128; data++)
    {
        uint16_t cw = G::bchEncode((uint8_t) data);

        if ((G::bchSyndrome(cw) != 0) || ((cw & 0x7f) != data)) {
            bchEncodeOk = false;
        }

        G::BchResult clean = G::bchDecode(cw);
        if ((clean.m_word != cw) || (clean.m_data != data) || (clean.m_bitErrors != 0)) {
            bchErrorsOk = false;
        }

        for (int i = 0; i < 15; i++)
        {
            uint16_t e1 = (uint16_t) (1u << i);
            G::BchResult r1 = G::bchDecode(cw ^ e1);
            bchPatterns++;
            if ((r1.m_word != cw) || (r1.m_bitErrors != 1)) {
                bchErrorsOk = false;
            }

            for (int j = i + 1; j < 15; j++)
            {
                G::BchResult r2 = G::bchDecode(cw ^ e1 ^ (1u << j));
                bchPatterns++;
                if ((r2.m_word != cw) || (r2.m_bitErrors != 2)) {
                    bchErrorsOk = false;
                }
            }
        }
    }

    // Some patterns of weight 3 are detected as uncorrectable, none are silently accepted as weight 0
    int detected = 0;
    for (int i = 0; i < 15; i++)
    {
        for (int j = i + 1; j < 15; j++)
        {
            for (int k = j + 1; k < 15; k++)
            {
                G::BchResult r = G::bchDecode((uint16_t) ((1u << i) | (1u << j) | (1u << k)));
                if (r.m_bitErrors < 0) {
                    detected++;
                } else if (r.m_bitErrors == 0) {
                    bchBeyond = false;
                }
            }
        }
    }

    check(bchEncodeOk, "BCH: all 128 data words encode to codewords with data in bits 6-0");
    check(bchErrorsOk, "BCH: all error patterns of weight 0-2 on all 128 codewords decode to the original");
    check(bchBeyond && (detected > 0), "BCH: weight 3 patterns are never taken for clean codewords and some are detected");
    std::printf("      %ld error patterns checked, %d of 455 weight 3 patterns detected\n", bchPatterns, detected);
}

void testAddresses()
{
    bool roundTrip = true;
    bool illegalOk = true;
    long count = 0;

    for (int index = 0; index < 10; index++)
    {
        for (int group = 0; group < 100; group++)
        {
            for (int low = 0; low < 1000; low++)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d%02d%03d", index, group, low);
                std::string address(buf);
                int preamble;
                uint16_t w1, w2;
                bool ok = G::encodeAddress(address, preamble, w1, w2);
                std::string back;

                if (ok)
                {
                    count++;
                    if (!G::decodeAddress(w1, w2, preamble, back) || (back != address) || (w2 >= 2000)
                        || (G::word1Index(w1 ^ 0xfff) >= 0) || (G::word1Index(w1) < 0)) {
                        roundTrip = false;
                    }
                }
                else
                {
                    // The same digits are refused for every index
                    static const int lowIllegal[16] = { 0, 25, 51, 103, 206, 340, 363, 412, 445, 530, 642, 726, 782, 810, 825, 877 };
                    static const int highIllegal[7] = { 0, 292, 425, 584, 631, 841, 851 };
                    bool expected = false;

                    for (int v : lowIllegal) {
                        expected = expected || ((group < 50) && (v == low));
                    }
                    for (int v : highIllegal) {
                        expected = expected || ((group >= 50) && (v == low));
                    }
                    if (!expected) {
                        illegalOk = false;
                    }
                }
            }
        }
    }

    check(roundTrip, "Address: encode and decode agree for all legal addresses, word 2 below 2000, no word 1 collides with a complement");
    check(illegalOk, "Address: only the listed digit combinations are refused");
    std::printf("      %ld legal addresses\n", count);

    int preamble;
    uint16_t w1, w2;
    std::string error;
    check(!G::encodeAddress("12345", preamble, w1, w2, &error), "Address: 5 digits refused");
    check(!G::encodeAddress("12a456", preamble, w1, w2, &error), "Address: non digit refused");

    bool preambleOk = true;
    for (int i = 0; i < 10; i++)
    {
        uint32_t cw = G::golayEncode(G::preambleValue(i));
        G::GolayResult direct = G::golayDecode(cw);
        G::GolayResult flipped = G::golayDecode(cw ^ 0x7fffff);

        if ((G::preambleIndex(direct.m_data) != i) || (G::preambleIndex(flipped.m_data) >= 0)
            || (G::preambleIndex(flipped.m_data ^ 0xfff) != i)) {
            preambleOk = false;
        }
    }
    check(preambleOk, "Preamble values and their complements are distinguishable");
}

// Feed the bits, optionally inverted, after random noise
struct Decoded
{
    std::vector<D::Message> m_messages;
    D::Status m_status;
    bool m_availableBeforeFlush = false;
};

Decoded decode(const std::vector<uint8_t>& bits, D::Polarity polarity, bool invertBits, int noiseBits, unsigned seed, bool nbs = false)
{
    std::mt19937 rng(seed);
    D decoder(polarity);
    Decoded out;

    decoder.setNonBatterySaver(nbs);

    for (int i = 0; i < noiseBits; i++) {
        decoder.feed((int) (rng() & 1));
    }
    for (uint8_t bit : bits) {
        decoder.feed(invertBits ? (bit ^ 1) : bit);
    }

    out.m_availableBeforeFlush = decoder.hasMessage();
    decoder.flush();

    while (decoder.hasMessage()) {
        out.m_messages.push_back(decoder.takeMessage());
    }
    out.m_status = decoder.getStatus();

    return out;
}

G::TxMessage tx(const char *address, int number, G::MessageType type, const char *text = "")
{
    G::TxMessage m;

    m.m_address = address;
    m.m_addressNumber = number;
    m.m_type = type;
    m.m_text = text;

    return m;
}

bool matches(const D::Message& rx, const G::TxMessage& m)
{
    if ((rx.m_address.substr(1) != m.m_address.substr(1)) || (rx.m_addressNumber != m.m_addressNumber)) {
        return false;
    }
    if ((rx.m_address[0] != '?') && (rx.m_address[0] != m.m_address[0])) {
        return false;
    }

    switch (m.m_type)
    {
    case G::Tone:
        return (rx.m_type == G::Tone) && (rx.m_rawBits == 0);
    case G::Voice:
        return (rx.m_type == G::Voice) && (rx.m_rawBits == 0);
    case G::Numeric:
        return (rx.m_type == G::Data) && (G::decodeNumeric(rx.m_rawData, rx.m_rawBits) == m.m_text);
    default:
        return (rx.m_type == G::Data) && (G::decodeAlphanumeric(rx.m_rawData, rx.m_rawBits) == m.m_text);
    }
}

bool allMatch(const Decoded& d, const std::vector<G::TxMessage>& sent)
{
    if (d.m_messages.size() != sent.size()) {
        return false;
    }
    for (size_t i = 0; i < sent.size(); i++)
    {
        if (!matches(d.m_messages[i], sent[i])) {
            return false;
        }
    }
    return true;
}

void roundTrip(const char *name, const std::vector<G::TxMessage>& sent, G::Mode mode = G::Auto)
{
    G::EncodeOptions options;
    options.m_mode = mode;
    G::EncodeResult enc = G::encode(sent, options);
    std::string label = name;

    if (!enc.m_ok)
    {
        std::printf("      encode error: %s\n", enc.m_error.c_str());
        check(false, (label + ": encode").c_str());
        return;
    }

    Decoded normal = decode(enc.m_bits, D::Normal, false, 0, 1);
    check(allMatch(normal, sent), (label + ": normal polarity round trip").c_str());

    Decoded inverted = decode(enc.m_bits, D::Inverted, true, 0, 1);
    check(allMatch(inverted, sent), (label + ": inverted bits, inverted decoder").c_str());

    Decoded wrongA = decode(enc.m_bits, D::Inverted, false, 0, 1);
    Decoded wrongB = decode(enc.m_bits, D::Normal, true, 0, 1);
    check(wrongA.m_messages.empty() && wrongB.m_messages.empty(), (label + ": the other polarity decoder stays silent").c_str());

    Decoded noisy = decode(enc.m_bits, D::Normal, false, 3000, 7);
    check(allMatch(noisy, sent), (label + ": preceded by random noise").c_str());
}

// Offsets of a single message transmission with a preamble (see the format description)
void injectGolay(std::vector<uint8_t>& bits, size_t start)
{
    static const int pairs[3] = { 1, 8, 17 };

    for (int p : pairs) {
        bits[start + 2 * p] ^= 1;
    }
}

void testErrors()
{
    std::vector<G::TxMessage> sent = { tx("100123", 3, G::Alphanumeric, "ERRORS IN ALL CODEWORDS OF THIS LONGER MESSAGE 0123456789") };
    G::EncodeResult enc = G::encode(sent);
    int blocks = ((int) enc.m_bits.size() - 1098) / 121;

    for (int i = 0; i < 18; i++) {
        injectGolay(enc.m_bits, 28 + 46 * i);
    }
    injectGolay(enc.m_bits, 884);
    injectGolay(enc.m_bits, 931);
    injectGolay(enc.m_bits, 1005);
    injectGolay(enc.m_bits, 1052);

    for (int b = 0; b < blocks; b++)
    {
        for (int k = 0; k < 8; k++)
        {
            enc.m_bits[1099 + 121 * b + 3 * 8 + k] ^= 1;
            enc.m_bits[1099 + 121 * b + 11 * 8 + k] ^= 1;
        }
    }

    Decoded d = decode(enc.m_bits, D::Normal, false, 0, 1);
    check(allMatch(d, sent), "Three errors in every Golay codeword and two in every BCH codeword still decode");
    check(!d.m_messages.empty() && (d.m_messages[0].m_errorsCorrected > 0) && (d.m_messages[0].m_errorsUncorrectable == 0)
        && (d.m_messages[0].m_checksumErrors == 0), "Corrections are reported, nothing uncorrectable");
    std::printf("      %d blocks, %d bits corrected\n", blocks, d.m_messages.empty() ? -1 : d.m_messages[0].m_errorsCorrected);

    // A tone only message with errors in its address
    std::vector<G::TxMessage> tone = { tx("100123", 2, G::Tone) };
    G::EncodeResult encTone = G::encode(tone);
    injectGolay(encTone.m_bits, 1005);
    injectGolay(encTone.m_bits, 1052);
    check(allMatch(decode(encTone.m_bits, D::Normal, false, 0, 1), tone), "Tone only with errors in the address words");
}

void testBatches()
{
    std::vector<G::TxMessage> three = {
        tx("100123", 1, G::Tone),
        tx("219789", 2, G::Numeric, "5551234 -U"),
        tx("100456", 4, G::Alphanumeric, "MEETING AT NOON"),
    };
    roundTrip("Batch of 3", three);

    G::EncodeResult enc = G::encode(three);
    check(enc.m_ok && (enc.m_slots == 16), "Batch of 3 uses 16 slots");

    std::vector<G::TxMessage> full;
    for (int i = 0; i < 16; i++)
    {
        char address[8];
        std::snprintf(address, sizeof(address), "1007%02d", i);
        full.push_back(tx(address, 1 + (i & 3), (i % 3 == 0) ? G::Tone : ((i % 3 == 1) ? G::Numeric : G::Alphanumeric),
            (i % 3 == 1) ? "0123456789" : ((i % 3 == 2) ? "PAGE" : "")));
    }
    roundTrip("Normal batch of 16", full);

    std::vector<G::TxMessage> extended;
    for (int i = 0; i < 20; i++)
    {
        char address[8];
        std::snprintf(address, sizeof(address), "1007%02d", i);
        extended.push_back(tx(address, 1 + ((i + 1) & 3), (i % 3 == 0) ? G::Numeric : ((i % 3 == 1) ? G::Tone : G::Alphanumeric),
            (i % 3 == 0) ? "98765 4321" : ((i % 3 == 2) ? "EXTENDED BATCH MESSAGE NUMBER" : "")));
    }
    roundTrip("Extended batch of 20", extended);
    G::EncodeResult encExt = G::encode(extended);
    check(encExt.m_ok && (encExt.m_slots == 32), "Extended batch uses 32 slots");

    Decoded d = decode(encExt.m_bits, D::Normal, false, 0, 1);
    bool batchFlag = !d.m_messages.empty();
    for (const D::Message& m : d.m_messages) {
        batchFlag = batchFlag && m.m_batch && (m.m_preamble == 1);
    }
    check(batchFlag && (d.m_messages.size() == 20) && (d.m_messages[19].m_slot == 19), "Batch flag, preamble index and slots are reported");

    std::vector<G::TxMessage> mixedPreamble = { tx("100123", 1, G::Tone), tx("300123", 1, G::Tone) };
    check(!G::encode(mixedPreamble).m_ok, "Batch with different preamble values is refused");
    check(!G::encode({ tx("100123", 1, G::Voice), tx("100456", 1, G::Tone) }).m_ok, "Voice in a batch is refused");
}

void testNonBatterySaver()
{
    std::vector<G::TxMessage> sent = { tx("500123", 2, G::Alphanumeric, "NO PREAMBLE CODEWORDS") };
    G::EncodeOptions options;
    options.m_mode = G::NonBatterySaver;
    G::EncodeResult enc = G::encode(sent, options);
    Decoded off = decode(enc.m_bits, D::Normal, false, 0, 1, false);
    Decoded on = decode(enc.m_bits, D::Normal, false, 0, 1, true);
    Decoded onNoise = decode(enc.m_bits, D::Normal, false, 500, 3, true);
    Decoded onInverted = decode(enc.m_bits, D::Inverted, true, 0, 1, true);

    check(enc.m_ok && off.m_messages.empty(), "Non battery saver pattern is ignored unless enabled");
    check(allMatch(on, sent) && on.m_messages[0].m_nonBatterySaver && (on.m_messages[0].m_address[0] == '?'), "Non battery saver alphanumeric round trip");
    check(allMatch(onNoise, sent), "Non battery saver after random noise");
    check(allMatch(onInverted, sent), "Non battery saver, inverted bits, inverted decoder");

    std::vector<G::TxMessage> tone = { tx("500123", 1, G::Tone) };
    check(allMatch(decode(G::encode(tone, options).m_bits, D::Normal, false, 0, 1, true), tone), "Non battery saver tone only");
    std::vector<G::TxMessage> voice = { tx("500123", 3, G::Voice) };
    check(allMatch(decode(G::encode(voice, options).m_bits, D::Normal, false, 0, 1, true), voice), "Non battery saver voice");
}

void testInterpretation()
{
    std::vector<G::TxMessage> alpha = { tx("100123", 1, G::Alphanumeric, "HELLO WORLD") };
    std::vector<G::TxMessage> numeric = { tx("100123", 1, G::Numeric, "5551234") };
    Decoded a = decode(G::encode(alpha).m_bits, D::Normal, false, 0, 1);
    Decoded n = decode(G::encode(numeric).m_bits, D::Normal, false, 0, 1);

    G::Interpretation ia = G::interpret(a.m_messages[0].m_rawData, a.m_messages[0].m_rawBits);
    G::Interpretation in = G::interpret(n.m_messages[0].m_rawData, n.m_messages[0].m_rawBits);

    check((ia.m_type == "alphanumeric") && (ia.m_text == "HELLO WORLD") && (ia.m_altType == "numeric") && (ia.m_score > ia.m_altScore),
        "Interpretation of an alphanumeric message");
    check((in.m_type == "numeric") && (in.m_text == "5551234") && (in.m_altType == "alphanumeric") && (in.m_score > in.m_altScore),
        "Interpretation of a numeric message");
    std::printf("      alpha: %s %d / %s %d, numeric: %s %d / %s %d\n", ia.m_type.c_str(), ia.m_score, ia.m_altType.c_str(), ia.m_altScore,
        in.m_type.c_str(), in.m_score, in.m_altType.c_str(), in.m_altScore);

    check(G::interpret(std::vector<uint8_t>(), 0).m_type == "tone_only", "Interpretation without bits is tone only");

    // A full block of alphanumeric text has no padding
    std::vector<G::TxMessage> full = { tx("100123", 1, G::Alphanumeric, "ABCDEFGH") };
    Decoded f = decode(G::encode(full).m_bits, D::Normal, false, 0, 1);
    G::Interpretation fi = G::interpret(f.m_messages[0].m_rawData, f.m_messages[0].m_rawBits);
    check(!f.m_messages.empty() && ((fi.m_type == "alphanumeric" ? fi.m_text : fi.m_altText) == "ABCDEFGH"),
        "A message that fills a block (no padding) keeps the alphanumeric reading as a candidate");

    std::vector<G::TxMessage> letters = { tx("100123", 1, G::Numeric, "ABCDEFGHJLNPR U-0123") };
    check(allMatch(decode(G::encode(letters).m_bits, D::Normal, false, 0, 1), letters), "Numeric letters through the shift nibble");

    check(G::escapeControl(std::string("A\r") + "B") == "A<0x0D>B", "Control characters are escaped");
}

void testStreaming()
{
    // Message available as soon as its last block is in, without flush
    std::vector<G::TxMessage> sent = { tx("100123", 1, G::Alphanumeric, "STREAMING") };
    Decoded d = decode(G::encode(sent).m_bits, D::Normal, false, 0, 1);
    check(d.m_availableBeforeFlush && allMatch(d, sent), "Message is available before flush");

    // Truncated message
    G::EncodeResult enc = G::encode({ tx("100123", 1, G::Alphanumeric, "THIS MESSAGE HAS THREE BLOCKS OF TEXT.") });
    std::vector<uint8_t> cut(enc.m_bits.begin(), enc.m_bits.begin() + 1098 + 121 * 2);
    Decoded t = decode(cut, D::Normal, false, 0, 1);
    check((t.m_messages.size() == 1) && t.m_messages[0].m_truncated && (t.m_messages[0].m_rawBits == 96), "Cut off message is reported truncated after flush");

    // Two transmissions with noise in between
    std::mt19937 rng(5);
    std::vector<G::TxMessage> a = { tx("100123", 1, G::Alphanumeric, "FIRST") };
    std::vector<G::TxMessage> b = { tx("300456", 2, G::Numeric, "123") };
    std::vector<uint8_t> bits = G::encode(a).m_bits;
    for (int i = 0; i < 300; i++) {
        bits.push_back((uint8_t) (rng() & 1));
    }
    std::vector<uint8_t> second = G::encode(b).m_bits;
    bits.insert(bits.end(), second.begin(), second.end());
    Decoded two = decode(bits, D::Normal, false, 100, 2);
    check((two.m_messages.size() == 2) && matches(two.m_messages[0], a[0]) && matches(two.m_messages[1], b[0]) && (two.m_status.m_transmissions == 2),
        "Two transmissions separated by noise");

    // Back to back without any gap
    std::vector<uint8_t> tight = G::encode(a).m_bits;
    tight.insert(tight.end(), second.begin(), second.end());
    Decoded two2 = decode(tight, D::Normal, false, 0, 1);
    check((two2.m_messages.size() == 2) && matches(two2.m_messages[0], a[0]) && matches(two2.m_messages[1], b[0]), "Two transmissions back to back");

    // Voice and tone only
    roundTrip("Voice", { tx("100123", 3, G::Voice) });
    roundTrip("Tone only", { tx("100123", 4, G::Tone) });
    roundTrip("Numeric", { tx("100123", 1, G::Numeric, "0123456789 -U") });
    roundTrip("Alphanumeric", { tx("100123", 2, G::Alphanumeric, "HELLO WORLD 123") });
    std::string longText;
    while (longText.size() < 200) {
        longText += "THE QUICK BROWN FOX 0123456789 ";
    }
    roundTrip("Long alphanumeric", { tx("750123", 1, G::Alphanumeric, longText.c_str()) });
    roundTrip("Group 50-99 numeric", { tx("968001", 1, G::Numeric, "42") });
    check(!G::encode({ tx("100123", 1, G::Alphanumeric, std::string(300, 'A').c_str()) }).m_ok, "Too long message refused");
    check(!G::encode({ tx("100123", 5, G::Tone) }).m_ok, "Address number 5 refused");
    check(!G::encode({ tx("100123", 1, G::Numeric, "12?") }).m_ok, "Character not allowed in numeric refused");
}

void testNoFalseLock()
{
    std::mt19937 rng(11);
    D normal(D::Normal);
    D inverted(D::Inverted);
    D nbs(D::Normal);

    nbs.setNonBatterySaver(true);

    for (int i = 0; i < 300000; i++)
    {
        int bit = (int) (rng() & 1);
        normal.feed(bit);
        inverted.feed(bit);
        nbs.feed(bit);
    }
    // Alternating bits and a lot of equal bits
    for (int i = 0; i < 5000; i++)
    {
        normal.feed(i & 1);
        inverted.feed(i & 1);
    }
    for (int i = 0; i < 5000; i++)
    {
        normal.feed(1);
        inverted.feed(0);
    }

    check((normal.getStatus().m_transmissions == 0) && (inverted.getStatus().m_transmissions == 0) && (nbs.getStatus().m_transmissions == 0)
        && !normal.hasMessage() && !inverted.hasMessage() && !nbs.hasMessage(), "No lock on 300000 random bits, a comma and a constant");
}

// Decodes one alphanumeric message whose start code (first copy) and complement (second copy)
// get the given numbers of single bit errors and of flipped bit pairs (a flipped pair is an
// error the duplicated bits cannot reveal)
bool startCodeReceived(int singles1, int pairs1, int singles2, int pairs2)
{
    G::TxMessage message;
    message.m_address = "123456";
    message.m_type = G::Alphanumeric;
    message.m_text = "START";
    G::EncodeOptions options;
    options.m_mode = G::Single;
    G::EncodeResult result = G::encode({message}, options);
    std::vector<uint8_t> bits = result.m_bits;

    // Locate the start code: 46 bits of the duplicated codeword of the start code value
    uint32_t start = G::golayEncode(G::m_startCode);
    int offset = -1;
    for (int o = 0; (o + 46 <= (int) bits.size()) && (offset < 0); o++)
    {
        bool match = true;
        for (int i = 0; (i < 23) && match; i++) {
            match = (bits[o + 2 * i] == ((start >> i) & 1)) && (bits[o + 2 * i + 1] == ((start >> i) & 1));
        }
        if (match) {
            offset = o;
        }
    }
    if (offset < 0) {
        return false;
    }

    auto damage = [&](int base, int singles, int pairs) {
        for (int i = 0; i < singles; i++) {
            bits[base + 2 * (i * 5 % 23)] ^= 1;
        }
        for (int i = 0; i < pairs; i++)
        {
            int p = (i * 7 + 3) % 23;
            bits[base + 2 * p] ^= 1;
            bits[base + 2 * p + 1] ^= 1;
        }
    };
    damage(offset, singles1, pairs1);
    damage(offset + 47, singles2, pairs2);

    D decoder(D::Normal);
    for (uint8_t b : bits) {
        decoder.feed(b);
    }
    decoder.flush();

    bool found = false;
    while (decoder.hasMessage())
    {
        D::Message m = decoder.takeMessage();
        found = found || (m.m_address == "123456");
    }
    return found;
}

void testStartCodeCopies()
{
    check(startCodeReceived(0, 0, 0, 0), "Start code: clean Word 1 and Word 2");
    check(startCodeReceived(0, 3, 0, 3), "Start code: 3 undetectable errors in each word, both decode");
    check(startCodeReceived(0, 0, 0, 8), "Start code: Word 2 destroyed (8 flipped pairs), detected on Word 1 alone");
    check(startCodeReceived(0, 8, 0, 0), "Start code: Word 1 destroyed (8 flipped pairs), detected on Word 2 alone");
    check(!startCodeReceived(0, 4, 0, 4), "Start code: both words beyond their code (4 flipped pairs each), rejected");

    // Every preamble value, single and batch: no rotation of a preamble word and no comma
    // on the word grid is taken for the start code, the message is found
    bool all = true;
    for (int index = 0; index < 10; index++)
    {
        for (int batch = 0; batch < 2; batch++)
        {
            char address[8];
            std::snprintf(address, sizeof(address), "%d00123", index);
            std::vector<G::TxMessage> sent = { tx(address, 2, G::Alphanumeric, "GRID") };
            G::EncodeOptions options;
            options.m_mode = batch ? G::Batch : G::Single;
            Decoded d = decode(G::encode(sent, options).m_bits, D::Normal, false, 0, 1);
            all = all && allMatch(d, sent) && (d.m_messages[0].m_preamble == index) && (d.m_messages[0].m_batch == (batch != 0));
        }
    }
    check(all, "Start code: found on the word grid after all 10 preamble values, single and batch");

    // Lock on a later preamble word: the first 10 preamble words are damaged
    std::vector<G::TxMessage> late = { tx("100123", 1, G::Numeric, "123") };
    G::EncodeResult enc = G::encode(late);
    for (int w = 0; w < 10; w++)
    {
        // 4 flipped pairs, at different bits in every word
        for (int p = 0; p < 4; p++)
        {
            int bit = (p * 5 + w * 7) % 23;
            enc.m_bits[28 + 46 * w + 2 * bit] ^= 1;
            enc.m_bits[28 + 46 * w + 2 * bit + 1] ^= 1;
        }
    }
    check(allMatch(decode(enc.m_bits, D::Normal, false, 0, 1), late), "Start code: found after a lock on the 13th preamble word");

    // One symbol of timing offset (a symbol lost before the start code): the start code
    // position is refined and the 600 bit/s blocks are still in step
    std::vector<G::TxMessage> slip = { tx("100123", 3, G::Alphanumeric, "TIMING OF THE BLOCKS") };
    G::EncodeResult encSlip = G::encode(slip);
    std::vector<uint8_t> lost(encSlip.m_bits);
    lost.erase(lost.begin() + 28 + 46 * 17 + 10);
    check(allMatch(decode(lost, D::Normal, false, 0, 1), slip), "Start code: one symbol lost in the last preamble word, blocks in step");
    std::vector<uint8_t> extra(encSlip.m_bits);
    extra.insert(extra.begin() + 28 + 46 * 17 + 10, extra[28 + 46 * 17 + 10]);
    check(allMatch(decode(extra, D::Normal, false, 0, 1), slip), "Start code: one symbol inserted in the last preamble word, blocks in step");
}

// Packs values of nbBits bits, least significant bit first, into message bits
std::vector<uint8_t> packValues(const std::vector<unsigned>& values, int nbBits, int& total)
{
    std::vector<uint8_t> packed((values.size() * nbBits + 7) / 8, 0);
    int pos = 0;

    for (unsigned v : values)
    {
        for (int j = 0; j < nbBits; j++, pos++)
        {
            if ((v >> j) & 1) {
                packed[pos >> 3] |= (uint8_t) (0x80 >> (pos & 7));
            }
        }
    }

    total = pos;
    return packed;
}

void testCharacterSets()
{
    int n;
    std::vector<uint8_t> p;
    std::string bar = G::tripleBar();

    p = packValues({ 1, 0xe, 2, 0xa, 0xa }, 4, n);
    check(G::decodeNumeric(p, n) == "1" + bar + "2", "Numeric 0xe is the triple bar, Null gives no character");
    p = packValues({ 1, 2, 3, 0xf }, 4, n);
    check(G::decodeNumeric(p, n) == "123E", "Numeric shift at the end is E");
    p = packValues({ 1, 0xf, 0xa, 0xa, 0xa }, 4, n);
    check(G::decodeNumeric(p, n) == "1E", "Numeric shift followed by Null (padding) is E");
    p = packValues({ 0xf, 0x0, 0xf, 0x5, 0xf, 0xe, 0xf, 0xf }, 4, n);
    check(G::decodeNumeric(p, n) == "A R<0x0F>", "Numeric shifted set: A, space, R and the spare as <0x0F>");
    p = packValues({ 0x21, 0x3c, 0x3f, 0x3e, 0x22, 0x3e, 0x3e }, 6, n);
    check(G::decodeAlphanumeric(p, n) == "A\r<0x3F>B", "Alphanumeric: upper case letters, CR/LF, spare as <0x3F>, Null/EOM gives no character");

    std::vector<G::TxMessage> sent = { tx("100123", 1, G::Numeric, ("12" + bar + "34").c_str()) };
    check(allMatch(decode(G::encode(sent).m_bits, D::Normal, false, 0, 1), sent), "Numeric triple bar in UTF-8 round trip");
    G::EncodeResult eq = G::encode({ tx("100123", 1, G::Numeric, "1=2*3") });
    Decoded deq = decode(eq.m_bits, D::Normal, false, 0, 1);
    check(!deq.m_messages.empty() && (G::decodeNumeric(deq.m_messages[0].m_rawData, deq.m_messages[0].m_rawBits) == "1" + bar + "2" + bar + "3"),
        "Numeric = and * are sent as the triple bar");
}

// Preamble, start code and address of a page (all 300 bit/s words) received through
// Gaussian noise, decided on the first symbol of each bit (hard) and on the sum of both
// symbols (soft)
void testSoftDecisions()
{
    const int trials = 200;
    const float sigma = 0.8f;
    int hardOk = 0;
    int softOk = 0;
    std::mt19937 rng(17);
    std::normal_distribution<float> gauss(0.0f, sigma);
    std::vector<G::TxMessage> sent = { tx("100123", 2, G::Tone) };
    G::EncodeResult enc = G::encode(sent);

    for (int t = 0; t < trials; t++)
    {
        D hard(D::Normal);
        D soft(D::Normal);

        for (uint8_t b : enc.m_bits)
        {
            float s = (b ? 1.0f : -1.0f) + gauss(rng);
            hard.feed(s > 0.0f ? 1 : 0);
            soft.feedSoft(s);
        }
        for (int i = 0; i < 200; i++)
        {
            float s = ((i & 1) ? 1.0f : -1.0f) + gauss(rng);
            hard.feed(s > 0.0f ? 1 : 0);
            soft.feedSoft(s);
        }
        hard.flush();
        soft.flush();

        Decoded dh, ds;
        while (hard.hasMessage()) {
            dh.m_messages.push_back(hard.takeMessage());
        }
        while (soft.hasMessage()) {
            ds.m_messages.push_back(soft.takeMessage());
        }
        // Only the words count: what follows the address is a 600 bit/s comma
        auto addressFound = [&](const Decoded& d) {
            return (d.m_messages.size() >= 1) && (d.m_messages[0].m_address == sent[0].m_address)
                && (d.m_messages[0].m_addressNumber == sent[0].m_addressNumber);
        };
        hardOk += addressFound(dh) ? 1 : 0;
        softOk += addressFound(ds) ? 1 : 0;
    }

    std::printf("      symbol noise sigma %.1f: %d/%d hard, %d/%d soft\n", sigma, hardOk, trials, softOk, trials);
    check(softOk > hardOk + trials / 4, "300 bit/s words decided on the sum of both soft symbols decode more pages than on one symbol");

    // Soft commas sum to noise: the fill commas of a batch and the noise after it never
    // give an address
    std::normal_distribution<float> light(0.0f, 0.3f);
    std::vector<G::TxMessage> batch = { tx("219789", 2, G::Numeric, "5551234"), tx("219456", 1, G::Tone), tx("219457", 4, G::Alphanumeric, "BATCH") };
    G::EncodeResult encBatch = G::encode(batch);
    int exact = 0;

    for (int t = 0; t < 200; t++)
    {
        D d(D::Normal);
        Decoded out;

        for (uint8_t b : encBatch.m_bits) {
            d.feedSoft((b ? 1.0f : -1.0f) + light(rng));
        }
        for (int i = 0; i < 2000; i++) {
            d.feedSoft(((rng() & 1) ? 1.0f : -1.0f) + light(rng));
        }
        d.flush();
        while (d.hasMessage()) {
            out.m_messages.push_back(d.takeMessage());
        }
        exact += allMatch(out, batch) ? 1 : 0;
    }
    check(exact == 200, "Soft symbols: 200 noisy batches give exactly their messages, nothing from the fill commas or the noise after");
}

// FM discriminator output for a bit stream, binary 1 = positive deviation, between random symbols
std::vector<float> makeWaveform(const std::vector<uint8_t>& bits, double sampleRate, bool inverted, float dc, float noise, int lead, int tail, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.0f, noise);
    std::vector<float> levels;

    for (int i = 0; i < lead; i++) {
        levels.push_back((rng() & 1) ? 1.0f : -1.0f);
    }
    for (uint8_t b : bits)
    {
        float level = b ? 1.0f : -1.0f;
        levels.push_back(inverted ? -level : level);
    }
    for (int i = 0; i < tail; i++) {
        levels.push_back((rng() & 1) ? 1.0f : -1.0f);
    }

    double samplesPerSymbol = sampleRate / RadioPagingGscReceiver::m_baud;
    float alpha = (float) (1.0 - std::exp(-1.0 / (0.1 * samplesPerSymbol)));
    std::vector<float> samples;
    float smoothed = 0.0f;

    for (double t = 0.0; (size_t) (t / samplesPerSymbol) < levels.size(); t += 1.0)
    {
        smoothed += alpha * (levels[(size_t) (t / samplesPerSymbol)] - smoothed);
        samples.push_back(smoothed + dc + gauss(rng));
    }

    return samples;
}

void receive(const std::vector<G::TxMessage>& sent, G::Mode mode, bool inverted, const char *name)
{
    G::EncodeOptions options;
    options.m_mode = mode;
    std::vector<float> samples = makeWaveform(G::encode(sent, options).m_bits, 38400.0, inverted, 0.06f, 0.15f, 300, 300, 23);
    RadioPagingGscReceiver receiver;
    Decoded d;
    bool locked = false;

    receiver.create(38400.0);
    for (float s : samples)
    {
        receiver.process(s);
        locked = locked || receiver.getStatus().m_locked;
    }
    receiver.flush();

    bool polarityOk = true;
    while (receiver.hasMessage())
    {
        D::Message m = receiver.takeMessage();
        polarityOk = polarityOk && (m.m_polarity == (inverted ? D::Inverted : D::Normal));
        d.m_messages.push_back(m);
    }

    std::string label = std::string("Receiver: ") + name + (inverted ? ", inverted" : ", normal");
    check(locked, (label + ": the demodulator locks on the preamble").c_str());
    check(allMatch(d, sent) && polarityOk, (label + ": all messages, from the decoder of the right polarity").c_str());
}

void testReceiver()
{
    std::vector<G::TxMessage> single = { tx("100123", 3, G::Alphanumeric, "RECEIVED OVER THE AIR") };
    std::vector<G::TxMessage> batch = {
        tx("219789", 2, G::Numeric, "5551234"),
        tx("219456", 1, G::Tone),
        tx("219457", 4, G::Alphanumeric, "BATCH"),
    };

    for (int inverted = 0; inverted < 2; inverted++)
    {
        receive(single, G::Single, inverted != 0, "single alphanumeric");
        receive(batch, G::Batch, inverted != 0, "batch of 3");
    }
}

} // namespace

int main()
{
    testCodes();
    testStartCodeCopies();
    testCharacterSets();
    testSoftDecisions();
    testReceiver();
    testAddresses();
    testStreaming();
    testErrors();
    testBatches();
    testNonBatterySaver();
    testInterpretation();
    testNoFalseLock();

    std::printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
