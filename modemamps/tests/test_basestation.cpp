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

#include "ampsbasestation.h"
#include "phone.h"

// The user's network (osmocom amps --channel 353 -T CC/PC --channel 356/359/362 -T VC -S sid=54)
static AmpsNetworkConfig network()
{
    AmpsNetworkConfig c;
    c.id = 1;
    c.sid = 54;
    c.carriers = {{353, AmpsChannelType::ControlPaging, 0}, {356, AmpsChannelType::Voice, 0},
        {359, AmpsChannelType::Voice, 0}, {362, AmpsChannelType::Voice, 0}};
    return c;
}

// the streams of osmocom's run: RX centre 835.725 MHz, TX centre 880.725 MHz
static const std::vector<AmpsStreamSpan> kRx = {{1, 835.725e6, 480000.0}};
static const std::vector<AmpsStreamSpan> kTx = {{2, 880.725e6, 480000.0}};

static void testBind()
{
    AmpsBaseStation bs(network());

    CHECK(bs.issues().empty());
    CHECK(bs.bind(kRx, kTx) && bs.running());
    CHECK(bs.rxStream() == 1 && bs.txStream() == 2);
    const auto rx = bs.rxTuning();
    const auto tx = bs.txTuning();
    // all four are heard, only the control channel is sent while the voice channels are idle
    CHECK(rx.size() == 4 && tx.size() == 1);
    if (rx.size() == 4 && tx.size() == 1)
    {
        // osmocom's offsets: -135, -45, +45, +135 kHz
        CHECK(std::fabs(rx[0].offset + 135000.0) < 1.0 && std::fabs(rx[3].offset - 135000.0) < 1.0);
        CHECK(std::fabs(rx[1].offset + 45000.0) < 1.0 && std::fabs(rx[2].offset - 45000.0) < 1.0);
        CHECK(tx[0].id == 10353 && std::fabs(tx[0].offset + 135000.0) < 1.0);
        CHECK(rx[0].id == 10353 && rx[0].passband == 15000.0 && rx[0].spacing == 30000.0);
    }
}

// The control carrier sends the overhead train; the idle voice carriers are not keyed (osmocom: no
// power); a call keys its voice carrier while its designation goes out, and its end takes it off
static void testProcess()
{
    AmpsBaseStation bs(network());
    FoccReader reader;

    bs.bind(kRx, kTx);
    CHECK(bs.txTuning().size() == 1 && bs.rxTuning().size() == 4);
    const auto block = [&bs, &reader](int b) {
        modemcellular::CarrierBatch rx;
        modemcellular::CarrierBatch tx;
        rx.reset(static_cast<uint64_t>(b) * kBlock, kBlock, {10353, 10356, 10359, 10362});
        bs.process(rx, tx);
        reader.process(tx.carrier(0), kBlock);
        return tx;
    };
    for (int b = 0; b < 100; b++)
    {
        const modemcellular::CarrierBatch tx = block(b);
        CHECK(tx.length == kBlock && tx.carriers.size() == 1 && tx.carriers[0] == 10353);
    }
    CHECK(heardWord(reader.words, [](uint64_t w) {
        return wordField(words::kSystemParameter1, w, "OHD") == kOhdSystemParameter1 && wordField(words::kSystemParameter1, w, "SID1") == 27;
    }));
    CHECK(bs.samples() == 100u * kBlock);
    CHECK(bs.txKeyingChanged() && !bs.txKeyingChanged());      // the first block keyed the control carrier

    // the phone calls: its voice carrier is keyed within a few blocks
    ReccMessage m;
    m.t = 1;
    m.min1 = ReccAssembler::min1Of("2177526");
    m.min2 = ReccAssembler::min2Of("216");
    m.dialled = "555";
    bs.system()->setCallHandler([](const AmpsSubscriber&, const std::string&) { return true; });
    bs.system()->receivedRecc(0, m);
    size_t carriers = 0;
    for (int b = 100; b < 120; b++) {
        carriers = block(b).carriers.size();
    }
    CHECK(carriers == 2 && bs.txKeyingChanged() && bs.txTuning().size() == 2);
    CHECK(bs.txTuning()[1].id == 10356);

    // the phone is on the voice channel once the designation is sent; its release (the tone of
    // 1.67 s, 170 blocks of 10 ms): off again
    int b0 = 120;
    while (b0 < 200 && (bs.system()->transactions().empty()
        || bs.system()->transactions()[0].state != AmpsSystem::State::MoAssignConfirm)) {
        block(b0++);
    }
    bs.system()->satChanged(1, 0);
    CHECK(bs.system()->transactions().size() == 1 && bs.system()->transactions()[0].state == AmpsSystem::State::Call);
    bs.system()->signallingTone(1, true);
    for (int b = b0; b < b0 + 170; b++) {
        block(b);
    }
    bs.system()->signallingTone(1, false);
    for (int b = b0 + 170; b < b0 + 172; b++) {
        carriers = block(b).carriers.size();
    }
    CHECK(carriers == 1 && bs.txKeyingChanged() && bs.txTuning().size() == 1);
}

// What keeps the network off
static void testRefused()
{
    AmpsNetworkConfig twoControl = network();
    twoControl.carriers.push_back({334, AmpsChannelType::ControlPaging, 0});
    AmpsBaseStation a(twoControl);
    CHECK(!a.issues().empty() && !a.bind(kRx, kTx));

    AmpsNetworkConfig unknown = network();
    unknown.carriers.push_back({900, AmpsChannelType::Voice, 0});
    AmpsBaseStation b(unknown);
    CHECK(b.bind(kRx, kTx) && !b.carriers()[4].planned && !b.carriers()[4].onAir);

    // a voice channel outside the stream: the others go on air
    AmpsNetworkConfig far = network();
    far.carriers.push_back({700, AmpsChannelType::Voice, 0});
    AmpsBaseStation c(far);
    CHECK(c.bind(kRx, kTx) && !c.carriers()[4].onAir && !c.carriers()[4].streamIssues.empty());

    // on two streams (each covers only some of the carriers): off
    AmpsBaseStation d(network());
    CHECK(!d.bind({{1, 835.635e6, 192000.0}, {3, 835.815e6, 192000.0}}, {{2, 880.635e6, 192000.0}, {4, 880.815e6, 192000.0}}));
    CHECK(!d.running() && !d.streamIssues().empty());

    // the control channel not on air: off
    AmpsNetworkConfig noControl;
    noControl.sid = 54;
    noControl.carriers = {{353, AmpsChannelType::ControlPaging, 0}, {356, AmpsChannelType::Voice, 0}};
    AmpsBaseStation e(noControl);
    CHECK(!e.bind({{1, 835.680e6, 96000.0}}, {{2, 880.680e6, 96000.0}}) && !e.running());
}

int main()
{
    testBind();
    testProcess();
    testRefused();

    if (g_failures == 0) {
        std::printf("all base station tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
