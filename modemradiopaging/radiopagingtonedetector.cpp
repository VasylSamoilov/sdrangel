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

#include <algorithm>
#include <cmath>

#include "radiopagingtonedetector.h"

RadioPagingToneDetector::RadioPagingToneDetector() :
    m_phaseStep(0.0),
    m_phase(0.0),
    m_window(1),
    m_levels(2),
    m_pos(0),
    m_sinceResum(0)
{
}

void RadioPagingToneDetector::create(double sampleRate, double deviation, int window, int levels)
{
    setDeviation(sampleRate, deviation);
    m_window = std::max(1, window);
    m_levels = (levels == 4) ? 4 : 2;
    reset();
}

void RadioPagingToneDetector::reset()
{
    m_phase = 0.0;
    m_pos = 0;
    for (int k = 0; k < 4; k++)
    {
        m_products[k].assign(m_window, std::complex<float>(0.0f, 0.0f));
        m_sums[k] = 0.0;
    }
    m_sinceResum = 0;
    m_strongest = 0;
}

float RadioPagingToneDetector::process(const std::complex<float>& sample)
{
    // Tones at -3, -1, +1, +3 thirds of the deviation (4 levels) or -3 and +3 (2 levels), as multiples of the
    // phase step; their levels -1, -1/3, 1/3, 1
    static const int multiple4[4] = {-3, -1, 1, 3};
    static const int multiple2[2] = {-3, 3};
    static const float level4[4] = {-1.0f, -1.0f / 3.0f, 1.0f / 3.0f, 1.0f};
    static const float level2[2] = {-1.0f, 1.0f};
    const int *multiple = (m_levels == 4) ? multiple4 : multiple2;
    const float *level = (m_levels == 4) ? level4 : level2;

    std::complex<float> rot1((float) std::cos(m_phase), (float) -std::sin(m_phase));   // e^-j phase
    std::complex<float> rot3 = rot1 * rot1 * rot1;

    m_phase += m_phaseStep;
    if (m_phase > M_PI) {
        m_phase -= 2.0 * M_PI;
    }

    double energy = 0.0;
    double weighted = 0.0;
    double largest = 0.0;

    for (int k = 0; k < m_levels; k++)
    {
        // The sample brought down by the tone: multiplied by e^(-j m phase)
        std::complex<float> rot = (std::abs(multiple[k]) == 3) ? rot3 : rot1;
        std::complex<float> p = (multiple[k] > 0) ? sample * rot : sample * std::conj(rot);

        m_sums[k] += std::complex<double>(p) - std::complex<double>(m_products[k][m_pos]);
        m_products[k][m_pos] = p;

        double e = std::norm(m_sums[k]);
        if ((k == 0) || (e > largest))
        {
            m_strongest = k;
            largest = e;
        }
        energy += e;
        weighted += e * level[k];
    }

    m_pos = (m_pos + 1) % m_window;

    // The running sums are made again from the window now and then, so that rounding does not build up
    if (++m_sinceResum >= 64 * m_window)
    {
        for (int k = 0; k < m_levels; k++)
        {
            m_sums[k] = 0.0;
            for (int i = 0; i < m_window; i++) {
                m_sums[k] += std::complex<double>(m_products[k][i]);
            }
        }
        m_sinceResum = 0;
    }

    double others = (energy - largest) / (m_levels - 1);
    m_quality = (largest > 0.0) ? (float) (largest / std::max(others, largest * 1e-6)) : 0.0f;

    return (energy > 0.0) ? (float) (weighted / energy) : 0.0f;
}

