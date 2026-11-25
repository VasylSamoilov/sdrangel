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

#include "ampsbasestation.h"

#include <algorithm>
#include <cmath>

namespace modemamps
{

namespace
{

int coveringStream(const std::vector<AmpsStreamSpan>& streams, double frequency, bool& dc)
{
    for (const AmpsStreamSpan& s : streams)
    {
        const double offset = std::fabs(frequency - s.centre);

        if (s.sampleRate > 0.0
            && offset + AmpsBaseStation::kCarrierBandwidth / 2.0 <= AmpsBaseStation::kUsableBandwidth * s.sampleRate / 2.0)
        {
            dc = offset < AmpsBaseStation::kCarrierBandwidth / 2.0;
            return s.id;
        }
    }
    return -1;
}

modemcellular::CarrierTuning tuning(int id, double frequency, double centre)
{
    modemcellular::CarrierTuning t;

    t.id = id;
    t.offset = frequency - centre;
    t.passband = AmpsBaseStation::kPassband;
    t.spacing = AmpsBaseStation::kChannelSpacing;
    return t;
}

} // namespace

AmpsBaseStation::AmpsBaseStation(const AmpsNetworkConfig& config) :
    m_config(config),
    m_rxStream(-1),
    m_txStream(-1),
    m_samples(0),
    m_clock(kAudioRate)
{
    int paging = 0;

    for (const AmpsCarrierConfig& c : config.carriers)
    {
        AmpsCarrierStatus s;
        s.config = c;
        s.issues = ampsCheckCarrier(c.channel, c.type);
        s.planned = ampsChannelFrequency(c.channel, s.frequency);
        for (const AmpsPlanIssue& i : s.issues) {
            s.planned = s.planned && !i.error;
        }
        if (c.type == AmpsChannelType::ControlPagingVoice) {
            // osmocom: "a hack, but the only way to use control channel and voice channel on one
            // transceiver. Some phones may reject this, but all my phones don't."
            s.issues.push_back({false, "the combined channel (CC/PC/VC) stops being the control channel during a call; "
                "some phones may reject it (osmocom)"});
        }
        if (c.type != AmpsChannelType::Voice) {
            paging++;
        }
        m_carriers.push_back(s);
    }
    // osmocom: only one paging channel
    if (paging > 1) {
        m_issues.push_back({true, "only one paging channel (CC/PC) is supported"});
    }
    if (paging == 0) {
        m_issues.push_back({true, "the network has no control channel (CC/PC)"});
    }
}

AmpsBaseStation::~AmpsBaseStation() = default;

bool AmpsBaseStation::bind(const std::vector<AmpsStreamSpan>& rxStreams, const std::vector<AmpsStreamSpan>& txStreams)
{
    m_channels.clear();
    m_onAir.clear();
    m_system.reset();
    m_keyed.clear();
    m_keyingChanged = false;
    m_rxStream = -1;
    m_txStream = -1;
    m_rxSpan = AmpsStreamSpan();
    m_txSpan = AmpsStreamSpan();
    for (AmpsCarrierStatus& c : m_carriers)
    {
        c.streamIssues.clear();
        c.rxStream = -1;
        c.txStream = -1;
        c.onAir = false;
    }
    m_streamIssues.clear();

    for (const AmpsPlanIssue& i : m_issues)
    {
        if (i.error) {
            return false;
        }
    }

    bool split = false;
    for (size_t i = 0; i < m_carriers.size(); i++)
    {
        AmpsCarrierStatus& c = m_carriers[i];
        bool rxDc = false;
        bool txDc = false;

        if (!c.planned) {
            continue;
        }
        c.rxStream = coveringStream(rxStreams, c.frequency.uplink, rxDc);
        c.txStream = coveringStream(txStreams, c.frequency.downlink, txDc);
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
    bool control = false;
    for (int index : m_onAir) {
        control = control || m_carriers[static_cast<size_t>(index)].config.type != AmpsChannelType::Voice;
    }
    if (!control)
    {
        m_streamIssues.push_back({true, "the control channel is not on the air"});
        m_onAir.clear();
        m_rxStream = -1;
        m_txStream = -1;
        return false;
    }

    for (const AmpsStreamSpan& s : rxStreams)
    {
        if (s.id == m_rxStream) {
            m_rxSpan = s;
        }
    }
    for (const AmpsStreamSpan& s : txStreams)
    {
        if (s.id == m_txStream) {
            m_txSpan = s;
        }
    }

    AmpsSystemConfig sc;
    sc.si.sid = m_config.sid;
    sc.si.dcc = static_cast<uint8_t>(m_config.dcc);
    sc.si.cmac = static_cast<uint8_t>(m_config.msPower);
    sc.si.vmac = static_cast<uint8_t>(m_config.msPower);
    sc.roundTrip = m_config.roundTrip;
    sc.sendCallerId = m_config.sendCallerId;
    for (int index : m_onAir)
    {
        const AmpsCarrierConfig& c = m_carriers[static_cast<size_t>(index)].config;
        sc.channels.push_back({c.channel, c.type, c.scc});
    }
    m_system = std::make_unique<AmpsSystem>(m_clock, sc);
    for (size_t i = 0; i < m_onAir.size(); i++)
    {
        m_channels.push_back(std::make_unique<AmpsChannel>(*m_system, static_cast<int>(i), kAudioRate));
        m_carriers[static_cast<size_t>(m_onAir[i])].onAir = true;
    }
    tapChannels();
    return true;
}

void AmpsBaseStation::setTraceTap(TraceTap tap)
{
    m_traceTap = std::move(tap);
    tapChannels();
}

void AmpsBaseStation::tapChannels()
{
    for (size_t i = 0; i < m_channels.size(); i++)
    {
        if (!m_traceTap)
        {
            m_channels[i]->setTraceTap(nullptr);
            continue;
        }
        m_channels[i]->setTraceTap([this, i](bool toPhone, const std::string& text) { m_traceTap(i, toPhone, text); });
    }
}

int AmpsBaseStation::carrierId(size_t index) const
{
    return m_config.id * kIdStride + m_carriers[index].config.channel;
}

std::vector<modemcellular::CarrierTuning> AmpsBaseStation::rxTuning() const
{
    std::vector<modemcellular::CarrierTuning> out;

    for (int index : m_onAir)
    {
        const AmpsCarrierStatus& c = m_carriers[static_cast<size_t>(index)];
        out.push_back(tuning(carrierId(static_cast<size_t>(index)), c.frequency.uplink, m_rxSpan.centre));
    }
    return out;
}

bool AmpsBaseStation::keyed(size_t onAirIndex) const
{
    return m_system && m_system->mode(static_cast<int>(onAirIndex)) != AmpsMode::Off;
}

std::vector<modemcellular::CarrierTuning> AmpsBaseStation::txTuning() const
{
    std::vector<modemcellular::CarrierTuning> out;

    for (size_t i = 0; i < m_onAir.size(); i++)
    {
        if (!keyed(i)) {
            continue;
        }
        const int index = m_onAir[i];
        const AmpsCarrierStatus& c = m_carriers[static_cast<size_t>(index)];
        out.push_back(tuning(carrierId(static_cast<size_t>(index)), c.frequency.downlink, m_txSpan.centre));
    }
    return out;
}

bool AmpsBaseStation::txKeyingChanged()
{
    const bool changed = m_keyingChanged;
    m_keyingChanged = false;
    return changed;
}

void AmpsBaseStation::process(const modemcellular::CarrierBatch& rx, modemcellular::CarrierBatch& tx)
{
    if (!m_system || rx.length <= 0)
    {
        tx.reset(rx.sample, rx.length, std::vector<int>());
        return;
    }

    for (size_t i = 0; i < m_channels.size(); i++)
    {
        const int at = rx.find(carrierId(static_cast<size_t>(m_onAir[i])));
        if (at >= 0) {
            m_channels[i]->receive(rx.carrier(static_cast<size_t>(at)), rx.length);
        }
    }
    m_clock.advance(rx.length);
    m_system->run();

    // each channel renders (a voice channel may be keyed by the control channel's message of this
    // block), the batch carries the keyed ones
    m_render.resize(m_channels.size());
    for (size_t i = 0; i < m_channels.size(); i++)
    {
        m_render[i].resize(static_cast<size_t>(rx.length));
        m_channels[i]->render(m_render[i].data(), rx.length);
    }
    std::vector<int> ids;
    std::vector<size_t> from;
    for (size_t i = 0; i < m_channels.size(); i++)
    {
        if (keyed(i))
        {
            ids.push_back(carrierId(static_cast<size_t>(m_onAir[i])));
            from.push_back(i);
        }
    }
    if (ids != m_keyed)
    {
        m_keyed = ids;
        m_keyingChanged = true;
    }
    tx.reset(rx.sample, rx.length, ids);
    for (size_t k = 0; k < from.size(); k++) {
        std::copy(m_render[from[k]].begin(), m_render[from[k]].end(), tx.carrier(k));
    }
    m_samples += static_cast<uint64_t>(rx.length);
}

} // namespace modemamps
