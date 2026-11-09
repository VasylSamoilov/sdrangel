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

// Self test of the modemradiopaging primitives. Prints one line per check and
// returns non-zero if any check fails.

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include <string>

#include "radiopagingbch.h"
#include "radiopagingfskdemod.h"
#include "radiopagingpocsag.h"
#include "radiopagingpocsagdecoder.h"
#include "radiopagingpocsagreceiver.h"

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures++;
    }
}

void testBCH()
{
    std::mt19937 rng(1);
    bool encodeOk = true;
    bool singleOk = true;
    bool doubleOk = true;
    bool parityOk = true;

    for (int n = 0; n < 200; n++)
    {
        uint32_t data = rng() & 0x1fffff;
        uint32_t word = RadioPagingBCH::encode(data);

        RadioPagingBCH::Result clean = RadioPagingBCH::decode(word);
        if ((RadioPagingBCH::syndrome(word) != 0) || (RadioPagingBCH::data(word) != data)
            || (clean.m_bitErrors != 0) || clean.m_parityError) {
            encodeOk = false;
        }

        for (int i = 1; i < 32; i++)
        {
            RadioPagingBCH::Result r1 = RadioPagingBCH::decode(word ^ (1u << i));
            if ((r1.m_word != word) || (r1.m_bitErrors != 1)) {
                singleOk = false;
            }

            for (int j = i + 1; j < 32; j++)
            {
                RadioPagingBCH::Result r2 = RadioPagingBCH::decode(word ^ (1u << i) ^ (1u << j));
                if ((r2.m_word != word) || (r2.m_bitErrors != 2)) {
                    doubleOk = false;
                }
            }
        }

        RadioPagingBCH::Result rp = RadioPagingBCH::decode(word ^ 1u);
        if ((rp.m_word != word) || !rp.m_parityError || (rp.m_bitErrors != 0)) {
            parityOk = false;
        }
    }

    check(encodeOk, "BCH encode gives zero syndrome, even parity and the data back");
    check(singleOk, "BCH corrects every single bit error in bits 31-1");
    check(doubleOk, "BCH corrects every double bit error in bits 31-1");
    check(parityOk, "BCH reports a parity bit error and repairs it");
    check(RadioPagingBCH::reverse(0x80000001u) == 0x80000001u
        && RadioPagingBCH::reverse(0x00000002u) == 0x40000000u, "BCH bit reverse");
}

// Generates FM discriminator output for random symbols with smoothed transitions,
// DC offset, a symbol clock error and Gaussian noise, demodulates it and counts
// symbol errors after acquisition
int runFsk(double sampleRate, double symbolRate, int levels, double clockError, float noise, float dc, int nbSymbols)
{
    std::mt19937 rng(levels * 1000 + (int) symbolRate);
    std::normal_distribution<float> gauss(0.0f, noise);
    const float levelValues4[4] = {-1.0f, -1.0f/3.0f, 1.0f/3.0f, 1.0f};

    std::vector<int> sent(nbSymbols);
    for (auto& s : sent) {
        s = (int) (rng() % (unsigned) levels);
    }

    RadioPagingFskDemod demod;
    demod.create(sampleRate, symbolRate, levels);
    std::vector<int> received;

    double txSamplesPerSymbol = sampleRate / (symbolRate * (1.0 + clockError));
    double t = 0.0;
    float smoothed = 0.0f;
    // One pole smoothing, time constant of a tenth of a symbol, to give finite rise times
    float alpha = (float) (1.0 - std::exp(-1.0 / (0.1 * txSamplesPerSymbol)));
    int nbSamples = (int) (nbSymbols * txSamplesPerSymbol);

    for (int n = 0; n < nbSamples; n++)
    {
        int index = (int) (t / txSamplesPerSymbol);
        if (index >= nbSymbols) {
            break;
        }
        float target = (levels == 4) ? levelValues4[sent[index]] : (sent[index] ? 1.0f : -1.0f);
        smoothed += alpha * (target - smoothed);
        t += 1.0;

        RadioPagingFskDemod::Symbol symbol;
        if (demod.process(smoothed + dc + gauss(rng), symbol)) {
            received.push_back(symbol.m_value);
        }
    }

    // Find the alignment with fewest errors, ignoring the first symbols (acquisition)
    const int skip = 200;
    int best = nbSymbols;

    for (int lag = -4; lag <= 4; lag++)
    {
        int errors = 0;
        int compared = 0;

        for (int i = skip; i < (int) received.size(); i++)
        {
            int j = i + lag;
            if ((j < 0) || (j >= nbSymbols)) {
                continue;
            }
            compared++;
            if (received[i] != sent[j]) {
                errors++;
            }
        }

        if ((compared > nbSymbols / 2) && (errors < best)) {
            best = errors;
        }
    }

    return best;
}

