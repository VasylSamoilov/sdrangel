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

// Self test of the radio paging demodulator sink: POCSAG, FLEX and GSC transmissions are
// FM modulated one after the other into one IQ stream, received through the channel
// sink with all legs running, and the records are compared with what was sent.

#include <cmath>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "dsp/dsptypes.h"
#include "util/messagequeue.h"
#include "maincore.h"

#include "radiopagingpocsag.h"
#include "radiopagingflex.h"
#include "radiopagingflexscheduler.h"
#include "radiopaginggsc.h"

#include "radiopagingdemodsink.h"

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

const double sampleRate = RadioPagingDemodSettings::m_channelSampleRate;

// FM modulator: instantaneous frequency in Hz to IQ samples, with noise
struct Modulator
{
    double m_phase = 0.0;
    float m_smoothed = 0.0f;
    std::mt19937 m_rng{3};
    std::normal_distribution<float> m_noise{0.0f, 0.05f};
    SampleVector m_samples;

    void sample(double frequency)
    {
        m_phase += 2.0 * M_PI * frequency / sampleRate;
        double a = 0.5 * SDR_RX_SCALED;
        m_samples.push_back(Sample(
            (FixReal) (a * (std::cos(m_phase) + m_noise(m_rng))),
            (FixReal) (a * (std::sin(m_phase) + m_noise(m_rng)))));
    }

    // Levels -1 to 1 at the baud rate, smoothed, times the deviation
    void symbols(const std::vector<float>& levels, double baud, double deviation)
    {
        double sps = sampleRate / baud;
        float alpha = (float) (1.0 - std::exp(-1.0 / (0.15 * sps)));
        double t = 0.0;

        for (float level : levels)
        {
            double end = t + sps;
            for (; t < end; t += 1.0)
            {
                m_smoothed += alpha * (level - m_smoothed);
                sample(m_smoothed * deviation);
            }
            t = end;
        }
    }

    void noise(int n)
    {
        std::uniform_real_distribution<double> uniform(-8000.0, 8000.0);
        for (int i = 0; i < n; i++) {
            sample(uniform(m_rng));
        }
    }
};

struct Expected
{
    std::string m_protocol;
    std::string m_mode;
    std::string m_address;
    bool m_inverted;
};

std::string key(const std::string& protocol, const std::string& mode, const std::string& address, bool inverted)
{
    return protocol + " " + mode + " " + address + (inverted ? " inverted" : " normal");
}

void addPocsag(Modulator& mod, std::set<std::string>& sent, int baud, uint32_t address, const char *text, bool inverted)
{
    RadioPagingPocsag::TxMessage m;
    m.m_address = address;
    m.m_function = 3;
    m.m_type = RadioPagingPocsag::Alphanumeric;
    m.m_text = text;
    RadioPagingPocsag::EncodeResult enc = RadioPagingPocsag::encode({ m });
    std::vector<float> levels;

    // Binary 1 is a negative deviation
    for (uint8_t b : enc.m_bits) {
        levels.push_back((b ? -1.0f : 1.0f) * (inverted ? -1.0f : 1.0f));
    }
    mod.symbols(levels, baud, 4500.0);
    mod.noise(4000);

    char a[16];
    std::snprintf(a, sizeof(a), "%07u", address);
    sent.insert(key("pocsag", std::to_string(baud), a, inverted));
}

void addFlex(Modulator& mod, std::set<std::string>& sent, RadioPagingFlex::Mode mode, const std::vector<uint64_t>& capcodes)
{
    static const float levels4[4] = {-1.0f, -1.0f/3.0f, 1.0f/3.0f, 1.0f};
    RadioPagingFlexEncoder encoder;
    encoder.setMode(mode);

    for (uint64_t capcode : capcodes)
    {
        RadioPagingFlexEncoder::TxMessage m;
        m.m_capcode = capcode;
        m.m_type = RadioPagingFlex::Alphanumeric;
        m.m_text = "FLEX through the channel";
        if (encoder.enqueue(m).m_ok)
        {
            char a[16];
            std::snprintf(a, sizeof(a), "%010llu", (unsigned long long) capcode);
            sent.insert(key("flex", RadioPagingFlex::modeName(mode), a, false));
        }
    }

    for (uint64_t index = 0; (encoder.pending() > 0) && (index < 50); index++)
    {
        RadioPagingFlexEncoder::Frame frame = encoder.buildFrame(index);

        for (const auto& block : frame.m_blocks)
        {
            std::vector<float> levels;
            for (uint8_t s : block.m_symbols) {
                levels.push_back((block.m_levels == 4) ? levels4[s & 3] : ((s & 1) ? 1.0f : -1.0f));
            }
            mod.symbols(levels, block.m_baud, 4800.0);
        }
    }
    mod.noise(4000);
}

