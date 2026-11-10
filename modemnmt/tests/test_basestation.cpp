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

#include <cmath>
#include <memory>

#include "phone.h"
#include "carrierqueue.h"
#include "nmtbasestation.h"
#include "nmtframe.h"
#include "nmtmodem.h"

using modemcellular::CarrierBatch;
using modemcellular::CarrierQueue;

static NmtNetworkConfig network(std::vector<int> channels)
{
    NmtNetworkConfig c;

    c.system = System::Nmt450;
    c.country = "DK";
    c.area = 1;
    for (size_t i = 0; i < channels.size(); i++)
    {
        NmtCarrierConfig carrier;
        carrier.channel = channels[i];
        carrier.type = i == 0 ? ChannelType::CcTc : ChannelType::Tc;
        c.carriers.push_back(carrier);
    }
    return c;
}

// DK channel 1 is 463.000 / 453.000 MHz, channel 5 463.100 / 453.100, channel 100 465.475 / 455.475
static const NmtStreamSpan kRx = {1, 453.3e6, 1e6};
static const NmtStreamSpan kTx = {2, 463.3e6, 1e6};

static bool hasIssue(const std::vector<NmtPlanIssue>& issues, bool error)
{
    for (const NmtPlanIssue& i : issues)
    {
        if (i.error == error) {
            return true;
        }
    }
    return false;
}

static void testBind()
{
    NmtBaseStation bs(network({1, 5}));

    CHECK(bs.issues().empty());
    CHECK(bs.carriers().size() == 2);
    CHECK(bs.carriers()[0].planned && bs.carriers()[1].planned);
    CHECK(std::fabs(bs.carriers()[1].frequency.downlink - 463.100e6) < 1.0);
    CHECK(std::fabs(bs.carriers()[1].frequency.uplink - 453.100e6) < 1.0);
    CHECK(!bs.running());

    CHECK(bs.bind({kRx}, {kTx}));
    CHECK(bs.running());
    CHECK(bs.rxStream() == 1 && bs.txStream() == 2);
    CHECK(bs.carriers()[0].onAir && bs.carriers()[1].onAir);
    CHECK(bs.streamIssues().empty());
    CHECK(bs.system()->channelCount() == 2);
    CHECK(bs.system()->channel(0).config().trafficArea == 0x51);
    CHECK(bs.system()->channel(0).config().type == ChannelType::CcTc);
    CHECK(bs.system()->channel(1).config().channel == 5);

    // a second binding starts the network again
    CHECK(bs.bind({kRx}, {kTx}));
    CHECK(bs.system()->channelCount() == 2);

    // a stream that is too narrow: the edge of the usable part (0.75 of the rate)
    NmtBaseStation edge(network({1}));
    const double half = NmtBaseStation::kCarrierBandwidth / 2.0;
    const NmtStreamSpan fits = {3, 453.0e6 + 0.375e6 - half - 1.0, 1e6};
    const NmtStreamSpan over = {3, 453.0e6 + 0.375e6 - half + 1.0, 1e6};
    CHECK(edge.bind({fits}, {kTx}));
    CHECK(!edge.bind({over}, {kTx}));
    CHECK(hasIssue(edge.carriers()[0].streamIssues, true));
    CHECK(hasIssue(edge.streamIssues(), true));
    CHECK(!edge.running());
}