void testFsk()
{
    char what[160];
    struct Case { double fs; double rate; int levels; };
    const Case cases[] = {
        {38400.0, 512.0, 2},
        {38400.0, 600.0, 2},
        {38400.0, 1200.0, 2},
        {38400.0, 1600.0, 2},
        {38400.0, 2400.0, 2},
        {38400.0, 3200.0, 2},
        {48000.0, 512.0, 2},      // 93.75 samples per symbol
        {38400.0, 1600.0, 4},
        {38400.0, 3200.0, 4},
        {44100.0, 3200.0, 4}      // 13.78 samples per symbol
    };

    for (const auto& c : cases)
    {
        int errors = runFsk(c.fs, c.rate, c.levels, 200e-6, 0.15f, 0.1f, 4000);
        std::snprintf(what, sizeof(what), "FSK %d-level %.0f sym/s at %.0f Hz, 200 ppm clock error, DC 0.1, noise 0.15: %d symbol errors",
            c.levels, c.rate, c.fs, errors);
        check(errors == 0, what);
    }
}

// FM discriminator output for a bit stream: binary 1 is a negative deviation for normal
// polarity. Lead and tail are random symbols before and after the transmission.
std::vector<float> makeWaveform(const std::vector<uint8_t>& bits, double sampleRate, double baud,
                                bool inverted, float dc, float noise, int lead, int tail, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> gauss(0.0f, noise);
    std::vector<float> levels;

    for (int i = 0; i < lead; i++) {
        levels.push_back((rng() & 1) ? 1.0f : -1.0f);
    }
    for (uint8_t b : bits)
    {
        float level = b ? -1.0f : 1.0f;
        levels.push_back(inverted ? -level : level);
    }
    for (int i = 0; i < tail; i++) {
        levels.push_back((rng() & 1) ? 1.0f : -1.0f);
    }

    double samplesPerSymbol = sampleRate / baud;
    float alpha = (float) (1.0 - std::exp(-1.0 / (0.1 * samplesPerSymbol)));
    std::vector<float> samples;
    float smoothed = 0.0f;
    double t = 0.0;

    for (;;)
    {
        size_t index = (size_t) (t / samplesPerSymbol);
        if (index >= levels.size()) {
            break;
        }
        smoothed += alpha * (levels[index] - smoothed);
        samples.push_back(smoothed + dc + gauss(rng));
        t += 1.0;
    }

    return samples;
}

bool sameMessages(const std::vector<RadioPagingPocsagDecoder::Message>& rx, const std::vector<RadioPagingPocsag::TxMessage>& tx, std::string& why);
std::vector<RadioPagingPocsag::TxMessage> pocsagTestMessages();

// The receiver of one speed, messages split by polarity
struct PocsagLeg
{
    RadioPagingPocsagReceiver m_receiver;
    std::vector<RadioPagingPocsagDecoder::Message> m_normalMessages;
    std::vector<RadioPagingPocsagDecoder::Message> m_invertedMessages;
    bool m_lockedInPreamble = false;

    PocsagLeg(double sampleRate, double baud) {
        m_receiver.create(sampleRate, (int) baud);
    }

    void process(float sample)
    {
        m_receiver.process(sample);

        RadioPagingPocsagReceiver::Status status = m_receiver.getStatus();
        if (status.m_preamble && status.m_locked) {
            m_lockedInPreamble = true;
        }

        while (m_receiver.hasMessage())
        {
            RadioPagingPocsagDecoder::Message m = m_receiver.takeMessage();
            if (m.m_polarity == RadioPagingPocsagDecoder::Inverted) {
                m_invertedMessages.push_back(m);
            } else {
                m_normalMessages.push_back(m);
            }
        }
    }
};

