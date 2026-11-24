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

#include "speechmeter.h"

#include <algorithm>
#include <cmath>

#include "dsp/iirfilter.h"

namespace modemcellular
{

const std::array<double, SpeechStats::kBands + 1> SpeechStats::kBandEdges = {300.0, 500.0, 800.0, 1200.0, 2000.0, 3000.0, 3400.0};

namespace
{

const double kBinDb = (SpeechStats::kHighDb - SpeechStats::kLowDb) / SpeechStats::kBins;

double toDb(double ratio)
{
    return ratio > 0.0 ? 10.0 * std::log10(ratio) : -100.0;
}

} // namespace

void SpeechStats::add(const SpeechStats& other)
{
    for (int i = 0; i < kBins; i++) {
        bins[i] += other.bins[i];
    }
    speech += other.speech;
    pause += other.pause;
    atLimit += other.atLimit;
    energy += other.energy;
    for (int i = 0; i < kBands; i++) {
        bandEnergy[i] += other.bandEnergy[i];
    }
    peak = std::max(peak, other.peak);
}

double SpeechStats::pauseShare() const
{
    const uint64_t all = speech + pause;

    return all ? static_cast<double>(pause) / static_cast<double>(all) : 0.0;
}

double SpeechStats::p99Db() const
{
    if (speech == 0) {
        return kLowDb;
    }
    const double wanted = 0.99 * static_cast<double>(speech);
    double counted = 0.0;

    for (int i = 0; i < kBins; i++)
    {
        counted += static_cast<double>(bins[i]);
        if (counted >= wanted) {
            return kLowDb + (i + 1) * kBinDb;
        }
    }
    return kHighDb;
}

double SpeechStats::limitShare() const
{
    return speech ? static_cast<double>(atLimit) / static_cast<double>(speech) : 0.0;
}

double SpeechStats::levelDb() const
{
    return speech ? toDb(energy / static_cast<double>(speech)) : kLowDb;
}

std::array<double, SpeechStats::kBands> SpeechStats::bandsDb() const
{
    std::array<double, kBands> db;

    for (int i = 0; i < kBands; i++) {
        db[i] = energy > 0.0 ? toDb(bandEnergy[i] / energy) : -100.0;
    }
    return db;
}

// Each band is two identical band pass sections (RBJ, 0 dB at the centre) whose cascade is 3 dB
// down at the band edges: the bands meet at -3 dB.
SpeechMeter::SpeechMeter(double gate, double limit) :
    m_gate(gate),
    m_limit(limit),
    m_blockFill(0),
    m_step(0),
    m_stepFill(0)
{
    const double pi = 3.14159265358979323846;

    for (int band = 0; band < SpeechStats::kBands; band++)
    {
        const double f1 = SpeechStats::kBandEdges[band];
        const double f2 = SpeechStats::kBandEdges[band + 1];
        const double f0 = std::sqrt(f1 * f2);
        const double q = f0 / (f2 - f1) * std::sqrt(std::sqrt(2.0) - 1.0);
        const double w0 = 2.0 * pi * f0 / kSampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        // IIRFilter: y = b0 x + b1 x1 + b2 x2 + a1 y1 + a2 y2
        const double b[3] = {alpha / a0, 0.0, -alpha / a0};
        const double a[3] = {1.0, 2.0 * std::cos(w0) / a0, -(1.0 - alpha) / a0};

        m_coeffs.push_back({{a[0], a[1], a[2]}, {b[0], b[1], b[2]}});
        for (int section = 0; section < 2; section++) {
            m_filters.push_back(std::make_unique<IIRFilter<double, 2>>(a, b));
        }
    }
    reset();
}

SpeechMeter::~SpeechMeter()
{
}

void SpeechMeter::reset()
{
    // setting the coefficients clears the state of a filter
    for (size_t i = 0; i < m_filters.size(); i++) {
        m_filters[i]->setCoeffs(m_coeffs[i / 2].a.data(), m_coeffs[i / 2].b.data());
    }
    std::fill(std::begin(m_bandBlock), std::end(m_bandBlock), 0.0);
    m_blockFill = 0;
    m_steps.fill(SpeechStats());
    m_step = 0;
    m_stepFill = 0;
    m_call = SpeechStats();
}

void SpeechMeter::process(const float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        const double x = samples[i];

        m_block[m_blockFill++] = samples[i];
        for (int band = 0; band < SpeechStats::kBands; band++)
        {
            const double y = m_filters[2 * band + 1]->run(m_filters[2 * band]->run(x));
            m_bandBlock[band] += y * y;
        }
        if (m_blockFill == kBlock) {
            endBlock();
        }
    }
}

void SpeechMeter::endBlock()
{
    SpeechStats block;
    double sum = 0.0;

    for (int i = 0; i < kBlock; i++) {
        sum += static_cast<double>(m_block[i]) * m_block[i];
    }
    if (std::sqrt(sum / kBlock) < m_gate)
    {
        block.pause = kBlock;
    }
    else
    {
        const double limitDb = 20.0 * std::log10(m_limit);

        block.speech = kBlock;
        block.energy = sum;
        for (int i = 0; i < kBlock; i++)
        {
            const double a = std::fabs(static_cast<double>(m_block[i]));
            const double db = a > 0.0 ? 20.0 * std::log10(a) : -200.0;
            const int bin = std::max(0, std::min(SpeechStats::kBins - 1, static_cast<int>(std::floor((db - SpeechStats::kLowDb) / kBinDb))));

            block.bins[bin]++;
            if (db >= limitDb) {
                block.atLimit++;
            }
            block.peak = std::max(block.peak, a);
        }
        for (int band = 0; band < SpeechStats::kBands; band++) {
            block.bandEnergy[band] = m_bandBlock[band];
        }
    }
    std::fill(std::begin(m_bandBlock), std::end(m_bandBlock), 0.0);
    m_blockFill = 0;

    m_steps[m_step].add(block);
    m_call.add(block);
    m_stepFill += kBlock;
    if (m_stepFill >= kStep)
    {
        m_step = (m_step + 1) % kSteps;
        m_steps[m_step] = SpeechStats();
        m_stepFill = 0;
    }
}

SpeechStats SpeechMeter::recent() const
{
    SpeechStats all;

    for (const SpeechStats& s : m_steps) {
        all.add(s);
    }
    return all;
}

} // namespace modemcellular
