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

#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "ampsbch.h"
#include "ampschannel.h"
#include "ampsmessages.h"
#include "ampsstream.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kPi = 3.14159265358979323846;
static const double kRate = 48000.0;
static const int kBlock = 480;

// The forward control channel as a phone reads it: after the sync, 5 repeats of words A and B,
// 44 bits each with a busy-idle bit after every 10 (osmocom's amps_decode_bits_focc); word A by
// the vote of its repeats.
class FoccReader
{
public:
    FoccReader() :
        m_demod(kRate, kFskBitRate, kFskDeviation)
    {
        m_demod.setBitSink([this](int bit, double, double) { this->bit(bit); });
    }
    void process(const float* s, int n) { m_demod.process(s, n); }
    std::vector<uint64_t> words;

private:
    void bit(int b)
    {
        if (!m_inFrame)
        {
            m_sync = ((m_sync << 1) | static_cast<uint32_t>(b)) & 0x7ff;
            if (m_sync == kWordSync)
            {
                m_inFrame = true;
                m_bits.clear();
                m_sync = 0x555;
                m_demod.startFrame();
            }
            return;
        }
        m_bits.push_back(static_cast<uint8_t>(b));
        if (m_bits.size() < 441) {
            return;
        }
        // the B/I bit after the sync, then 10 words of 44
        uint64_t a[5];
        for (int w = 0; w < 10; w++)
        {
            uint64_t word = 0;
            for (int j = 0; j < 44; j++)
            {
                if (j % 11 == 10) {
                    continue;
                }
                word = (word << 1) | m_bits[1 + w * 44 + j];
            }
            if ((w & 1) == 0) {
                a[w / 2] = word;
            }
        }
        uint64_t voted = 0;
        for (int b2 = 0; b2 < 40; b2++)
        {
            int ones = 0;
            for (int r = 0; r < 5; r++) {
                ones += static_cast<int>((a[r] >> b2) & 1);
            }
            if (ones >= 3) {
                voted |= 1ull << b2;
            }
        }
        if (bchCheck(voted, kForwardMessageBits)) {
            words.push_back(voted);
        }
        m_inFrame = false;
        m_demod.endFrame();
    }

    ManchesterDemodulator m_demod;
    uint32_t m_sync = 0x555;
    bool m_inFrame = false;
    std::vector<uint8_t> m_bits;
};

// A phone sending bits on the reverse control channel
class PhoneTx
{
public:
    PhoneTx() :
        m_mod(kRate, kFskBitRate, kFskDeviation)
    {
        m_mod.setBitSource([this]() {
            if (m_bits.empty()) {
                return -1;
            }
            const int b = m_bits.front();
            m_bits.pop_front();
            return b;
        });
    }
    void send(const std::vector<uint8_t>& bits) { m_bits.insert(m_bits.end(), bits.begin(), bits.end()); }
    void render(float* s, int n)
    {
        const int done = m_mod.render(s, n, false);
        std::fill(s + done, s + n, 0.0f);
    }

private:
    ManchesterModulator m_mod;
    std::deque<uint8_t> m_bits;
};

static const uint32_t kMin1 = ReccAssembler::min1Of("2177526");
static const uint16_t kMin2 = ReccAssembler::min2Of("216");

static std::vector<uint64_t> registrationWords()
{
    return {encodeWord(words::kReccA, {{"F", 1}, {"NAWC", 2}, {"T", 1}, {"S", 1}, {"E", 1}, {"SCM", 0xa}, {"MIN1", kMin1}}),
        encodeWord(words::kReccB, {{"NAWC", 1}, {"ORDQ", 3}, {"ORDER", kOrderRegistration}, {"MIN2", kMin2}}),
        encodeWord(words::kReccSerial, {{"NAWC", 0}, {"SERIAL", 0xe0ec3aac}})};
}

static std::vector<uint64_t> originationWords()
{
    return {encodeWord(words::kReccA, {{"F", 1}, {"NAWC", 3}, {"T", 1}, {"S", 1}, {"E", 1}, {"SCM", 0xa}, {"MIN1", kMin1}}),
        encodeWord(words::kReccB, {{"NAWC", 2}, {"ORDER", kOrderPage}, {"MIN2", kMin2}}),
        encodeWord(words::kReccSerial, {{"NAWC", 1}, {"SERIAL", 0xe0ec3aac}}),
        encodeWord(words::kReccDigits, {{"NAWC", 0}, {"DIGIT 1", 5}, {"DIGIT 2", 5}, {"DIGIT 3", 5}})};
}

struct Bench
{
    modemcellular::SampleClock clock{kRate};
    AmpsSystem system;
    AmpsChannel cc;
    AmpsChannel vc;
    FoccReader focc;
    PhoneTx phone;
    SatDetector phoneSat{kRate};        // the phone hears the SAT of the voice channel
    SatTone phoneSatTone{kRate, 0};     // and transponds it
    bool onVoice = false;
    bool tone = false;
    int heardSat = -2;
    std::vector<std::string> calls;
    int registrations = 0;
    int clears = 0;
    double toneAt = 0.0;
    double vc1kHz = 0.0;
    double vcPower = 0.0;
    double t = 0.0;

    static AmpsSystemConfig config()
    {
        AmpsSystemConfig c;
        c.si.sid = 54;
        c.channels = {{353, AmpsChannelType::ControlPaging, 0}, {356, AmpsChannelType::Voice, 0}};
        return c;
    }

