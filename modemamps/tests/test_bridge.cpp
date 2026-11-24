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
#include <memory>

#include "ampscellularbridge.h"
#include "phone.h"

// The AMPS system driven by its events (no signal), bridged to modemcellular's router
struct Station
{
    modemcellular::SampleClock clock{kRate};
    AmpsSystem amps;
    modemcellular::CellularSystem cellular;
    std::unique_ptr<AmpsCellularBridge> bridge;

    static AmpsSystemConfig config()
    {
        AmpsSystemConfig c;
        c.si.sid = 54;
        c.channels = {{353, AmpsChannelType::ControlPaging, 0}, {356, AmpsChannelType::Voice, 0},
            {359, AmpsChannelType::Voice, 0}};
        return c;
    }

    Station() :
        amps(clock, config()),
        cellular(clock, modemcellular::ToneSet::American),
        bridge(std::make_unique<AmpsCellularBridge>(amps, cellular))
    {
    }

    static ReccMessage message(const std::string& number, uint8_t order, uint8_t ordq, uint8_t msgType, const std::string& dialled)
    {
        ReccMessage m;
        m.t = 1;
        m.order = order;
        m.ordq = ordq;
        m.msgType = msgType;
        m.min2 = ReccAssembler::min2Of(number.substr(0, 3));
        m.min1 = ReccAssembler::min1Of(number.substr(3));
        m.dialled = dialled;
        return m;
    }

    void registration(const std::string& number, bool powerDown)
    {
        FoccMessage f;
        amps.receivedRecc(0, message(number, kOrderRegistration, 3, powerDown ? 1 : 0, ""));
        amps.nextFocc(0, f);
        amps.nextFocc(0, f);
    }

    // the phone calls: designation sent, the phone on the voice channel with its SAT
    void call(const std::string& number, const std::string& dialled, int vc)
    {
        FoccMessage f;
        amps.receivedRecc(0, message(number, kOrderPage, 0, 0, dialled));
        amps.nextFocc(0, f);
        amps.nextFocc(0, f);
        amps.satChanged(vc, 0);
    }

    // 20 ms of the call: the phone's speech in, ours out
    void step(int vc, double t, bool speaking, std::vector<float>& received)
    {
        float in[160];
        float out[160];
        for (int i = 0; i < 160; i++) {
            in[i] = speaking ? static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * (t + i / 8000.0))) : 0.0f;
        }
        amps.audioFromMobile(vc, in, 160);
        amps.audioToMobile(vc, out, 160);
        received.insert(received.end(), out, out + 160);
        clock.advance(960);
        amps.run();
        bridge->run();
    }
};

static double amplitude(const std::vector<float>& x, double from, double to)
{
    double re = 0.0, im = 0.0;
    const size_t a = static_cast<size_t>(from * 8000.0);
    const size_t b = std::min(x.size(), static_cast<size_t>(to * 8000.0));
    for (size_t i = a; i < b; i++)
    {
        re += x[i] * std::cos(2.0 * kPi * 1000.0 * i / 8000.0);
        im += x[i] * std::sin(2.0 * kPi * 1000.0 * i / 8000.0);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(b - a);
}

static void testRegistration()
{
    Station s;

    CHECK(!s.cellular.isRegistered("2162177526"));
    s.registration("2162177526", false);
    CHECK(s.cellular.isRegistered("2162177526"));
    s.registration("2162177526", true);
    CHECK(!s.cellular.isRegistered("2162177526"));
}

// A call to the echo: what the phone says comes back a second later; its release ends the call
static void testEcho()
{
    Station s;
    std::vector<float> received;

    s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
    s.cellular.setAnswerDelay(0.5);
    s.registration("2162177526", false);
    s.call("2162177526", "555", 1);
    CHECK(s.cellular.callCount() == 1 && s.amps.mode(1) == AmpsMode::Audio);

    for (int k = 0; k < 250; k++)
    {
        const double t = k * 0.02;
        s.step(1, t, t >= 1.6 && t < 2.6, received);
    }
    CHECK(amplitude(received, 0.0, 2.5) < 0.01);
    CHECK(amplitude(received, 2.8, 3.4) > 0.2);         // one second later
    CHECK(amplitude(received, 3.8, 5.0) < 0.01);

    s.amps.signallingTone(1, true);
    s.clock.advance(48000);
    s.amps.signallingTone(1, false);
    s.bridge->run();
    CHECK(s.cellular.callCount() == 0 && s.amps.mode(1) == AmpsMode::Off);
}

// One phone calls another: the router calls its leg; paging is not done yet, so that leg releases
// at once; the caller hears the tone of the cause until the router's tone timeout, then its call
// ends (our release on its voice channel)
static void testCallToPhone()
{
    Station s;
    std::vector<float> received;

    s.cellular.setToneTimeout(1.0);
    s.registration("2162177526", false);
    s.registration("2165550000", false);
    s.call("2165550000", "2162177526", 1);
    CHECK(s.cellular.callCount() == 1);
    for (int k = 0; k < 25; k++) {
        s.step(1, k * 0.02, false, received);
    }
    double power = 0.0;
    for (float v : received) {
        power += v * v;
    }
    CHECK(power > 0.0);                                     // the tone
    CHECK(s.amps.mode(1) == AmpsMode::Audio);
    for (int k = 25; k < 100; k++) {
        s.step(1, k * 0.02, false, received);
    }
    CHECK(s.cellular.callCount() == 0);
    CHECK(s.amps.mode(1) == AmpsMode::Frame);
}

// The bridge goes with a phone in a call: the phone is released
static void testBridgeGone()
{
    Station s;

    s.cellular.setFallback(modemcellular::CallRouter::Fallback::Echo);
    s.registration("2162177526", false);
    s.call("2162177526", "555", 1);
    s.bridge.reset();
    CHECK(s.amps.mode(1) == AmpsMode::Frame);
}

int main()
{
    testRegistration();
    testEcho();
    testCallToPhone();
    testBridgeGone();

    if (g_failures == 0) {
        std::printf("all bridge tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