// Transmission with only the last preambleBits of the preamble, received at one speed
bool decodedWithShortPreamble(int preambleBits, double baud, bool& lockedInPreamble)
{
    std::vector<RadioPagingPocsag::TxMessage> messages = pocsagTestMessages();
    RadioPagingPocsag::EncodeResult result = RadioPagingPocsag::encode(messages);
    std::vector<uint8_t> bits(result.m_bits.begin() + (RadioPagingPocsag::m_preambleBits - preambleBits), result.m_bits.end());
    std::vector<float> samples = makeWaveform(bits, 38400.0, baud, false, 0.08f, 0.12f, 300, 300, 21);
    PocsagLeg leg(38400.0, baud);

    for (float s : samples) {
        leg.process(s);
    }

    lockedInPreamble = leg.m_lockedInPreamble;
    std::string why;
    return sameMessages(leg.m_normalMessages, messages, why);
}

void testPocsagPreambleLock()
{
    const double bauds[3] = {512.0, 1200.0, 2400.0};
    char what[160];

    for (double baud : bauds)
    {
        bool locked = false;
        bool ok = decodedWithShortPreamble(RadioPagingPocsag::m_preambleBits, baud, locked);
        std::snprintf(what, sizeof(what), "POCSAG %.0f baud: the demodulator locks on the preamble", baud);
        check(locked, what);
        std::snprintf(what, sizeof(what), "POCSAG %.0f baud: full preamble, all messages", baud);
        check(ok, what);

        ok = decodedWithShortPreamble(64, baud, locked);
        std::snprintf(what, sizeof(what), "POCSAG %.0f baud: only 64 bits of preamble received, locked %s, all messages %s",
            baud, locked ? "yes" : "no", ok ? "yes" : "no");
        check(ok, what);

        ok = decodedWithShortPreamble(0, baud, locked);
        std::snprintf(what, sizeof(what), "POCSAG %.0f baud: no preamble at all, all messages %s", baud, ok ? "yes" : "no");
        check(ok, what);
    }
}

bool sameMessages(const std::vector<RadioPagingPocsagDecoder::Message>& rx, const std::vector<RadioPagingPocsag::TxMessage>& tx, std::string& why)
{
    if (rx.size() != tx.size())
    {
        why = "decoded " + std::to_string(rx.size()) + " messages, sent " + std::to_string(tx.size()) + " (";
        for (const auto& r : rx) {
            why += std::to_string(r.m_address) + "/" + std::to_string(r.m_rawBits) + "b ";
        }
        why += ")";
        return false;
    }

    for (size_t i = 0; i < tx.size(); i++)
    {
        const auto& r = rx[i];
        const auto& t = tx[i];
        RadioPagingPocsag::Interpretation interp = RadioPagingPocsag::interpret(r.m_rawData, r.m_rawBits);
        std::string expectedType = (t.m_type == RadioPagingPocsag::Tone) ? "tone_only"
            : (t.m_type == RadioPagingPocsag::Numeric ? "numeric" : "alphanumeric");

        if ((r.m_address != t.m_address) || (r.m_function != t.m_function)
            || (interp.m_type != expectedType) || (interp.m_text != t.m_text))
        {
            why = "message " + std::to_string(i) + " differs: address " + std::to_string(r.m_address)
                + " function " + std::to_string(r.m_function) + " type " + interp.m_type
                + " text '" + RadioPagingPocsag::escapeControl(interp.m_text) + "'";
            return false;
        }
    }

    return true;
}

std::vector<RadioPagingPocsag::TxMessage> pocsagTestMessages()
{
    std::vector<RadioPagingPocsag::TxMessage> messages;
    RadioPagingPocsag::TxMessage m;

    m.m_address = 1234567; m.m_function = 0; m.m_type = RadioPagingPocsag::Numeric; m.m_text = "0123456789 -";
    messages.push_back(m);
    m.m_address = 1005432; m.m_function = 2; m.m_type = RadioPagingPocsag::Alphanumeric; m.m_text = "Hello World 123";
    messages.push_back(m);
    m.m_address = 1005433; m.m_function = 1; m.m_type = RadioPagingPocsag::Tone; m.m_text = "";
    messages.push_back(m);
    m.m_address = 1005440; m.m_function = 3; m.m_type = RadioPagingPocsag::Alphanumeric;
    m.m_text = "";
    for (int i = 0; i < 150; i++) {
        m.m_text.push_back((char) ('a' + (i % 26)));
    }
    messages.push_back(m);
    m.m_address = 100; m.m_function = 0; m.m_type = RadioPagingPocsag::Alphanumeric; m.m_text = "A\r\nB";
    messages.push_back(m);

    return messages;
}

