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

#include <algorithm>
#include <cmath>
#include <complex>
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
        carrier.supervisory = static_cast<int>(i % 4) + 1;     // every traffic channel has a tone
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
    NmtBaseStation bs(network({1, 401, 1, 5}));
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

    // a test network: Y1 0..15 in place of the country's, Y2 0..15, with a warning
    c = network({1});
    c.y1 = 0;
    c.area = 0;
    NmtBaseStation y1zero(c);
    CHECK(!hasIssue(y1zero.issues(), true) && hasIssue(y1zero.issues(), false));
    CHECK(y1zero.bind({kRx}, {kTx}));
    CHECK(y1zero.system()->channel(0).config().trafficArea == 0x00);

    c = network({1});
    c.y1 = 15;
    c.area = 3;
    NmtBaseStation y1high(c);
    CHECK(y1high.bind({kRx}, {kTx}));
    CHECK(y1high.system()->channel(0).config().trafficArea == 0xf3);

    c = network({1});
    c.y1 = 16;
    NmtBaseStation y1bad(c);
    CHECK(hasIssue(y1bad.issues(), true));
    CHECK(!y1bad.bind({kRx}, {kTx}));

    c = network({1});
    c.system = System::Nmt900;
    c.y1 = 8;
    NmtBaseStation y1nine(c);
    CHECK(hasIssue(y1nine.issues(), true));

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

    // what a network needs: a calling channel and a traffic channel (warnings)
    c = network({5, 6});
    c.carriers[0].type = ChannelType::Tc;
    NmtBaseStation noCalling(c);
    CHECK(noCalling.issues().size() == 1 && !noCalling.issues()[0].error);
    CHECK(noCalling.issues()[0].text.find("no calling channel") == 0);
    CHECK(noCalling.bind({kRx}, {kTx}));
    c.carriers[0].type = ChannelType::Cc;
    c.carriers[1].type = ChannelType::Cc;
    NmtBaseStation noTraffic(c);
    CHECK(noTraffic.issues().size() == 1 && noTraffic.issues()[0].text.find("no traffic channel") == 0);

    // a type of NMT-900 on NMT-450, or a supervisory tone on a calling channel: the carrier is off
    c = network({1, 5});
    c.carriers[1].type = ChannelType::AcTc;
    NmtBaseStation wrongType(c);
    CHECK(!wrongType.carriers()[1].planned && hasIssue(wrongType.carriers()[1].issues, true));
    c = network({1, 5});
    c.carriers[0].type = ChannelType::Cc;
    c.carriers[0].supervisory = 1;
    c.carriers[1].supervisory = 2;
    NmtBaseStation toneOnCc(c);
    CHECK(!toneOnCc.carriers()[0].planned && toneOnCc.carriers()[1].planned);

    // an interleaved channel 12.5 kHz from another: a warning
    NmtBaseStation interleaved(network({1, 201}));
    CHECK(interleaved.carriers()[1].planned);
    bool near = false;
    for (const NmtPlanIssue& issue : interleaved.carriers()[1].issues) {
        near = near || issue.text.find("less than 25 kHz") != std::string::npos;
    }
    CHECK(near);
    NmtBaseStation apart(network({1, 2}));
    CHECK(apart.carriers()[1].issues.empty());

    // a traffic channel without a supervisory tone: a warning, the carrier is on
    c = network({1, 5});
    c.carriers[1].supervisory = 0;
    NmtBaseStation noTone(c);
    CHECK(noTone.carriers()[1].planned && noTone.carriers()[0].issues.empty());
    CHECK(noTone.carriers()[1].issues.size() == 1 && !noTone.carriers()[1].issues[0].error);
    CHECK(noTone.carriers()[1].issues[0].text.find("no supervisory tone") == 0);
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

    // the idle frames are on both carriers, at the deviation of the signalling: 4.2 kHz at
    // 1800 Hz after the pre-emphasis, below the 4.7 kHz the plan allows
    double peak[2] = {0.0, 0.0};
    for (int b = 0; b < 50; b++)
    {
        rx.reset(5760 + 480 * static_cast<uint64_t>(b), 480, {1, 5});
        bs.process(rx, tx);
        for (size_t c = 0; c < 2; c++)
        {
            for (int i = 0; i < 480; i++) {
                peak[c] = std::max(peak[c], static_cast<double>(std::fabs(tx.carrier(c)[i])));
            }
        }
    }
    std::printf("idle frames: peak deviation %.0f Hz and %.0f Hz\n", peak[0], peak[1]);
    CHECK(peak[0] > 3800.0 && peak[0] < 4700.0);
    CHECK(peak[1] > 3800.0 && peak[1] < 4700.0);

    // a 20 kHz plan: the deviation goes down by its factor (0.8)
    NmtNetworkConfig c = network({132});
    c.country = "HU";
    NmtBaseStation hu(c);
    CHECK(hu.bind({{1, 457.3e6, 1e6}}, {{2, 467.3e6, 1e6}}));
    double huPeak = 0.0;
    for (int b = 0; b < 50; b++)
    {
        rx.reset(480 * static_cast<uint64_t>(b), 480, {132});
        hu.process(rx, tx);
        for (int i = 0; i < 480; i++) {
            huPeak = std::max(huPeak, static_cast<double>(std::fabs(tx.carrier(0)[i])));
        }
    }
    CHECK(std::fabs(huPeak - 0.8 * peak[0]) < 0.05 * peak[0]);

    // the carrier ids carry the network id
    NmtNetworkConfig second = network({1});
    second.id = 3;
    NmtBaseStation other(second);
    CHECK(other.bind({kRx}, {kTx}));
    CHECK(other.carrierId(0) == 30001);
    CHECK(other.rxTuning().size() == 1 && other.rxTuning()[0].id == 30001);
    CHECK(std::fabs(other.rxTuning()[0].offset - (453.0e6 - kRx.centre)) < 1.0);
    CHECK(std::fabs(other.txTuning()[0].offset - (463.0e6 - kTx.centre)) < 1.0);
    CHECK(std::fabs(other.rxTuning()[0].spacing - 25000.0) < 1.0);
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
    // the signalling level of the last frame heard, 1.0 is that of a base station
    double level() const { return m_level; }

    // The radio of the phone: the frequency deviation in Hz to and from the base station, with
    // the emphasis and the speech deviation (3 kHz at 1 kHz) of the Scandinavian plan.
    void transmit(float* hz, int count)
    {
        m_modem.render(hz, count, false);
        m_txEmphasis.pre(hz, count);
        for (int i = 0; i < count; i++) {
            hz[i] *= static_cast<float>(NmtBaseStation::kSpeechDeviation);
        }
    }
    void listen(const float* hz, int count)
    {
        m_audio.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; i++) {
            m_audio[static_cast<size_t>(i)] = hz[i] / static_cast<float>(NmtBaseStation::kSpeechDeviation);
        }
        m_rxEmphasis.de(m_audio.data(), count);
        m_modem.receive(m_audio.data(), count);
    }

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

        m_level = rx.level;
        if (isLineSignal(f, 15)) {
            m_guard = true;
        }
        if (m_guard && f.mt == MessageType::Frame4) {
            m_done = true;
        }
    }

    NmtModem m_modem;
    const NmtChannel& m_channel;
    Emphasis m_txEmphasis{kRate};
    Emphasis m_rxEmphasis{kRate};
    std::vector<float> m_audio;
    double m_level = 0.0;
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
    double baseLevel = 0.0;

    registrations = 0;
    system.setRegistrationListener([&](const NmtSubscriber&) { registrations++; });
    tc.setFrameListener([&](const NmtChannel::ReceivedFrame& f) { baseLevel = f.level; });
    for (int i = 0; i < 1500; i++)                  // up to 15 s
    {
        rx.reset(static_cast<uint64_t>(i) * block, block, {1, 5});
        phone.transmit(rx.carrier(1), block);
        bs.process(rx, tx);
        queue.write(tx.carrier(1), static_cast<size_t>(tx.length));

        owed += block * (1.0 + ppm * 1e-6);
        const size_t count = static_cast<size_t>(owed);
        owed -= static_cast<double>(count);
        queue.read(air.data(), count, queue.ratio());
        phone.listen(air.data(), static_cast<int>(count));

        if (phone.done() && tc.state() == NmtChannel::State::Idle)
        {
            underruns = queue.underruns();
            // both ends hear the signalling at the level of a base station
            CHECK(baseLevel > 0.85 && baseLevel < 1.15);
            CHECK(phone.level() > 0.85 && phone.level() < 1.15);
            return (i + 1) * block / kRate;
        }
    }
    underruns = queue.underruns();
    return -1.0;
}

