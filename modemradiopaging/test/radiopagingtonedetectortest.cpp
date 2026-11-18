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

// Self test of the tone detector, the carrier offset and deviation estimator and the decision given to the FSK
// demodulator: the output, strongest tone and quality for each tone, the estimates for 2-level and 4-level FSK
// (and a 2-level signal not taken for 4-level at 1.5 times its deviation), the quality of noise, and the
// strongest tone taken as the symbol

#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "radiopagingfskdemod.h"
#include "radiopagingfskmod.h"
#include "radiopagingtonedetector.h"

namespace {

const double fs = 38400.0;
int passes = 0;
int failures = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
    (ok ? passes : failures)++;
}

// A tone at offset Hz for n samples into the detector; the last output
float feedTone(RadioPagingToneDetector& d, double offset, int n)
{
    float out = 0.0f;

    for (int i = 0; i < n; i++)
    {
        double phase = 2.0 * M_PI * offset * i / fs;
        out = d.process(std::complex<float>((float) std::cos(phase), (float) std::sin(phase)));
    }

    return out;
}

void testTones()
{
    const double dev = 4800.0;

    for (int levels : {2, 4})
    {
        const std::vector<double> tones = (levels == 2) ? std::vector<double>{-dev, dev}
            : std::vector<double>{-dev, -dev / 3.0, dev / 3.0, dev};
        const std::vector<float> values = (levels == 2) ? std::vector<float>{-1.0f, 1.0f}
            : std::vector<float>{-1.0f, -1.0f / 3.0f, 1.0f / 3.0f, 1.0f};

        for (size_t k = 0; k < tones.size(); k++)
        {
            RadioPagingToneDetector d;
            d.create(fs, dev, 24, levels);   // A symbol at 1600 baud: the tones are orthogonal
            float out = feedTone(d, tones[k], 48);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "detector %d levels, tone %+.0f Hz: output %.3f, strongest %d, quality %.0f",
                levels, tones[k], out, d.strongest(), d.quality());
            check((std::fabs(out - values[k]) < 0.02f) && (d.strongest() == (int) k) && (d.quality() > 100.0f), buf);
        }
    }

    // Noise: the strongest tone over the others is about 1 (log10 about 0.6 at most over many symbols)
    RadioPagingToneDetector d;
    d.create(fs, 4800.0, 24, 2);
    std::mt19937 rng(5);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    double sum = 0.0;
    const int n = 38400;

    for (int i = 0; i < n; i++)
    {
        d.process(std::complex<float>(gauss(rng), gauss(rng)));
        sum += std::log10(std::max(1e-3f, d.quality()));
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf), "detector: quality of noise, mean log10 %.3f", sum / n);
    check((sum / n > 0.2) && (sum / n < 0.7), buf);
}

// Random FSK at a carrier offset into the estimator for a second; the estimate
void estimate(int txLevels, int estimatorLevels, double baud, double shaping, double deviation, double offset, double& estOffset, double& estDeviation)
{
    RadioPagingFskMod mod;
    mod.setChannelSampleRate((int) fs);
    mod.setDeviation(deviation);
    mod.setShaping(shaping);
    std::mt19937 rng(11);
    std::vector<float> levels;
    static const float values4[4] = {-1.0f, -1.0f / 3.0f, 1.0f / 3.0f, 1.0f};

    for (int i = 0; i < (int) baud; i++) {
        levels.push_back((txLevels == 4) ? values4[rng() & 3] : ((rng() & 1) ? 1.0f : -1.0f));
    }
    mod.push(baud, levels);

    RadioPagingToneOffset est;
    est.create(fs, 2000.0, 6000.0, 0.25, 5000.0, estimatorLevels);
    double phase = 0.0;

    while (mod.busy())
    {
        phase += 2.0 * M_PI * offset / fs;
        Complex c = mod.next() * Complex((float) std::cos(phase), (float) std::sin(phase));
        est.process(std::complex<float>(c.real(), c.imag()));
    }

    estOffset = est.offset();
    estDeviation = est.deviation();
}

void testEstimator()
{
    // The shaping of each protocol: POCSAG at the baud rate, FLEX at 3200 Hz
    struct Case { const char *m_name; int m_txLevels; int m_levels; double m_baud; double m_shaping; double m_deviation; double m_offset; };
    const Case cases[] = {
        {"POCSAG 1200 at 4500 Hz, +1000 Hz", 2, 2, 1200, 1200, 4500, 1000},
        {"FLEX 1600/2 at 4800 Hz, -2000 Hz", 2, 4, 1600, 3200, 4800, -2000},
        {"FLEX 6400/4 at 4800 Hz, +500 Hz", 4, 4, 3200, 3200, 4800, 500},
        // A fit at -/+2000 Hz and 6000 Hz deviation also has a tone on each true one: not taken
        {"FLEX 1600/2 at 4000 Hz, 4-level estimator", 2, 4, 1600, 3200, 4000, 0},
        {"FLEX 1600/2 at 3500 Hz, 4-level estimator", 2, 4, 1600, 3200, 3500, 0},
    };

    for (const Case& c : cases)
    {
        double offset, deviation;
        estimate(c.m_txLevels, c.m_levels, c.m_baud, c.m_shaping, c.m_deviation, c.m_offset, offset, deviation);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "estimator: %s: offset %.0f Hz, deviation %.0f Hz", c.m_name, offset, deviation);
        // The shaped symbols spend part of their time between the levels: the power is a little inside the
        // deviation (up to about 5 % for 2-level FLEX at 3500 Hz), which the detectors take
        check((std::fabs(offset - c.m_offset) <= 80.0) && (std::fabs(deviation - c.m_deviation) <= 0.05 * c.m_deviation), buf);
    }
}

void testDecision()
{
    // The decision given with the samples is the symbol; without one the symbol is decided from the samples
    RadioPagingFskDemod demod;
    demod.create(fs, 3200.0, 4, 12);
    demod.setLevels(0.0f, 1.0f);
    demod.setHoldLevels(true);
    RadioPagingFskDemod::Symbol symbol;
    int given = 0, decided = 0, symbols = 0;

    for (int i = 0; i < 1200; i++)
    {
        if (demod.process(1.0f, 1, symbol))
        {
            symbols++;
            given += (symbol.m_value == 1);
        }
    }
    for (int i = 0; i < 1200; i++)
    {
        if (demod.process(1.0f, -1, symbol)) {
            decided += (symbol.m_value == 3);
        }
    }

    char buf[96];
    std::snprintf(buf, sizeof(buf), "demodulator: the given decision is the symbol (%d of %d), without one the level (%d)", given, symbols, decided);
    check((symbols > 90) && (given == symbols) && (decided > 90), buf);
}

} // namespace

int main()
{
    testTones();
    testEstimator();
    testDecision();

    std::printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