void testPocsagCodec()
{
    std::vector<RadioPagingPocsag::TxMessage> messages = pocsagTestMessages();
    RadioPagingPocsag::EncodeResult result = RadioPagingPocsag::encode(messages);

    check(result.m_ok, "POCSAG encode succeeds");
    check(result.m_codewords.size() % 17 == 0, "POCSAG output is a whole number of 17 codeword batches");
    check(result.m_bits.size() == (size_t) (576 + 32 * result.m_codewords.size()), "POCSAG output bit count is preamble plus codewords");

    bool syncOk = true;
    for (size_t i = 0; i < result.m_codewords.size(); i += 17) {
        syncOk = syncOk && (result.m_codewords[i] == RadioPagingPocsag::m_syncWord);
    }
    check(syncOk, "POCSAG batches start with the sync codeword");

    // The address of the first message is in frame 7, so slot 14 or 15 of the first batch
    uint32_t first = result.m_codewords[1 + 14];
    check((first & 0x80000000u) == 0 && (RadioPagingBCH::data(first) >> 2) == (1234567u >> 3), "POCSAG address is placed in its frame");

    RadioPagingPocsag::TxMessage bad;
    bad.m_address = 5; bad.m_type = RadioPagingPocsag::Numeric; bad.m_text = "12a";
    check(!RadioPagingPocsag::encode({bad}).m_ok, "POCSAG rejects a character that is not numeric");
    bad.m_address = 2007664; bad.m_text = "12";
    check(!RadioPagingPocsag::encode({bad}).m_ok, "POCSAG rejects an address that looks like the idle codeword");
    check(RadioPagingPocsag::escapeControl(std::string("A\r\n\x7f") + "B") == "A<0x0D><0x0A><0x7F>B", "control characters are escaped");
}

void testPocsagLink()
{
    std::vector<RadioPagingPocsag::TxMessage> messages = pocsagTestMessages();
    RadioPagingPocsag::EncodeResult clean = RadioPagingPocsag::encode(messages);
    const double fs = 38400.0;
    const double bauds[3] = {512.0, 1200.0, 2400.0};
    char what[200];

    // One bit error in every codeword, in different positions, the sync codewords included
    std::vector<uint8_t> damaged = clean.m_bits;
    for (size_t i = 0; i < clean.m_codewords.size(); i++) {
        damaged[576 + 32 * i + (i * 7) % 32] ^= 1;
    }

    for (int variant = 0; variant < 2; variant++)
    {
        for (int pol = 0; pol < 2; pol++)
        {
            for (int s = 0; s < 3; s++)
            {
                bool inverted = (pol == 1);
                const std::vector<uint8_t>& bits = (variant == 0) ? clean.m_bits : damaged;
                std::vector<float> samples = makeWaveform(bits, fs, bauds[s], inverted, 0.08f, 0.12f, 300, 300, 7 + s);

                // All three speeds listen to the same signal
                PocsagLeg legs[3] = {PocsagLeg(fs, bauds[0]), PocsagLeg(fs, bauds[1]), PocsagLeg(fs, bauds[2])};
                for (float sample : samples) {
                    for (auto& leg : legs) {
                        leg.process(sample);
                    }
                }

                std::string why;
                const auto& right = inverted ? legs[s].m_invertedMessages : legs[s].m_normalMessages;
                const auto& wrongPol = inverted ? legs[s].m_normalMessages : legs[s].m_invertedMessages;
                bool ok = sameMessages(right, messages, why);
                std::snprintf(what, sizeof(what), "POCSAG %s, %.0f baud, %s polarity: all messages decoded%s%s",
                    variant ? "with a bit error in every codeword" : "clean", bauds[s], inverted ? "inverted" : "normal",
                    ok ? "" : " -- ", why.c_str());
                check(ok, what);

                bool others = wrongPol.empty();
                for (int o = 0; o < 3; o++)
                {
                    if (o != s) {
                        others = others && legs[o].m_normalMessages.empty() && legs[o].m_invertedMessages.empty();
                    }
                }
                std::snprintf(what, sizeof(what), "POCSAG %.0f baud, %s polarity: other speeds and the other polarity stay silent",
                    bauds[s], inverted ? "inverted" : "normal");
                check(others, what);

                if (variant == 1)
                {
                    int corrected = 0;
                    for (const auto& m : right) {
                        corrected += m.m_errorsCorrected;
                    }
                    check(corrected > 0, "POCSAG reports corrected bit errors");
                }
            }
        }
    }
}