// The whole chain on the air: the phone transmits on the uplink of its channel into the receive
// stream, the receiver of the stream hands its batches to the base station, the transmitter of the
// stream renders the downlinks of both carriers from their queues, and the phone receives its
// downlink out of the transmit stream.
static void testRadioPath()
{
    const double rate = 960000.0;
    const NmtStreamSpan rxSpan = {1, 453.3e6, rate};
    const NmtStreamSpan txSpan = {2, 463.3e6, rate};
    NmtBaseStation bs(network({1, 5}));
    CHECK(bs.bind({rxSpan}, {txSpan}));
    NmtSystem& system = *bs.system();
    NmtChannel& tc = system.channel(1);
    AudioPhone phone(tc);
    int registrations = 0;
    double baseLevel = 0.0;
    system.setRegistrationListener([&](const NmtSubscriber&) { registrations++; });
    tc.setFrameListener([&](const NmtChannel::ReceivedFrame& f) { baseLevel = f.level; });

    const std::vector<modemcellular::CarrierTuning> rxTuning = bs.rxTuning();
    const std::vector<modemcellular::CarrierTuning> txTuning = bs.txTuning();
    CHECK(rxTuning.size() == 2 && txTuning.size() == 2);

    // the base station side
    modemcellular::CarrierReceiver bsRx(rate);
    modemcellular::CarrierTransmitter bsTx(rate);
    std::vector<std::unique_ptr<CarrierQueue>> queues;
    for (size_t i = 0; i < txTuning.size(); i++) {
        queues.push_back(std::make_unique<CarrierQueue>(modemcellular::CarrierRadio::kBlock));
    }
    CarrierBatch tx;
    bsRx.setCarriers(rxTuning);
    bsTx.setCarriers(txTuning);
    bsRx.setBatchHandler([&](const CarrierBatch& rx) {
        bs.process(rx, tx);
        for (size_t i = 0; i < queues.size(); i++) {
            queues[i]->write(tx.carrier(i), static_cast<size_t>(tx.length));
        }
    });
    bsTx.setAudioSource([&](size_t index, float* audio, size_t count) {
        queues[index]->read(audio, count, 1.0);
    });

    // the phone on channel 5
    modemcellular::CarrierTransmitter phoneTx(rate);
    modemcellular::CarrierReceiver phoneRx(rate);
    phoneTx.setCarriers({rxTuning[1]});
    phoneRx.setCarriers({txTuning[1]});
    phoneTx.setAudioSource([&](size_t, float* audio, size_t count) {
        phone.transmit(audio, static_cast<int>(count));
    });
    phoneRx.setBatchHandler([&](const CarrierBatch& b) {
        phone.listen(b.carrier(0), b.length);
    });

    std::vector<std::complex<float>> up(9600), down(9600);
    bool finished = false;
    int blocks = 0;
    for (; blocks < 1000 && !finished; blocks++)      // up to 10 s
    {
        phoneTx.pull(up.data(), up.size());
        bsRx.feed(up.data(), up.size());
        bsTx.pull(down.data(), down.size());
        phoneRx.feed(down.data(), down.size());
        finished = phone.done() && tc.state() == NmtChannel::State::Idle;
    }

    std::printf("radio path: roaming done after %.2f s, levels %.2f at the base station, %.2f at the phone\n",
        blocks * 0.01, baseLevel, phone.level());
    CHECK(finished);
    CHECK(registrations == 1);
    CHECK(system.isRegistered('1', "123456"));
    CHECK(baseLevel > 0.85 && baseLevel < 1.15);
    CHECK(phone.level() > 0.85 && phone.level() < 1.15);
    for (const std::unique_ptr<CarrierQueue>& q : queues) {
        CHECK(q->underruns() == 0);
    }
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
    testRadioPath();

    if (g_failures == 0) {
        std::printf("all base station tests passed\n");
    }

    return g_failures == 0 ? 0 : 1;
}
