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

#ifndef MODEMNMT_NMTBASESTATION_H_
#define MODEMNMT_NMTBASESTATION_H_

#include <memory>
#include <string>
#include <vector>

#include "carrierbatch.h"
#include "clock.h"
#include "nmtplan.h"
#include "nmtsystem.h"
#include "export.h"

namespace modemnmt
{

// One NMT network: a base station with its carriers, as the controller of the cellular plugins
// runs it. The carriers get their frequencies from the plan of the country and are bound to one
// receive and one transmit stream (radio plugin) whose span covers them. The network runs on the
// clock of its receive stream: every received batch is fed to the carriers, the clock moves on by
// its length, the timers run, and the same length is rendered for the transmitter.
struct NmtCarrierConfig
{
    int channel = 0;
    ChannelType type = ChannelType::Tc;
    int supervisory = 0;            // 1..4 on a traffic channel, 0 for none
};

struct NmtNetworkConfig
{
    System system = System::Nmt450;
    std::string country = "DK";     // short name of the plan
    int area = 1;                   // traffic area Y2 within the country
    int areaNo = 0;                 // area number 1..4, 0 for none
    int msPower = 1;
    bool compandor = true;
    bool sendCallerId = false;
    bool sendClock = false;
    int batteryPeriod = 0;
    int batteryGroup = 8;
    int ccBandStart = 0;
    int ccBandEnd = 0;
    int tcBandStart = 0;
    int tcBandEnd = 0;
    std::string smscNumber;         // empty: no short messages from the phones
    std::vector<NmtCarrierConfig> carriers;
};

// The span of a radio stream: the carriers it can carry.
struct NmtStreamSpan
{
    int id = -1;
    double centre = 0.0;            // Hz
    double sampleRate = 0.0;        // Hz
};

struct NmtCarrierStatus
{
    NmtCarrierConfig config;
    NmtChannelFrequency frequency;
    std::vector<NmtPlanIssue> issues;           // of the plan
    std::vector<NmtPlanIssue> streamIssues;     // of the binding to the streams
    bool planned = false;           // the plan knows the channel and nothing refuses it
    int rxStream = -1;              // -1: no stream covers it
    int txStream = -1;
    bool onAir = false;
};

class MODEMNMT_API NmtBaseStation
{
public:
    static constexpr double kAudioRate = 48000.0;
    // the part of a stream that is usable (no filter is perfect), and the bandwidth of a carrier:
    // 2 * (4700 Hz deviation + 4055 Hz modulation) times the deviation factor of the plan
    static constexpr double kUsableBandwidth = 0.75;
    static constexpr double kCarrierBandwidth = 2.0 * (4700.0 + 4055.0);

    explicit NmtBaseStation(const NmtNetworkConfig& config);
    ~NmtBaseStation();
    NmtBaseStation(const NmtBaseStation&) = delete;
    NmtBaseStation& operator=(const NmtBaseStation&) = delete;

    const NmtNetworkConfig& config() const { return m_config; }
    // network level problems (unknown country, no such traffic area); the carriers have their own
    const std::vector<NmtPlanIssue>& issues() const { return m_issues; }
    // why the last binding did not start the network
    const std::vector<NmtPlanIssue>& streamIssues() const { return m_streamIssues; }
    const std::vector<NmtCarrierStatus>& carriers() const { return m_carriers; }

    // Binds the carriers to the streams that cover them and starts the network on them. All
    // carriers of the network have to be on one receive and one transmit stream: if they are not,
    // or nothing is covered, the network stays off and false is returned (the reason is in the
    // issues). A new binding starts the network again (transactions are lost).
    bool bind(const std::vector<NmtStreamSpan>& rxStreams, const std::vector<NmtStreamSpan>& txStreams);
    bool running() const { return static_cast<bool>(m_system); }
    int rxStream() const { return m_rxStream; }
    int txStream() const { return m_txStream; }

    // One received batch of the receive stream (carrier ids are channel numbers; other carriers
    // of the stream are left alone) to the batch for the transmitter: the carriers on air, the
    // same length, the same first sample.
    void process(const modemcellular::CarrierBatch& rx, modemcellular::CarrierBatch& tx);
    uint64_t samples() const { return m_samples; }

    // the network itself, null while it is not running
    NmtSystem* system() { return m_system.get(); }

private:
    NmtNetworkConfig m_config;
    std::vector<NmtPlanIssue> m_issues;
    std::vector<NmtPlanIssue> m_streamIssues;
    std::vector<NmtCarrierStatus> m_carriers;
    int m_trafficArea;
    int m_rxStream;
    int m_txStream;
    uint64_t m_samples;
    modemcellular::SampleClock m_clock;
    std::unique_ptr<NmtSystem> m_system;
    std::vector<int> m_onAir;                   // indices into m_carriers
    std::vector<NmtChannel*> m_channels;        // parallel to m_onAir
};

} // namespace modemnmt

#endif // MODEMNMT_NMTBASESTATION_H_
