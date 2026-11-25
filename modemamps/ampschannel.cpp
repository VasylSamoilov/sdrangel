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

#include "ampschannel.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "ampsmessages.h"

namespace modemamps
{

namespace
{

// osmocom: the DC filter ahead of the FSK and the SAT (its emphasis state's high pass)
const double kDcCutoff = 300.0;

} // namespace

AmpsChannel::AmpsChannel(AmpsSystem& system, int index, double sampleRate) :
    m_system(system),
    m_index(index),
    m_control(system.config().channels[static_cast<size_t>(index)].type != AmpsChannelType::Voice),
    m_scc(system.config().channels[static_cast<size_t>(index)].scc),
    m_sampleRate(sampleRate),
    m_rxMode(AmpsMode::Off),
    m_txMode(AmpsMode::Off),
    m_dcFilter(kDcCutoff, sampleRate, 1),
    m_reverse(sampleRate, false),
    m_sat(sampleRate),
    m_satTone(sampleRate, system.config().channels[static_cast<size_t>(index)].scc),
    m_rxSpeech(sampleRate),
    m_txSpeech(sampleRate),
    m_rxMeter(kRxNoiseGate, kVoiceMaxDeviation / kSpeechDeviation),
    m_txMeter(kRxNoiseGate, kVoiceMaxDeviation / kSpeechDeviation),
    m_modulator(sampleRate, kFskBitRate, kFskDeviation)
{
    m_reverse.setChannel(m_control ? ReverseReceiver::ChannelRecc : ReverseReceiver::ChannelRvc);
    m_reverse.setBurstHandler([this](const ReverseBurst& b) { return burst(b); });

    m_assembler.setSystemInfo(system.config().si);
    m_assembler.setMessageSink([this](const ReccMessage& m) {
        trace(false, describeRecc(m));
        m_system.receivedRecc(m_index, m);
    });
    m_assembler.setDropSink([this](const char *reason) { trace(false, std::string("(a message dropped: ") + reason + ")"); });

    m_focc.setSystemInfo(system.config().si);
    m_focc.setRegistrationId([]() { return static_cast<uint32_t>(std::time(nullptr)); });
    m_focc.setMessageSource([this](FoccMessage& m) {
        const bool any = m_system.nextFocc(m_index, m);
        if (any) {
            trace(true, describeFocc(m));
        }
        return any;
    });
    m_fvc.setWordSource([this](uint64_t& w) {
        const bool any = m_system.nextFvc(m_index, w);
        if (any)
        {
            for (const std::string& line : m_fvcTrace.add(w)) {
                trace(true, line);
            }
        }
        return any;
    });

    m_sat.setSatSink([this](int scc, double) {
        // the detector starts afresh with a change of mode and finds the same SAT again: shown once
        if (scc != m_tracedSat)
        {
            trace(false, scc < 0 ? std::string("SAT lost")
                : "SAT " + std::to_string(static_cast<int>(satFrequency(scc))) + " Hz (SCC " + std::to_string(scc) + ")");
            m_tracedSat = scc;
        }
        m_system.satChanged(m_index, scc);
    });
    m_sat.setToneSink([this](bool on, double) {
        if (on)
        {
            m_toneSince = m_received;
            trace(false, "ST on");
        }
        else
        {
            char text[48];
            std::snprintf(text, sizeof(text), "ST off after %.2f s", static_cast<double>(m_received - m_toneSince) / m_sampleRate);
            trace(false, text);
        }
        m_system.signallingTone(m_index, on);
    });
    m_rxSpeech.setSink([this](float* s, int n) { m_system.audioFromMobile(m_index, s, n); });
    m_txSpeech.setPull([this](float* s, int n) { m_system.audioToMobile(m_index, s, n); });

    if (m_control) {
        m_modulator.setBitSource([this]() { return m_focc.nextBit(); });
    } else {
        m_modulator.setBitSource([this]() { return m_fvc.nextBit(); });
    }
}

bool AmpsChannel::burst(const ReverseBurst& b)
{
    if (m_control)
    {
        if (!b.valid())
        {
            m_assembler.lost();
            return false;
        }
        return m_assembler.word(b.word(), b.first);
    }

    // RVC: an order confirmation (T 1); the called address of a flash is not taken yet
    if (b.valid()) {
        trace(false, describeRvc(b.word()));
    }
    // the called address (T 0), the answer to Send Called-Address: 8 digits a word, F 1 the first,
    // NAWC the words still to come
    if (b.valid() && wordField(words::kRvcDigits, b.word(), "T") == 0)
    {
        const uint64_t w = b.word();
        if (wordField(words::kRvcDigits, w, "F") == 1) {
            m_calledAddress.clear();
        }
        for (int i = 1; i <= 8; i++)
        {
            const char d = codeDigit(static_cast<int>(wordField(words::kRvcDigits, w, "DIGIT " + std::to_string(i))));
            if (d != 0) {
                m_calledAddress += d;
            }
        }
        if (wordField(words::kRvcDigits, w, "NAWC") != 0) {
            return true;                    // the next word follows
        }
        m_system.receivedRvcCalledAddress(m_index, m_calledAddress);
        m_calledAddress.clear();
        return false;
    }
    if (b.valid() && wordField(words::kRvcOrderConfirmation, b.word(), "T") == 1)
    {
        m_system.receivedRvcOrder(m_index, static_cast<uint8_t>(wordField(words::kRvcOrderConfirmation, b.word(), "ORDER")),
            static_cast<uint8_t>(wordField(words::kRvcOrderConfirmation, b.word(), "ORDQ")));
    }
    return false;
}

// The meters measure the speech of a call (as NMT's): from the first speech of a call, the
// orders in between not counted, to its end with the channel off; the last call's stay readable.
void AmpsChannel::meterMode(AmpsMode mode)
{
    if (mode == AmpsMode::Audio && !m_meterLive)
    {
        m_rxMeter.reset();
        m_txMeter.reset();
        m_meterCall++;
        m_meterLive = true;
        m_meterNumber.clear();
        for (const AmpsSystem::TransactionInfo& t : m_system.transactions())
        {
            if (t.channel == m_index) {
                m_meterNumber = t.subscriber.number();
            }
        }
        m_meterLimitedStart = m_txSpeech.limited();
    }
    if (mode == AmpsMode::Off) {
        m_meterLive = false;
    }
    m_rxSpeech.setMeter(mode == AmpsMode::Audio ? &m_rxMeter : nullptr);
    m_txSpeech.setMeter(mode == AmpsMode::Audio ? &m_txMeter : nullptr);
}

AmpsChannel::Speech AmpsChannel::speech() const
{
    Speech s;

    s.call = m_meterCall;
    s.live = m_meterLive;
    s.number = m_meterNumber;
    s.rxRecent = m_rxMeter.recent();
    s.txRecent = m_txMeter.recent();
    s.rxCall = m_rxMeter.call();
    s.txCall = m_txMeter.call();
    s.limited = m_txSpeech.limited() - m_meterLimitedStart;
    return s;
}

void AmpsChannel::trace(bool toPhone, const std::string& text)
{
    if (m_traceTap) {
        m_traceTap(toPhone, text);
    }
}

void AmpsChannel::receive(const float* samples, int length)
{
    m_received += static_cast<uint64_t>(length);       // the time of the tone's end: this block's end
    m_filtered.resize(static_cast<size_t>(length));
    for (int i = 0; i < length; i++) {
        m_filtered[static_cast<size_t>(i)] = static_cast<float>(m_dcFilter.process(samples[i]));
    }

    const AmpsMode mode = m_system.mode(m_index);
    if (mode != m_rxMode)
    {
        m_reverse.reset();
        m_sat.reset();
        m_rxSpeech.reset();
        m_rxMode = mode;
        meterMode(mode);
    }

    const float* s = m_filtered.data();
    switch (mode)
    {
    case AmpsMode::Off:
        break;
    case AmpsMode::Control:
        m_reverse.process(s, length);
        break;
    case AmpsMode::Audio:
    case AmpsMode::Frame:
        m_sat.process(s, length);
        m_reverse.process(s, length);
        m_rxSpeech.process(s, length);
        break;
    case AmpsMode::Silence:
        m_sat.process(s, length);
        m_reverse.process(s, length);
        break;
    }
}

void AmpsChannel::render(float* samples, int length)
{
    int pos = 0;

    while (pos < length)
    {
        const AmpsMode mode = m_system.mode(m_index);
        float* s = samples + pos;
        const int remaining = length - pos;

        if (mode != m_txMode)
        {
            m_modulator.reset();
            if (mode == AmpsMode::Frame)
            {
                m_fvc.reset();
                m_fvcTrace.reset();
            }
            // a voice channel keyed for a call and off after it
            if (m_txMode == AmpsMode::Off && !m_control)
            {
                trace(true, "keyed, SAT " + std::to_string(static_cast<int>(satFrequency(m_scc))) + " Hz");
                m_tracedSat = -1;           // a call: its phone's SAT is new
            }
            if (mode == AmpsMode::Off && !m_control) {
                trace(true, "off");
            }
            m_txMode = mode;
        }

        switch (mode)
        {
        case AmpsMode::Off:
            std::fill(s, s + remaining, 0.0f);
            pos = length;
            break;
        case AmpsMode::Control:
            pos += m_modulator.render(s, remaining, false);
            break;
        case AmpsMode::Silence:
            std::fill(s, s + remaining, 0.0f);
            m_satTone.add(s, remaining);
            pos = length;
            break;
        case AmpsMode::Audio:
            m_txSpeech.generate(s, remaining);
            m_satTone.add(s, remaining);
            pos = length;
            break;
        case AmpsMode::Frame:
        {
            // the orders in place of the speech, no SAT (osmocom); when they end the system has
            // changed the mode for the rest
            const int n = m_modulator.render(s, remaining, false);
            pos += n;
            if (n < remaining && m_system.mode(m_index) == AmpsMode::Frame)
            {
                std::fill(samples + pos, samples + length, 0.0f);
                pos = length;
            }
            break;
        }
        }
    }
}

} // namespace modemamps