static void testCoverage()
{
    // channel 100 is outside both streams: the others go on the air without it
    NmtBaseStation bs(network({1, 100, 5}));

    CHECK(bs.bind({kRx}, {kTx}));
    CHECK(bs.carriers()[0].onAir && !bs.carriers()[1].onAir && bs.carriers()[2].onAir);
    CHECK(hasIssue(bs.carriers()[1].streamIssues, true));
    CHECK(bs.carriers()[1].rxStream == -1);
    CHECK(bs.system()->channelCount() == 2);

    // with a second pair of streams channel 100 would be split off: refused
    const NmtStreamSpan rx2 = {5, 455.5e6, 1e6};
    const NmtStreamSpan tx2 = {6, 465.5e6, 1e6};
    CHECK(!bs.bind({kRx, rx2}, {kTx, tx2}));
    CHECK(!bs.running());
    CHECK(hasIssue(bs.streamIssues(), true));
    CHECK(bs.rxStream() == -1);

    // receive and transmit may be on different devices, but one each
    NmtBaseStation far(network({100}));
    CHECK(far.bind({kRx, rx2}, {kTx, tx2}));
    CHECK(far.rxStream() == 5 && far.txStream() == 6);

    // a carrier on the centre of a stream is a warning only
    NmtBaseStation dc(network({1}));
    CHECK(dc.bind({{1, 453.0e6, 1e6}}, {kTx}));
    CHECK(hasIssue(dc.carriers()[0].streamIssues, false));
    CHECK(!hasIssue(dc.carriers()[0].streamIssues, true));
    CHECK(dc.carriers()[0].onAir);

    // nothing to bind to
    NmtBaseStation none(network({1}));
    CHECK(!none.bind({}, {}));
    CHECK(!none.running());
}

static void testPlanErrors()
{
    // unknown channel and a channel given twice stay off the air, the rest goes on
    NmtBaseStation bs(network({1, 400, 1, 5}));
    CHECK(bs.carriers()[0].planned);
    CHECK(!bs.carriers()[1].planned && hasIssue(bs.carriers()[1].issues, true));
    CHECK(!bs.carriers()[2].planned && hasIssue(bs.carriers()[2].issues, true));
    CHECK(bs.carriers()[3].planned);
    CHECK(bs.bind({kRx}, {kTx}));
    CHECK(bs.system()->channelCount() == 2);

    // an unknown country or traffic area: nothing is planned
    NmtNetworkConfig c = network({1});
    c.country = "XX";
    NmtBaseStation country(c);
    CHECK(hasIssue(country.issues(), true));
    CHECK(!country.carriers()[0].planned);
    CHECK(!country.bind({kRx}, {kTx}));

    c = network({1});
    c.area = 10;
    NmtBaseStation area(c);
    CHECK(hasIssue(area.issues(), true));
    CHECK(!area.bind({kRx}, {kTx}));

    // the clock on a combined channel is a warning only
    c = network({1});
    c.sendClock = true;
    NmtBaseStation clock(c);
    CHECK(hasIssue(clock.carriers()[0].issues, false));
    CHECK(clock.carriers()[0].planned);

    // the 20 kHz plans need less bandwidth
    c = network({132});
    c.country = "HU";
    NmtBaseStation hu(c);
    CHECK(std::fabs(hu.carriers()[0].frequency.deviationFactor - 0.8) < 1e-9);
    CHECK(hu.bind({{1, 457.3e6, 1e6}}, {{2, 467.3e6, 1e6}}));
}

static void testBatch()
{
    NmtBaseStation bs(network({1, 5}));
    CarrierBatch rx, tx;

    // not running: the answer is empty
    rx.reset(0, 480, {1, 5});
    bs.process(rx, tx);
    CHECK(tx.carriers.empty() && tx.length == 480);
    CHECK(bs.samples() == 0);

    CHECK(bs.bind({kRx}, {kTx}));
    // the receive stream has more carriers than the network, in another order
    rx.reset(4800, 480, {9, 5, 1});
    bs.process(rx, tx);
    CHECK(tx.sample == 4800 && tx.length == 480);
    CHECK(tx.carriers.size() == 2 && tx.carriers[0] == 1 && tx.carriers[1] == 5);
    CHECK(tx.samples.size() == 960);
    CHECK(bs.samples() == 480);

    // a carrier missing from the batch is still rendered
    rx.reset(5280, 480, {1});
    bs.process(rx, tx);
    CHECK(tx.carriers.size() == 2);
    CHECK(bs.samples() == 960);

    // the idle frames are on both carriers
    double energy[2] = {0.0, 0.0};
    for (int b = 0; b < 20; b++)
    {
        rx.reset(5760 + 480 * static_cast<uint64_t>(b), 480, {1, 5});
        bs.process(rx, tx);
        for (size_t c = 0; c < 2; c++)
        {
            for (int i = 0; i < 480; i++) {
                energy[c] += tx.carrier(c)[i] * tx.carrier(c)[i];
            }
        }
    }
    CHECK(energy[0] > 1.0 && energy[1] > 1.0);
}