void addGsc(Modulator& mod, std::set<std::string>& sent, const std::vector<RadioPagingGsc::TxMessage>& messages)
{
    RadioPagingGsc::EncodeResult enc = RadioPagingGsc::encode(messages);
    std::vector<float> levels;

    // Binary 1 is a positive deviation
    for (uint8_t b : enc.m_bits) {
        levels.push_back(b ? 1.0f : -1.0f);
    }
    mod.symbols(levels, 600, 4500.0);
    mod.noise(4000);

    for (const auto& m : messages)
    {
        RadioPagingGsc::MessageType type = (m.m_type == RadioPagingGsc::Numeric) ? RadioPagingGsc::Alphanumeric : m.m_type;
        sent.insert(key("gsc", "600", m.m_address + RadioPagingGsc::suffixDigit(type, m.m_addressNumber), false));
    }
}

RadioPagingGsc::TxMessage gsc(const char *address, int number, RadioPagingGsc::MessageType type, const char *text)
{
    RadioPagingGsc::TxMessage m;
    m.m_address = address;
    m.m_addressNumber = number;
    m.m_type = type;
    m.m_text = text;
    return m;
}

} // namespace

int main()
{
    Modulator mod;
    std::set<std::string> sent;

    mod.noise(3000);
    addPocsag(mod, sent, 1200, 1234567, "POCSAG 1200 THROUGH THE CHANNEL", false);
    addPocsag(mod, sent, 512, 7654, "POCSAG 512", false);
    addPocsag(mod, sent, 2400, 2000001, "POCSAG 2400", false);
    addPocsag(mod, sent, 1200, 1000002, "POCSAG INVERTED", true);
    addFlex(mod, sent, RadioPagingFlex::Mode1600_2, { 123456, 234567 });
    addFlex(mod, sent, RadioPagingFlex::Mode3200_4, { 345678 });
    addGsc(mod, sent, { gsc("219789", 2, RadioPagingGsc::Numeric, "5551234"), gsc("219456", 1, RadioPagingGsc::Tone, ""),
        gsc("219457", 4, RadioPagingGsc::Alphanumeric, "GSC BATCH") });
    addGsc(mod, sent, { gsc("100123", 3, RadioPagingGsc::Alphanumeric, "GSC SINGLE") });

    MessageQueue queue;
    RadioPagingDemodSink sink;
    sink.setMessageQueueToChannel(&queue);
    sink.applyChannelSettings((int) sampleRate, 0, true);

    const size_t chunk = 4096;
    for (size_t i = 0; i < mod.m_samples.size(); i += chunk)
    {
        size_t end = std::min(mod.m_samples.size(), i + chunk);
        sink.feed(mod.m_samples.begin() + i, mod.m_samples.begin() + end);
    }

    std::set<std::string> received;
    int records = 0;
    bool fieldsOk = true;
    Message *message;

    while ((message = queue.pop()))
    {
        if (MainCore::MsgMessageRecord::match(*message))
        {
            const MessageRecord& r = ((MainCore::MsgMessageRecord *) message)->getRecord();
            records++;
            received.insert(key(r.m_protocol.toStdString(), r.m_mode.toStdString(), r.m_address.toStdString(),
                r.m_polarity == MessageRecord::PolarityInverted));
            fieldsOk = fieldsOk && !r.m_id.isEmpty() && (r.m_direction == MessageRecord::DirectionRX)
                && (r.m_kind == MessageRecord::KindMessage) && (r.m_protocolData.value("interpretedBy").toString() == "decoder")
                && (r.m_speed > 0);
            std::printf("      %s %s %s %s %s \"%s\"\n", r.m_protocol.toStdString().c_str(), r.m_mode.toStdString().c_str(),
                r.m_address.toStdString().c_str(), r.m_polarity == MessageRecord::PolarityInverted ? "I" : "N",
                r.m_msgType.toStdString().c_str(), r.m_text.toStdString().c_str());
        }
        delete message;
    }

    for (const auto& s : sent)
    {
        if (!received.count(s)) {
            std::printf("      missing: %s\n", s.c_str());
        }
    }
    for (const auto& r : received)
    {
        if (!sent.count(r)) {
            std::printf("      unexpected: %s\n", r.c_str());
        }
    }

    check(received == sent, "Every page sent is received once, with its protocol, mode, address and polarity, nothing else ("
        + std::to_string(sent.size()) + " sent, " + std::to_string(records) + " records)");
    check((size_t) records == sent.size(), "No duplicate records");
    check(fieldsOk, "Records carry an id, direction rx, kind message, speed and interpretedBy decoder");

    RadioPagingDemodSink::Status status = sink.getStatus();
    check((status.m_messages[0] == 4) && (status.m_messages[1] == 3) && (status.m_messages[2] == 4), "Status counts the records per protocol");

    std::printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
