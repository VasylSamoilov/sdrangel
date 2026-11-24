///////////////////////////////////////////////////////////////////////////////////
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

#include <cstdio>
#include <cstring>
#include <vector>

#include "carrierwire.h"

using namespace modemcellular;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static void testBatch()
{
    CarrierBatch b;
    b.reset(123456789012ull, 480, {10001, 10005});
    for (size_t i = 0; i < b.samples.size(); i++) {
        b.samples[i] = static_cast<float>(i) * 0.5f;
    }
    b.levels = {-20.5f, -60.0f};

    const std::vector<uint8_t> wire = encodeBatch(b);
    CHECK(wireKind(wire.data(), wire.size()) == WireKind::Batch);

    CarrierBatch out;
    CHECK(decodeBatch(wire.data(), wire.size(), out));
    CHECK(out.sample == b.sample && out.length == 480);
    CHECK(out.carriers == b.carriers);
    CHECK(out.levels == b.levels);
    CHECK(out.samples == b.samples);

    // short, long, or of another kind: refused
    CHECK(!decodeBatch(wire.data(), wire.size() - 1, out));
    std::vector<uint8_t> longer = wire;
    longer.push_back(0);
    CHECK(!decodeBatch(longer.data(), longer.size(), out));
    CHECK(!decodeBatch(wire.data(), 0, out));
    CHECK(!decodeBatch(nullptr, 10, out));
    const std::vector<uint8_t> span = encodeSpan({1.0, 2.0});
    CHECK(!decodeBatch(span.data(), span.size(), out));

    // a damaged carrier count does not allocate the world
    std::vector<uint8_t> damaged = wire;
    damaged[13] = 0xff;
    damaged[14] = 0xff;
    damaged[15] = 0xff;
    damaged[16] = 0x7f;
    CHECK(!decodeBatch(damaged.data(), damaged.size(), out));

    // nor a header that names more samples than the packet has (within the limits)
    std::vector<uint8_t> big = wire;
    const int32_t length = 48000;
    const uint32_t many = 1024;
    std::memcpy(big.data() + 9, &length, 4);
    std::memcpy(big.data() + 13, &many, 4);
    big.resize(1 + 8 + 4 + 4 + 4 * 1024 + 1, 0);
    CarrierBatch fresh;
    CHECK(!decodeBatch(big.data(), big.size(), fresh));
    CHECK(fresh.samples.capacity() == 0);
    // over the limits: 1 s per block, 1024 carriers
    CarrierBatch tooLong;
    tooLong.reset(0, 48001, {1});
    std::vector<uint8_t> longWire = encodeBatch(tooLong);
    CHECK(!decodeBatch(longWire.data(), longWire.size(), out));

    // a batch of the controller has no levels
    CarrierBatch tx;
    tx.reset(0, 2, {1});
    tx.levels.clear();
    const std::vector<uint8_t> txWire = encodeBatch(tx);
    CHECK(decodeBatch(txWire.data(), txWire.size(), out));
    CHECK(out.carriers.size() == 1 && out.levels.size() == 1 && out.levels[0] == 0.0f);

    // no carriers
    CarrierBatch empty;
    empty.reset(5, 480, {});
    const std::vector<uint8_t> emptyWire = encodeBatch(empty);
    CHECK(decodeBatch(emptyWire.data(), emptyWire.size(), out));
    CHECK(out.carriers.empty() && out.sample == 5);
}

static void testOthers()
{
    WireSpan span{463.3e6, 960000.0};
    std::vector<uint8_t> wire = encodeSpan(span);
    WireSpan s;
    CHECK(wireKind(wire.data(), wire.size()) == WireKind::Span);
    CHECK(decodeSpan(wire.data(), wire.size(), s));
    CHECK(s.centre == span.centre && s.sampleRate == span.sampleRate);

    wire = encodeSpanRequest();
    CHECK(wire.size() == 1 && wireKind(wire.data(), wire.size()) == WireKind::SpanRequest);

    WireTuning tuning;
    tuning.lead = 4800;
    tuning.carriers = {{10001, -300000.0, 8755.0, 25000.0}, {10005, -200000.0, 8755.0, 25000.0}};
    wire = encodeTuning(tuning);
    WireTuning t;
    CHECK(decodeTuning(wire.data(), wire.size(), t));
    CHECK(t.lead == 4800 && t.carriers.size() == 2);
    CHECK(t.carriers[1].id == 10005 && t.carriers[1].offset == -200000.0);
    CHECK(t.carriers[0].passband == 8755.0 && t.carriers[0].spacing == 25000.0);
    tuning.carriers.clear();
    wire = encodeTuning(tuning);
    CHECK(decodeTuning(wire.data(), wire.size(), t) && t.carriers.empty());

    WireTxStatus status;
    status.carriers = {1, 5};
    status.fill = {4800, 4790};
    status.underruns = {0, 3};
    wire = encodeTxStatus(status);
    WireTxStatus st;
    CHECK(decodeTxStatus(wire.data(), wire.size(), st));
    CHECK(st.carriers == status.carriers && st.fill == status.fill && st.underruns == status.underruns);
    status.fill.pop_back();
    wire = encodeTxStatus(status);
    CHECK(wireKind(wire.data(), wire.size()) == WireKind::None);

    const uint8_t unknown[2] = {9, 0};
    CHECK(wireKind(unknown, 2) == WireKind::None);
}