namespace {

// In place radix-2 FFT, size a power of 2
void fft(std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();

    for (size_t i = 1, j = 0; i < n; i++)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(x[i], x[j]);
        }
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        double angle = -2.0 * M_PI / (double) len;
        std::complex<double> wlen(std::cos(angle), std::sin(angle));

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; k++)
            {
                std::complex<double> u = x[i + k];
                std::complex<double> v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

RadioPagingToneOffset::RadioPagingToneOffset() :
    m_sampleRate(1.0),
    m_minDeviation(0.0),
    m_maxDeviation(0.0),
    m_deviation(0.0),
    m_alpha(1.0),
    m_range(0.0),
    m_fill(0),
    m_offset(0.0)
{
}

void RadioPagingToneOffset::create(double sampleRate, double minDeviation, double maxDeviation, double seconds, double range, int levels)
{
    m_sampleRate = sampleRate;
    m_minDeviation = minDeviation;
    m_maxDeviation = maxDeviation;
    m_levels = levels;
    m_alpha = std::min(1.0, (m_blockSize / sampleRate) / std::max(1e-3, seconds));
    m_range = range;
    reset();
}

void RadioPagingToneOffset::reset()
{
    m_block.assign(m_blockSize, std::complex<float>(0.0f, 0.0f));
    m_fill = 0;
    m_power.assign(m_blockSize, 0.0);
    m_offset = 0.0;
    m_deviation = 0.5 * (m_minDeviation + m_maxDeviation);
}

void RadioPagingToneOffset::process(const std::complex<float>& sample)
{
    m_block[m_fill++] = sample;

    if (m_fill == m_blockSize)
    {
        m_fill = 0;
        update();
    }
}

void RadioPagingToneOffset::update()
{
    std::vector<std::complex<double>> x(m_blockSize);

    // Hann window
    for (int i = 0; i < m_blockSize; i++)
    {
        double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (m_blockSize - 1));
        x[i] = std::complex<double>(m_block[i]) * w;
    }

    fft(x);

    for (int i = 0; i < m_blockSize; i++) {
        m_power[i] += m_alpha * (std::norm(x[i]) - m_power[i]);
    }

    // The power of each bin with its neighbours (the tones are spread by the modulation)
    const double binHz = m_sampleRate / m_blockSize;
    const int spread = 2;
    std::vector<double> s(m_blockSize, 0.0);
    for (int i = 0; i < m_blockSize; i++)
    {
        for (int k = -spread; k <= spread; k++) {
            s[i] += m_power[(i + k + m_blockSize) % m_blockSize];
        }
    }
    auto power = [&](int bin) {
        return s[((bin % m_blockSize) + m_blockSize) % m_blockSize];
    };

    // The pair of tones with the most power
    const int rangeBins = (int) std::lround(m_range / binHz);
    const int devMin = std::max(1, (int) std::lround(m_minDeviation / binHz));
    const int devMax = std::max(devMin, (int) std::lround(m_maxDeviation / binHz));
    double best = -1.0;
    int bestBin = 0;
    int bestDev = devMin;

    for (int d = devMin; d <= devMax; d++)
    {
        for (int b = -rangeBins; b <= rangeBins; b++)
        {
            // The power of the tones. For 4 levels the inner tones too, at a third of the deviation: idle fill
            // puts most of the power on some of the four tones, so all four are summed
            double score = power(b + d) + power(b - d);
            if (m_levels == 4)
            {
                int inner = (int) std::lround(d / 3.0);
                score += power(b + inner) + power(b - inner);
            }
            if (score > best)
            {
                best = score;
                bestBin = b;
                bestDev = d;
            }
        }
    }

    // 4 levels: a 2-level signal of deviation D also fits offset -/+D/2 and deviation 1.5D, one tone of D as an
    // outer tone and the other as an inner one. The pair of tones of such a fit is a true outer pair of the fit
    // (b +/- d/3, 2d/3): when that pair has clearly more power than the outer pair of the fit, it is taken
    if (m_levels == 4)
    {
        int third = (int) std::lround(bestDev / 3.0);
        int partnerDev = bestDev - third;
        double outer = power(bestBin + bestDev) + power(bestBin - bestDev);

        for (int sign : {-1, 1})
        {
            int partnerBin = bestBin + sign * third;
            double partnerOuter = power(partnerBin + partnerDev) + power(partnerBin - partnerDev);

            if ((partnerDev >= devMin) && (std::abs(partnerBin) <= rangeBins) && (partnerOuter > m_aliasRatio * outer))
            {
                bestBin = partnerBin;
                bestDev = partnerDev;
                break;
            }
        }

        // The inner tones of the score take some of the power between the outer ones of a 2-level signal: the
        // deviation is the one of the most power on the outer pair near it (the outer tones are those of all modes)
        int refined = bestDev;
        double refinedPower = -1.0;
        for (int d = std::max(devMin, (int) (0.9 * bestDev)); d <= std::min(devMax, (int) std::ceil(1.1 * bestDev)); d++)
        {
            double p = power(bestBin + d) + power(bestBin - d);
            if (p > refinedPower)
            {
                refinedPower = p;
                refined = d;
            }
        }
        bestDev = refined;
    }

    m_offset = bestBin * binHz;
    m_deviation = bestDev * binHz;
}