// Feeds bits straight to a decoder (normal polarity: binary 1 is symbol 0) and returns the messages
std::vector<RadioPagingPocsagDecoder::Message> decodeBits(const std::vector<uint8_t>& bits)
{
    RadioPagingPocsagDecoder decoder(RadioPagingPocsagDecoder::Normal);
    std::vector<RadioPagingPocsagDecoder::Message> messages;

    for (uint8_t b : bits)
    {
        decoder.feed(b ? 0 : 1);
        while (decoder.hasMessage()) {
            messages.push_back(decoder.takeMessage());
        }
    }
    // A few bits of silence after the transmission
    for (int i = 0; i < 64; i++)
    {
        decoder.feed(i & 1);
        while (decoder.hasMessage()) {
            messages.push_back(decoder.takeMessage());
        }
    }

    return messages;
}

void flipBits(std::vector<uint8_t>& bits, size_t codewordIndex, int count)
{
    for (int i = 0; i < count; i++) {
        bits[RadioPagingPocsag::m_preambleBits + 32 * codewordIndex + 3 + 7 * i] ^= 1;
    }
}

bool hasAddressText(const std::vector<RadioPagingPocsagDecoder::Message>& messages, uint32_t address, const std::string& text)
{
    for (const auto& m : messages)
    {
        if ((m.m_address == address) && (RadioPagingPocsag::interpret(m.m_rawData, m.m_rawBits).m_text == text)) {
            return true;
        }
    }
    return false;
}