static void testNames()
{
    WireNames names;
    names.networks = {9, 12, 3};
    names.names = {"Bench", "", "K\xc3\xb8" "benhavn"};
    std::vector<uint8_t> wire = encodeNames(names);
    WireNames out;
    CHECK(wireKind(wire.data(), wire.size()) == WireKind::Names);
    CHECK(decodeNames(wire.data(), wire.size(), out));
    CHECK(out.networks == names.networks && out.names == names.names);

    // cut short or with a byte too many
    for (size_t cut = 1; cut < wire.size(); cut++) {
        CHECK(!decodeNames(wire.data(), cut, out));
    }
    std::vector<uint8_t> longer = wire;
    longer.push_back(0);
    CHECK(!decodeNames(longer.data(), longer.size(), out));

    // the longest name passes, a longer one is refused
    names.names[1] = std::string(WireNames::kMaxName, 'x');
    wire = encodeNames(names);
    CHECK(decodeNames(wire.data(), wire.size(), out) && out.names[1].size() == WireNames::kMaxName);
    names.names[1] += 'x';
    wire = encodeNames(names);
    CHECK(wireKind(wire.data(), wire.size()) == WireKind::None);
    names.names.pop_back();
    wire = encodeNames(names);
    CHECK(wireKind(wire.data(), wire.size()) == WireKind::None);

    // none: the networks are gone
    wire = encodeNames(WireNames());
    CHECK(decodeNames(wire.data(), wire.size(), out) && out.networks.empty() && out.names.empty());
}

// The call meters of the carriers: every count, the energies and the live flag come through.
static void testSpeech()
{
    WireSpeech speech;
    for (int k = 0; k < 2; k++)
    {
        WireSpeech::Carrier c;
        c.carrier = 90010 + k;
        c.live = k == 0;
        for (int i = 0; i < SpeechStats::kBins; i++) {
            c.recent.bins[i] = static_cast<uint64_t>(i * 7 + k);
            c.call.bins[i] = static_cast<uint64_t>(i * 1000 + 3);
        }
        c.recent.speech = 1600;
        c.recent.pause = 14400;
        c.recent.atLimit = 5;
        c.recent.energy = 12.5;
        c.call.speech = 800000;
        c.call.energy = 1.0e5;
        for (int b = 0; b < SpeechStats::kBands; b++) {
            c.recent.bandEnergy[b] = 0.5 * (b + 1);
            c.call.bandEnergy[b] = 100.0 * (b + 1);
        }
        c.recent.peak = 1.567;
        speech.carriers.push_back(c);
    }
    std::vector<uint8_t> wire = encodeSpeech(speech);
    WireSpeech out;
    CHECK(wireKind(wire.data(), wire.size()) == WireKind::Speech);
    CHECK(decodeSpeech(wire.data(), wire.size(), out));
    CHECK(out.carriers.size() == 2);
    for (size_t k = 0; k < out.carriers.size() && k < 2; k++)
    {
        const WireSpeech::Carrier& a = speech.carriers[k];
        const WireSpeech::Carrier& b = out.carriers[k];
        CHECK(a.carrier == b.carrier && a.live == b.live);
        CHECK(a.recent.bins == b.recent.bins && a.call.bins == b.call.bins);
        CHECK(a.recent.speech == b.recent.speech && a.recent.pause == b.recent.pause && a.recent.atLimit == b.recent.atLimit);
        CHECK(a.recent.energy == b.recent.energy && a.call.energy == b.call.energy);
        CHECK(a.recent.bandEnergy == b.recent.bandEnergy && a.call.bandEnergy == b.call.bandEnergy);
        CHECK(a.recent.peak == b.recent.peak && a.call.speech == b.call.speech);
    }
    for (size_t cut = 1; cut < wire.size(); cut += 7) {
        CHECK(!decodeSpeech(wire.data(), cut, out));
    }
    wire.push_back(0);
    CHECK(!decodeSpeech(wire.data(), wire.size(), out));
    wire = encodeSpeech(WireSpeech());
    CHECK(decodeSpeech(wire.data(), wire.size(), out) && out.carriers.empty());
}

int main()
{
    testBatch();
    testOthers();
    testNames();
    testSpeech();

    if (g_failures == 0) {
        std::printf("all carrier wire tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