// A phone on the audio of the traffic channel: roams in and clears down.
class AudioPhone
{
public:
    AudioPhone(const NmtChannel& channel) :
        m_modem(kRate, 1.0),
        m_channel(channel)
    {
        m_modem.setFrameSource([this](FrameBits& bits) { return next(bits); });
        m_modem.setFrameSink([this](const RxFrame& rx) { heard(rx); });
    }

    NmtModem& modem() { return m_modem; }
    bool done() const { return m_done; }

private:
    bool next(FrameBits& bits)
    {
        if (m_done) {
            return false;
        }
        const Phone phone;
        bits = encodeFrame(m_guard ? lineSignalFrame(m_channel, phone, 0x11111) : roamingFrame(m_channel, phone));
        return true;
    }

    void heard(const RxFrame& rx)
    {
        const Frame f = decodeFrame(rx.bits, Direction::MtxToMs, false);

        if (isLineSignal(f, 15)) {
            m_guard = true;
        }
        if (m_guard && f.mt == MessageType::Frame4) {
            m_done = true;
        }
    }

    NmtModem m_modem;
    const NmtChannel& m_channel;
    bool m_guard = false;
    bool m_done = false;
};

// The data path as the plugins will run it: the receiver hands a batch per block, the network
// answers with a batch for the transmitter, which goes through the queue of the transmitter. The
// transmitter clock is off by ppm. Returns the seconds to the end of the roaming, -1 if it did not end.
static double roamOverTheAir(size_t lead, double ppm, uint64_t& underruns, int& registrations)
{
    NmtBaseStation bs(network({1, 5}));
    CHECK(bs.bind({kRx}, {kTx}));
    NmtSystem& system = *bs.system();
    NmtChannel& tc = system.channel(1);
    AudioPhone phone(tc);
    CarrierQueue queue(lead);
    CarrierBatch rx, tx;
    std::vector<float> air(1024);
    double owed = 0.0;
    const int block = 480;

    registrations = 0;
    system.setRegistrationListener([&](const NmtSubscriber&) { registrations++; });
    for (int i = 0; i < 1500; i++)                  // up to 15 s
    {
        rx.reset(static_cast<uint64_t>(i) * block, block, {1, 5});
        phone.modem().render(rx.carrier(1), block, false);
        bs.process(rx, tx);
        queue.write(tx.carrier(1), static_cast<size_t>(tx.length));

        owed += block * (1.0 + ppm * 1e-6);
        const size_t count = static_cast<size_t>(owed);
        owed -= static_cast<double>(count);
        queue.read(air.data(), count, queue.ratio());
        phone.modem().receive(air.data(), static_cast<int>(count));

        if (phone.done() && tc.state() == NmtChannel::State::Idle)
        {
            underruns = queue.underruns();
            return (i + 1) * block / kRate;
        }
    }
    underruns = queue.underruns();
    return -1.0;
}

static void testOverTheAir()
{
    // a block of lead (what the plugins need at least), 100 ms and 300 ms (the target round trip);
    // the transmitter drifts by 50 ppm either way
    for (size_t lead : {480u, 4800u, 14400u})
    {
        for (double ppm : {-50.0, 50.0})
        {
            uint64_t underruns = 0;
            int registrations = 0;
            const double seconds = roamOverTheAir(lead, ppm, underruns, registrations);

            CHECK(seconds > 0.0);
            CHECK(registrations == 1);
            CHECK(underruns == 0);
            std::printf("lead %5zu samples, drift %+.0f ppm: roaming done after %.2f s\n", lead, ppm, seconds);
        }
    }
}

int main()
{
    testBind();
    testCoverage();
    testPlanErrors();
    testBatch();
    testOverTheAir();

    if (g_failures == 0) {
        std::printf("all base station tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
