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

#include "nmtbasestation.h"

#include <cmath>

namespace modemnmt
{

namespace
{

// The stream whose span holds the carrier, -1 if none. dc is set if the carrier sits on the
// centre of the span (the DC of the receiver or the carrier leak of the transmitter).
int coveringStream(const std::vector<NmtStreamSpan>& streams, double frequency, double bandwidth, bool& dc)
{
    for (const NmtStreamSpan& s : streams)
    {
        const double offset = std::fabs(frequency - s.centre);

        if (s.sampleRate > 0.0 && offset + bandwidth / 2.0 <= NmtBaseStation::kUsableBandwidth * s.sampleRate / 2.0)
        {
            dc = offset < bandwidth / 2.0;
            return s.id;
        }
    }
    return -1;
}

} // namespace

NmtBaseStation::NmtBaseStation(const NmtNetworkConfig& config) :
    m_config(config),
    m_trafficArea(-1),
    m_rxStream(-1),
    m_txStream(-1),
    m_samples(0),
    m_clock(kAudioRate)
{
    const NmtCountry* country = nmtCountry(config.system, config.country);

    if (!country) {
        m_issues.push_back({true, "no NMT-" + std::to_string(static_cast<int>(config.system)) + " plan for country " + config.country});
    }
    else
    {
        m_trafficArea = nmtTrafficArea(config.system, config.country + "," + std::to_string(config.area));
        if (m_trafficArea < 0) {
            m_issues.push_back({true, "country " + config.country + " has no traffic area " + std::to_string(config.area)});
        }
    }

    for (size_t i = 0; i < config.carriers.size(); i++)
    {
        NmtCarrierStatus status;

        status.config = config.carriers[i];
        if (country)
        {
            status.issues = nmtCheckCarrier(*country, status.config.channel, status.config.type == ChannelType::CcTc && config.sendClock);
            nmtChannelFrequency(*country, status.config.channel, status.frequency);
        }
        for (size_t j = 0; j < i; j++)
        {
            if (config.carriers[j].channel == status.config.channel) {
                status.issues.push_back({true, "channel " + std::to_string(status.config.channel) + " is given twice"});
            }
        }
        status.planned = country && m_trafficArea >= 0;
        for (const NmtPlanIssue& issue : status.issues) {
            status.planned = status.planned && !issue.error;
        }
        m_carriers.push_back(status);
    }
}

NmtBaseStation::~NmtBaseStation() = default;

bool NmtBaseStation::bind(const std::vector<NmtStreamSpan>& rxStreams, const std::vector<NmtStreamSpan>& txStreams)
{
    m_channels.clear();
    m_rxEmphasis.clear();
    m_txEmphasis.clear();
    m_onAir.clear();
    m_system.reset();
    m_rxStream = -1;
    m_txStream = -1;
    m_rxSpan = NmtStreamSpan();
    m_txSpan = NmtStreamSpan();

    // the issues of an earlier binding go
    for (NmtCarrierStatus& c : m_carriers)
    {
        c.streamIssues.clear();
        c.rxStream = -1;
        c.txStream = -1;
        c.onAir = false;
    }
    m_streamIssues.clear();

    bool split = false;

    for (size_t i = 0; i < m_carriers.size(); i++)
    {
        NmtCarrierStatus& c = m_carriers[i];

        if (!c.planned) {
            continue;
        }
        const double bandwidth = kCarrierBandwidth * c.frequency.deviationFactor;
        bool rxDc = false;
        bool txDc = false;

        c.rxStream = coveringStream(rxStreams, c.frequency.uplink, bandwidth, rxDc);
        c.txStream = coveringStream(txStreams, c.frequency.downlink, bandwidth, txDc);
        if (c.rxStream < 0) {
            c.streamIssues.push_back({true, "no receive stream covers the uplink"});
        }
        if (c.txStream < 0) {
            c.streamIssues.push_back({true, "no transmit stream covers the downlink"});
        }
        if (rxDc || txDc) {
            c.streamIssues.push_back({false, "the carrier is on the centre of the stream"});
        }
        if (c.rxStream < 0 || c.txStream < 0) {
            continue;
        }
        if (m_rxStream < 0)
        {
            m_rxStream = c.rxStream;
            m_txStream = c.txStream;
        }
        if (c.rxStream != m_rxStream || c.txStream != m_txStream)
        {
            split = true;
            continue;
        }
        m_onAir.push_back(static_cast<int>(i));
    }

    if (split)
    {
        m_streamIssues.push_back({true, "the carriers are on more than one receive or transmit stream"});
        m_onAir.clear();
        m_rxStream = -1;
        m_txStream = -1;
        return false;
    }
    if (m_onAir.empty())
    {
        m_streamIssues.push_back({true, "no carrier is on the air"});
        return false;
    }

    for (const NmtStreamSpan& s : rxStreams)
    {
        if (s.id == m_rxStream) {
            m_rxSpan = s;
        }
    }
    for (const NmtStreamSpan& s : txStreams)
    {
        if (s.id == m_txStream) {
            m_txSpan = s;
        }
    }

    m_system = std::make_unique<NmtSystem>(m_clock);
    m_system->setSmscNumber(m_config.smscNumber);
    for (int index : m_onAir)
    {
        NmtCarrierStatus& c = m_carriers[static_cast<size_t>(index)];
        ChannelConfig cc;

        cc.system = m_config.system;
        cc.type = c.config.type;
        cc.channel = c.config.channel;
        cc.msPower = m_config.msPower;
        cc.trafficArea = static_cast<uint8_t>(m_trafficArea);
        cc.areaNo = m_config.areaNo;
        cc.sendClock = m_config.sendClock;
        cc.batteryPeriod = m_config.batteryPeriod;
        cc.batteryGroup = m_config.batteryGroup;
        cc.supervisory = c.config.supervisory;
        cc.sendCallerId = m_config.sendCallerId;
        cc.ccBandStart = m_config.ccBandStart;
        cc.ccBandEnd = m_config.ccBandEnd;
        cc.tcBandStart = m_config.tcBandStart;
        cc.tcBandEnd = m_config.tcBandEnd;
        cc.compandor = m_config.compandor;
        // the deviation factor goes into the speech deviation of the audio (process()), the
        // levels inside the channel stay those of the Scandinavian plan
        m_channels.push_back(&m_system->addChannel(cc, kAudioRate, 1.0));
        m_rxEmphasis.push_back(std::make_unique<Emphasis>(kAudioRate));
        m_txEmphasis.push_back(std::make_unique<Emphasis>(kAudioRate));
        c.onAir = true;
    }

    return true;
}

int NmtBaseStation::carrierId(size_t index) const
{
    return m_config.id * kIdStride + m_carriers[index].config.channel;
}

namespace
{

modemcellular::CarrierTuning tuning(int id, double frequency, double centre, const NmtChannelFrequency& f)
{
    modemcellular::CarrierTuning t;

    t.id = id;
    t.offset = frequency - centre;
    t.passband = NmtBaseStation::kCarrierBandwidth / 2.0 * f.deviationFactor;
    t.spacing = f.spacing;
    return t;
}

} // namespace

std::vector<modemcellular::CarrierTuning> NmtBaseStation::rxTuning() const
{
    std::vector<modemcellular::CarrierTuning> out;

    for (int index : m_onAir)
    {
        const NmtCarrierStatus& c = m_carriers[static_cast<size_t>(index)];
        out.push_back(tuning(carrierId(static_cast<size_t>(index)), c.frequency.uplink, m_rxSpan.centre, c.frequency));
    }
    return out;
}

std::vector<modemcellular::CarrierTuning> NmtBaseStation::txTuning() const
{
    std::vector<modemcellular::CarrierTuning> out;

    for (int index : m_onAir)
    {
        const NmtCarrierStatus& c = m_carriers[static_cast<size_t>(index)];
        out.push_back(tuning(carrierId(static_cast<size_t>(index)), c.frequency.downlink, m_txSpan.centre, c.frequency));
    }
    return out;
}

void NmtBaseStation::process(const modemcellular::CarrierBatch& rx, modemcellular::CarrierBatch& tx)
{
    std::vector<int> ids;

    for (int index : m_onAir) {
        ids.push_back(carrierId(static_cast<size_t>(index)));
    }
    tx.reset(rx.sample, rx.length, m_system ? ids : std::vector<int>());
    if (!m_system || rx.length <= 0) {
        return;
    }

    m_audio.resize(static_cast<size_t>(rx.length));
    for (size_t i = 0; i < m_channels.size(); i++)
    {
        const int at = rx.find(ids[i]);

        if (at < 0) {
            continue;
        }
        // Hz to the speech level, then the emphasis of the receiver
        const float* in = rx.carrier(static_cast<size_t>(at));
        const float scale = static_cast<float>(1.0 / speechDeviation(i));
        for (int k = 0; k < rx.length; k++) {
            m_audio[static_cast<size_t>(k)] = in[k] * scale;
        }
        m_rxEmphasis[i]->de(m_audio.data(), rx.length);
        m_channels[i]->receive(m_audio.data(), rx.length);
    }
    m_clock.advance(rx.length);
    m_system->run();
    for (size_t i = 0; i < m_channels.size(); i++)
    {
        float* out = tx.carrier(i);
        const float scale = static_cast<float>(speechDeviation(i));

        m_channels[i]->render(out, rx.length, false);
        m_txEmphasis[i]->pre(out, rx.length);
        for (int k = 0; k < rx.length; k++) {
            out[k] *= scale;
        }
    }
    m_samples += static_cast<uint64_t>(rx.length);
}

double NmtBaseStation::speechDeviation(size_t onAirIndex) const
{
    return kSpeechDeviation * m_carriers[static_cast<size_t>(m_onAir[onAirIndex])].frequency.deviationFactor;
}

} // namespace modemnmt