    Bench() :
        system(clock, config()),
        cc(system, 0, kRate),
        vc(system, 1, kRate)
    {
        system.setRegistrationHandler([this](const AmpsSubscriber&, bool) { registrations++; });
        system.setCallHandler([this](const AmpsSubscriber&, const std::string& d) {
            calls.push_back(d);
            return true;
        });
        system.setClearHandler([this](const AmpsSubscriber&) { clears++; });
        // the network sends a 1 kHz tone at the speech level
        system.setAudioHandlers(nullptr, [this](const AmpsSubscriber&, float* s, int n) {
            for (int i = 0; i < n; i++) {
                s[i] = static_cast<float>(std::sin(2.0 * kPi * 1000.0 * (m_callSamples++) / 8000.0));
            }
        });
        phoneSat.setSatSink([this](int scc, double) { heardSat = scc; });
    }

    void run(double seconds)
    {
        std::vector<float> in(kBlock), out(kBlock);
        for (int b = 0; b < static_cast<int>(seconds * 100.0); b++)
        {
            // the phone towards the base
            phone.render(in.data(), kBlock);
            cc.receive(in.data(), kBlock);
            std::fill(in.begin(), in.end(), 0.0f);
            if (onVoice)
            {
                phoneSatTone.add(in.data(), kBlock);
                for (int i = 0; tone && i < kBlock; i++) {
                    in[i] += static_cast<float>(kFskDeviation * std::sin(2.0 * kPi * kSignallingToneHz * (t + i / kRate)));
                }
            }
            vc.receive(in.data(), kBlock);
            // the base towards the phone
            cc.render(out.data(), kBlock);
            focc.process(out.data(), kBlock);
            vc.render(out.data(), kBlock);
            phoneSat.process(out.data(), kBlock);
            double re = 0.0, im = 0.0, p = 0.0;
            for (int i = 0; i < kBlock; i++)
            {
                re += out[i] * std::cos(2.0 * kPi * 1000.0 * (t + i / kRate));
                im += out[i] * std::sin(2.0 * kPi * 1000.0 * (t + i / kRate));
                p += out[i] * out[i];
            }
            vc1kHz = 2.0 * std::sqrt(re * re + im * im) / kBlock;
            vcPower = p / kBlock;
            clock.advance(kBlock);
            system.run();
            t += kBlock / kRate;
        }
    }

    uint64_t m_callSamples = 0;
};

static bool heardWord(const std::vector<uint64_t>& w, const std::function<bool(uint64_t)>& match)
{
    for (uint64_t x : w)
    {
        if (match(x)) {
            return true;
        }
    }
    return false;
}

// The whole of a phone's call through the channels: the overhead train, a registration and its
// acknowledgement, an origination, the designation, the voice channel with SAT, speech, the
// phone's release by its tone
static void testCall()
{
    Bench b;

    b.run(1.0);
    CHECK(heardWord(b.focc.words, [](uint64_t w) {
        return wordField(words::kSystemParameter1, w, "OHD") == kOhdSystemParameter1 && wordField(words::kSystemParameter1, w, "SID1") == 27;
    }));
    CHECK(heardWord(b.focc.words, [](uint64_t w) { return wordField(words::kControlFiller, w, "OHD") == kOhdControlFiller; }));

    b.phone.send(reccMessage(0, registrationWords()));
    b.run(1.0);
    CHECK(b.registrations == 1);
    CHECK(heardWord(b.focc.words, [](uint64_t w) {
        return wordField(words::kExtendedAddressOrder, w, "T1T2") == kT1T2Next && wordField(words::kExtendedAddressOrder, w, "SCC") == 3
            && wordField(words::kExtendedAddressOrder, w, "ORDER") == kOrderRegistration && wordField(words::kExtendedAddressOrder, w, "MIN2") == kMin2;
    }));

    b.focc.words.clear();
    b.phone.send(reccMessage(0, originationWords()));
    b.run(1.0);
    CHECK(b.calls.size() == 1 && !b.calls.empty() && b.calls[0] == "555");
    CHECK(heardWord(b.focc.words, [](uint64_t w) {
        return wordField(words::kExtendedAddressChannel, w, "T1T2") == kT1T2Next && wordField(words::kExtendedAddressChannel, w, "SCC") == 0
            && wordField(words::kExtendedAddressChannel, w, "CHAN") == 356;
    }));

    // the phone goes to the voice channel: it hears the SAT and transponds it
    b.run(0.5);
    CHECK(b.heardSat == 0);
    b.onVoice = true;
    b.run(1.0);
    CHECK(b.system.mode(1) == AmpsMode::Audio);
    // the network's 1 kHz at the speech level, about 1.27 of 2.9 kHz after gain, compressor and emphasis
    CHECK(b.vc1kHz > 0.9 * kSpeechDeviation && b.vc1kHz < 1.6 * kSpeechDeviation);

    // its tone of 1.7 s releases the call, the voice channel goes off
    b.tone = true;
    b.run(1.7);
    b.tone = false;
    b.run(0.6);
    CHECK(b.clears == 1 && b.system.mode(1) == AmpsMode::Off);
    b.onVoice = false;
    b.run(0.1);
    CHECK(b.vcPower == 0.0);
}

int main()
{
    testCall();

    if (g_failures == 0) {
        std::printf("all channel tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
