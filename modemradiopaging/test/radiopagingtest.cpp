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

// A demodulator and a decoder per polarity for one speed
struct PocsagLeg
{
    RadioPagingFskDemod m_demod;
    RadioPagingPocsagDecoder m_normal{RadioPagingPocsagDecoder::Normal};
    RadioPagingPocsagDecoder m_inverted{RadioPagingPocsagDecoder::Inverted};
    std::vector<RadioPagingPocsagDecoder::Message> m_normalMessages;
    std::vector<RadioPagingPocsagDecoder::Message> m_invertedMessages;

    PocsagLeg(double sampleRate, double baud) {
        m_demod.create(sampleRate, baud, 2);
    }

    void process(float sample)
    {
        RadioPagingFskDemod::Symbol symbol;

        if (m_demod.process(sample, symbol))
        {
            m_normal.feed(symbol.m_value);
            m_inverted.feed(symbol.m_value);
            m_demod.setLocked(m_normal.isSynced() || m_inverted.isSynced());

            while (m_normal.hasMessage()) {
                m_normalMessages.push_back(m_normal.takeMessage());
            }
            while (m_inverted.hasMessage()) {
                m_invertedMessages.push_back(m_inverted.takeMessage());
            }
        }
    }
};

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

} // namespace

int main()
{
    testBCH();
    testPocsagCodec();
    testPocsagLink();
    testFsk();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