void testPocsagStandardRules()
{
    // Encoder: an idle or address codeword between a message and the next message's address,
    // and an idle codeword at the end of the transmission
    std::vector<RadioPagingPocsag::TxMessage> messages = pocsagTestMessages();
    RadioPagingPocsag::TxMessage m;
    m.m_address = 9; m.m_function = 3; m.m_type = RadioPagingPocsag::Alphanumeric; m.m_text = "abc";
    messages.push_back(m);
    m.m_address = 10; m.m_text = "def";     // Frame 2, right after the previous message
    messages.push_back(m);
    RadioPagingPocsag::EncodeResult result = RadioPagingPocsag::encode(messages);

    bool separated = true;
    uint32_t previous = RadioPagingPocsag::m_idleWord;
    for (uint32_t w : result.m_codewords)
    {
        if (w == RadioPagingPocsag::m_syncWord) {
            continue;
        }
        bool isAddress = ((w & 0x80000000u) == 0) && (w != RadioPagingPocsag::m_idleWord);
        if (isAddress && ((previous & 0x80000000u) != 0)) {
            separated = false;
        }
        previous = w;
    }
    check(separated, "POCSAG encoder: no address codeword directly after a message codeword");
    check(result.m_codewords.back() == RadioPagingPocsag::m_idleWord, "POCSAG encoder: the last codeword is an idle codeword");

    // A message that fills a batch exactly is followed by a batch of idle codewords
    RadioPagingPocsag::TxMessage full;
    full.m_address = 0; full.m_type = RadioPagingPocsag::Numeric; full.m_text = std::string(75, '1');
    RadioPagingPocsag::EncodeResult fullResult = RadioPagingPocsag::encode({full});
    check(fullResult.m_codewords.back() == RadioPagingPocsag::m_idleWord, "POCSAG encoder: a full batch is followed by idle codewords");

    // Decoder: all messages from bits alone
    std::vector<RadioPagingPocsagDecoder::Message> all = decodeBits(result.m_bits);
    check(all.size() == messages.size(), "POCSAG decoder: all messages of a multi-batch transmission");

    // Joining during a transmission, without the preamble: start at the second batch
    std::vector<uint8_t> late(result.m_bits.begin() + RadioPagingPocsag::m_preambleBits + 17 * 32 - 5, result.m_bits.end());
    std::vector<RadioPagingPocsagDecoder::Message> joined = decodeBits(late);
    bool lateOk = hasAddressText(joined, 9, "abc") && hasAddressText(joined, 10, "def") && hasAddressText(joined, 100, "A\r\nB");
    check(lateOk, "POCSAG decoder: joining at the second batch without preamble decodes the later messages");

    // No preamble at all: a single batch is decoded because it is valid by itself
    RadioPagingPocsag::TxMessage one;
    one.m_address = 77; one.m_type = RadioPagingPocsag::Alphanumeric; one.m_text = "single";
    RadioPagingPocsag::EncodeOptions noPreamble;
    noPreamble.m_preamble = false;
    RadioPagingPocsag::EncodeResult single = RadioPagingPocsag::encode({one}, noPreamble);
    check(single.m_codewords.size() == 17, "POCSAG encoder: a short message is a single batch");
    std::vector<uint8_t> singleBits(single.m_bits);
    singleBits.insert(singleBits.begin(), 40, 0);
    check(hasAddressText(decodeBits(singleBits), 77, "single"), "POCSAG decoder: a valid single batch is decoded without preamble");

    // A single batch with an undecodable codeword has no confirming sync codeword: dropped
    std::vector<uint8_t> badSingle = single.m_bits;
    for (int i = 0; i < 3; i++) {
        badSingle[32 * 15 + 2 + 9 * i] ^= 1;    // 3 errors in the last idle codeword
    }
    check(decodeBits(badSingle).empty(), "POCSAG decoder: an unconfirmed batch with an undecodable codeword is not decoded");

    // The same damage in the last batch of a multi-batch transmission: its sync codeword is
    // confirmed by the previous one, the batch is decoded
    std::vector<uint8_t> badLast = result.m_bits;
    flipBits(badLast, result.m_codewords.size() - 1, 3);
    check(decodeBits(badLast).size() == messages.size(), "POCSAG decoder: the last batch of a confirmed transmission is decoded with an undecodable codeword");

    // Two successive undecodable codewords end a message; the next messages are still decoded
    RadioPagingPocsag::TxMessage longMessage;
    longMessage.m_address = 1000; longMessage.m_type = RadioPagingPocsag::Alphanumeric;
    longMessage.m_text = std::string(60, 'x');
    RadioPagingPocsag::TxMessage after;
    after.m_address = 1001; after.m_type = RadioPagingPocsag::Alphanumeric; after.m_text = "after";
    RadioPagingPocsag::EncodeResult twoResult = RadioPagingPocsag::encode({longMessage, after});
    std::vector<uint8_t> two = twoResult.m_bits;
    // Codewords of the first batch: sync (index 0), address of 1000 in frame 0 (index 1),
    // message codewords from index 2
    flipBits(two, 4, 3);
    flipBits(two, 5, 3);
    std::vector<RadioPagingPocsagDecoder::Message> twoMessages = decodeBits(two);
    bool cut = false;
    for (const auto& msg : twoMessages)
    {
        if (msg.m_address == 1000) {
            cut = msg.m_truncated && (msg.m_errorsUncorrectable == 2) && (msg.m_rawBits == 2 * 20 + 20);
        }
    }
    check(cut, "POCSAG decoder: two successive undecodable codewords end the message");
    check(hasAddressText(twoMessages, 1001, "after"), "POCSAG decoder: the message after them is decoded");

    // A single undecodable codeword stays in the message
    std::vector<uint8_t> oneBad = twoResult.m_bits;
    flipBits(oneBad, 4, 3);
    bool kept = false;
    for (const auto& msg : decodeBits(oneBad))
    {
        if (msg.m_address == 1000) {
            kept = !msg.m_truncated && (msg.m_errorsUncorrectable == 1);
        }
    }
    check(kept, "POCSAG decoder: one undecodable codeword is kept in the message");
}

