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
#include <cstdio>
#include <vector>

#include "ampsspeech.h"
#include "ampsvoice.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kPi = 3.14159265358979323846;
static const double kRate = 48000.0;

// the amplitude of a frequency in the second half of x
static double amplitude(const std::vector<float>& x, double hz, double rate)
{
    double re = 0.0, im = 0.0;
    const size_t from = x.size() / 2;

    for (size_t i = from; i < x.size(); i++)
    {
        re += x[i] * std::cos(2.0 * kPi * hz * i / rate);
        im += x[i] * std::sin(2.0 * kPi * hz * i / rate);
    }
    return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(x.size() - from);
}

// one second of a 1 kHz tone of level (1.0 = 2.9 kHz deviation) with the SAT, through the receiver
static std::vector<float> receive(double level)
{
    AmpsRxSpeech rx(kRate);
    SatTone sat(kRate, 1);
    std::vector<float> out;
    std::vector<float> block(480);
    int n = 0;

    rx.setSink([&out](float* s, int count) { out.insert(out.end(), s, s + count); });
    for (int b = 0; b < 100; b++)
    {
        for (auto& v : block) {
            v = static_cast<float>(level * kSpeechDeviation * std::sin(2.0 * kPi * 1000.0 * n++ / kRate));
        }
        sat.add(block.data(), 480);
        rx.process(block.data(), 480);
    }
    return out;
}

// From the radio: the speech level stays (the de-emphasis and the expander are neutral at 1 kHz and
// 1.0), the SAT is gone, below the gate nothing
static void testReceive()
{
    const std::vector<float> out = receive(1.0);

    CHECK(out.size() == 8000);
    const double a = amplitude(out, 1000.0, 8000.0);
    CHECK(a > 0.85 && a < 1.15);
    CHECK(amplitude(out, 2000.0, 8000.0) < 0.02);       // 6 kHz would fold to 2 kHz

    const std::vector<float> quiet = receive(0.05);
    CHECK(amplitude(quiet, 1000.0, 8000.0) < 0.01);
}

// one second of the call's 1 kHz at level towards the radio
static std::vector<float> transmit(double level, double hz, AmpsTxSpeech& tx)
{
    int n = 0;
    std::vector<float> out(48000);

    tx.setPull([&](float* s, int count) {
        for (int i = 0; i < count; i++) {
            s[i] = static_cast<float>(level * std::sin(2.0 * kPi * hz * n++ / 8000.0));
        }
    });
    for (int b = 0; b < 100; b++) {
        tx.generate(out.data() + b * 480, 480);
    }
    return out;
}

// Towards the radio: +3 dB, the compressor, the pre-emphasis; a loud tone held at 12 kHz
static void testTransmit()
{
    AmpsTxSpeech tx(kRate);
    const std::vector<float> out = transmit(1.0, 1000.0, tx);
    const double a = amplitude(out, 1000.0, kRate) / kSpeechDeviation;
    // +3 dB and the 2:1 compressor: 1.414^0.5 = 1.189; the shelf (318.3 us to 2 kHz) is 2.13 at
    // 1 kHz, halved 1.066 (osmocom's scale of 0.5 takes 2.0 out): 1.267
    const double expected = std::sqrt(std::pow(10.0, kTxGainDb / 20.0)) * 1.066;

    CHECK(std::fabs(a / expected - 1.0) < 0.03);
    CHECK(tx.limited() == 0);

    AmpsTxSpeech loud(kRate);
    const std::vector<float> l = transmit(20.0, 3000.0, loud);
    double peak = 0.0;
    for (float v : l) {
        peak = std::max(peak, static_cast<double>(std::fabs(v)));
    }
    CHECK(peak <= kVoiceMaxDeviation + 1.0 && peak > 0.9 * kVoiceMaxDeviation);
    CHECK(loud.limited() > 0);
}

int main()
{
    testReceive();
    testTransmit();

    if (g_failures == 0) {
        std::printf("all speech tests passed\n");
    }
    return g_failures == 0 ? 0 : 1;
}
