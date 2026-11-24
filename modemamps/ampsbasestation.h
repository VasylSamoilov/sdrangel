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

#ifndef MODEMAMPS_AMPSBASESTATION_H_
#define MODEMAMPS_AMPSBASESTATION_H_

#include <memory>
#include <string>
#include <vector>

#include "ampschannel.h"
#include "ampsplan.h"
#include "ampssystem.h"
#include "carrierbatch.h"
#include "carrierradio.h"
#include "clock.h"
#include "export.h"

// One AMPS network: a base station with its carriers, as the controller of the cellular plugins runs
// it, the same way as modemnmt's NmtBaseStation. The carriers get their frequencies from the channel
// plan and are bound to one receive and one transmit stream (radio plugin) whose span covers them.
// The network runs on the clock of its receive stream: every received batch is fed to the carriers,
// the clock moves on by its length, the timers run, and the same length is rendered for the
// transmitter. The batches carry the frequency deviation in Hz, which the channels take as it is.
namespace modemamps
{

struct AmpsCarrierConfig
{
    int channel = 0;
    AmpsChannelType type = AmpsChannelType::Voice;
    int scc = 0;                    // SAT colour code 0..2 of a voice channel
};

struct AmpsNetworkConfig
{
    int id = 0;                     // the carrier ids are id * kIdStride + channel number
    uint16_t sid = 0;               // the system identification (osmocom -S sid=)
    int dcc = 0;
    int msPower = 4;                // the power level of the phones (osmocom's ms_power: CMAC, VMAC)
    double roundTrip = 0.0;         // the round trip of the radio, s (AmpsSystemConfig)
    std::vector<AmpsCarrierConfig> carriers;
};

struct AmpsStreamSpan
{
    int id = -1;
    double centre = 0.0;            // Hz
    double sampleRate = 0.0;        // Hz
};

struct AmpsCarrierStatus
{
    AmpsCarrierConfig config;
    AmpsChannelFrequency frequency;
    std::vector<AmpsPlanIssue> issues;          // of the plan
    std::vector<AmpsPlanIssue> streamIssues;    // of the binding to the streams
    bool planned = false;           // the plan knows the channel and nothing refuses it
    int rxStream = -1;
    int txStream = -1;
    bool onAir = false;
};

class MODEMAMPS_API AmpsBaseStation
{
public:
    static constexpr double kAudioRate = 48000.0;
    // the part of a stream that is usable, and the band of a carrier: 15 kHz either side (8 kHz
    // deviation of the FSK, 12 kHz of the voice), the channel spacing
    static constexpr double kUsableBandwidth = 0.75;
    static constexpr double kPassband = 15000.0;
    static constexpr double kCarrierBandwidth = 2.0 * kPassband;
    static constexpr double kChannelSpacing = kAmpsChannelSpacing;
    static constexpr int kIdStride = 10000;

    explicit AmpsBaseStation(const AmpsNetworkConfig& config);
    ~AmpsBaseStation();
    AmpsBaseStation(const AmpsBaseStation&) = delete;
    AmpsBaseStation& operator=(const AmpsBaseStation&) = delete;

    const AmpsNetworkConfig& config() const { return m_config; }
    // network level problems (more than one paging channel)
    const std::vector<AmpsPlanIssue>& issues() const { return m_issues; }
    const std::vector<AmpsPlanIssue>& streamIssues() const { return m_streamIssues; }
    const std::vector<AmpsCarrierStatus>& carriers() const { return m_carriers; }

    // Binds the carriers to the streams that cover them and starts the network on them (all on one
    // receive and one transmit stream, else the network stays off and false is returned, the reason
    // in the issues). A new binding starts the network again.
    bool bind(const std::vector<AmpsStreamSpan>& rxStreams, const std::vector<AmpsStreamSpan>& txStreams);
    bool running() const { return static_cast<bool>(m_system); }
    int rxStream() const { return m_rxStream; }
    int txStream() const { return m_txStream; }
    int carrierId(size_t index) const;
    std::vector<modemcellular::CarrierTuning> rxTuning() const;
    std::vector<modemcellular::CarrierTuning> txTuning() const;

    // One received batch to the batch for the transmitter: the carriers on air, the same length, the
    // same first sample.
    void process(const modemcellular::CarrierBatch& rx, modemcellular::CarrierBatch& tx);
    uint64_t samples() const { return m_samples; }

    AmpsSystem* system() { return m_system.get(); }
    const modemcellular::SampleClock& clock() const { return m_clock; }

private:
    AmpsNetworkConfig m_config;
    std::vector<AmpsPlanIssue> m_issues;
    std::vector<AmpsPlanIssue> m_streamIssues;
    std::vector<AmpsCarrierStatus> m_carriers;
    int m_rxStream;
    int m_txStream;
    uint64_t m_samples;
    modemcellular::SampleClock m_clock;
    std::unique_ptr<AmpsSystem> m_system;
    AmpsStreamSpan m_rxSpan;
    AmpsStreamSpan m_txSpan;
    std::vector<int> m_onAir;                           // indices into m_carriers
    std::vector<std::unique_ptr<AmpsChannel>> m_channels;   // parallel to m_onAir
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSBASESTATION_H_