void testBerWindow()
{
    RadioPagingBerWindow w;
    check(!w.valid(), "BER window: no codeword, no value");

    for (int i = 0; i < 10; i++) {
        w.add(0, 32, false);
    }
    check(w.valid() && (w.ber() == 0.0f) && (w.m_uncorrectable == 0), "BER window: clean codewords give 0");

    w.add(2, 32, false);
    check((w.m_errorBits == 2) && (w.m_uncorrectable == 0), "BER window: corrected bits are counted, nothing uncorrectable");

    w.add(0, 32, true);
    check((w.m_errorBits == 2 + RadioPagingBerWindow::m_uncorrectableBits) && (w.m_uncorrectable == 1),
        "BER window: an uncorrectable codeword counts its capped errors and itself");

    for (int i = 0; i < 4 * RadioPagingBerWindow::m_windowCodewords; i++) {
        w.add(0, 32, false);
    }
    check((w.m_uncorrectable == 0) && (w.m_errorBits == 0), "BER window: old errors leave the window");

    // POCSAG decoder: a clean batch gives a valid window without errors
    RadioPagingPocsagDecoder d(RadioPagingPocsagDecoder::Normal);
    std::vector<uint32_t> words = { RadioPagingPocsag::m_syncWord };
    for (int i = 0; i < RadioPagingPocsag::m_codewordsPerBatch; i++) {
        words.push_back(RadioPagingPocsag::m_idleWord);
    }
    words.push_back(RadioPagingPocsag::m_syncWord);
    for (uint32_t word : words)
    {
        for (int b = 31; b >= 0; b--) {
            d.feed(((word >> b) & 1) ? 0 : 1);  // Binary 1 is a negative deviation for normal polarity
        }
    }
    RadioPagingBerWindow ber = d.getStatus().m_ber;
    check(ber.valid() && (ber.m_errorBits == 0) && (ber.m_uncorrectable == 0), "POCSAG decoder: BER of a clean batch is 0");
}

// The same transmission twice: the second copy of each message is marked duplicate; the same
// transmission on the other polarity is another network: not a duplicate
void testPocsagDuplicates()
{
    std::vector<RadioPagingPocsag::TxMessage> messages = pocsagTestMessages();
    RadioPagingPocsag::EncodeResult result = RadioPagingPocsag::encode(messages);
    std::vector<float> first = makeWaveform(result.m_bits, 38400.0, 1200.0, false, 0.08f, 0.12f, 300, 2000, 31);
    std::vector<float> again = makeWaveform(result.m_bits, 38400.0, 1200.0, false, 0.08f, 0.12f, 300, 2000, 32);
    std::vector<float> other = makeWaveform(result.m_bits, 38400.0, 1200.0, true, 0.08f, 0.12f, 300, 2000, 33);
    RadioPagingPocsagReceiver receiver;
    receiver.create(38400.0, 1200);
    std::vector<RadioPagingPocsagDecoder::Message> out[3];
    const std::vector<float> *parts[3] = { &first, &again, &other };

    for (int p = 0; p < 3; p++)
    {
        for (float s : *parts[p])
        {
            receiver.process(s);
            while (receiver.hasMessage()) {
                out[p].push_back(receiver.takeMessage());
            }
        }
    }

    bool firstNew = !out[0].empty();
    bool againDup = (out[1].size() == out[0].size());
    bool otherNew = (out[2].size() == out[0].size());

    for (const auto& m : out[0]) { firstNew = firstNew && !m.m_duplicate; }
    for (const auto& m : out[1]) { againDup = againDup && m.m_duplicate; }
    for (const auto& m : out[2]) { otherNew = otherNew && !m.m_duplicate && (m.m_polarity == RadioPagingPocsagDecoder::Inverted); }

    check(firstNew, "POCSAG duplicates: first copy is not a duplicate");
    check(againDup, "POCSAG duplicates: second copy is output again, marked duplicate");
    check(otherNew, "POCSAG duplicates: the same messages on the other polarity are not duplicates");
}

} // namespace

int main()
{
    testBCH();
    testPocsagCodec();
    testPocsagLink();
    testPocsagStandardRules();
    testPocsagPreambleLock();
    testFsk();
    testBerWindow();
    testPocsagDuplicates();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
