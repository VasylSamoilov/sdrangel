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

#include "firfilter.h"

#include <algorithm>
#include <cmath>

namespace modemcellular
{

namespace
{

const double kPi = 3.14159265358979323846;

} // namespace

std::vector<float> designLowpass(double cutoff, double sampleRate, int taps)
{
    taps = std::max(1, taps | 1);
    std::vector<double> h(static_cast<size_t>(taps));
    const double fc = cutoff / sampleRate;
    const int mid = taps / 2;
    double sum = 0.0;

    for (int i = 0; i < taps; i++)
    {
        const int n = i - mid;
        const double sinc = n == 0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * n) / (kPi * n);
        const double w = taps == 1 ? 1.0
            : 0.42 - 0.5 * std::cos(2.0 * kPi * i / (taps - 1)) + 0.08 * std::cos(4.0 * kPi * i / (taps - 1));
        h[static_cast<size_t>(i)] = sinc * w;
        sum += sinc * w;
    }

    std::vector<float> out(h.size());
    for (size_t i = 0; i < h.size(); i++) {
        out[i] = static_cast<float>(h[i] / sum);
    }
    return out;
}

int lowpassTaps(double transition, double sampleRate)
{
    // Blackman: about 5.5 / (transition / rate) taps for 74 dB
    const int taps = static_cast<int>(std::ceil(5.5 * sampleRate / transition));

    return std::max(3, taps | 1);
}

FirDecimator::FirDecimator(int factor, const std::vector<float>& taps) :
    m_factor(std::max(1, factor)),
    m_taps(taps.empty() ? std::vector<float>{1.0f} : taps)
{
    // reversed, so that the history (oldest first) multiplies straight
    std::reverse(m_taps.begin(), m_taps.end());
    reset();
}

void FirDecimator::reset()
{
    m_history.assign(2 * m_taps.size(), std::complex<float>(0.0f, 0.0f));
    m_pos = 0;
    m_phase = 0;
}

void FirDecimator::process(const std::complex<float>* in, size_t count, std::vector<std::complex<float>>& out)
{
    const size_t n = m_taps.size();

    for (size_t i = 0; i < count; i++)
    {
        m_history[m_pos] = in[i];
        m_history[m_pos + n] = in[i];
        m_pos = m_pos + 1 == n ? 0 : m_pos + 1;
        if (++m_phase < m_factor) {
            continue;
        }
        m_phase = 0;

        // the oldest sample is at m_pos
        const std::complex<float>* h = &m_history[m_pos];
        float re = 0.0f;
        float im = 0.0f;
        for (size_t k = 0; k < n; k++)
        {
            re += h[k].real() * m_taps[k];
            im += h[k].imag() * m_taps[k];
        }
        out.emplace_back(re, im);
    }
}

FirInterpolator::FirInterpolator(int factor, const std::vector<float>& taps) :
    m_factor(std::max(1, factor))
{
    const std::vector<float> h = taps.empty() ? std::vector<float>{1.0f} : taps;

    m_phaseTaps = static_cast<int>((h.size() + static_cast<size_t>(m_factor) - 1) / static_cast<size_t>(m_factor));
    m_phases.assign(static_cast<size_t>(m_factor), std::vector<float>(static_cast<size_t>(m_phaseTaps), 0.0f));
    // branch p, tap k is h[k * factor + p]; reversed for the oldest-first history
    for (int p = 0; p < m_factor; p++)
    {
        for (int k = 0; k < m_phaseTaps; k++)
        {
            const size_t index = static_cast<size_t>(k * m_factor + p);
            const float value = index < h.size() ? h[index] * static_cast<float>(m_factor) : 0.0f;
            m_phases[static_cast<size_t>(p)][static_cast<size_t>(m_phaseTaps - 1 - k)] = value;
        }
    }
    reset();
}

void FirInterpolator::reset()
{
    m_history.assign(2 * static_cast<size_t>(m_phaseTaps), std::complex<float>(0.0f, 0.0f));
    m_pos = 0;
}

void FirInterpolator::process(const std::complex<float>* in, size_t count, std::vector<std::complex<float>>& out)
{
    const size_t n = static_cast<size_t>(m_phaseTaps);

    for (size_t i = 0; i < count; i++)
    {
        m_history[m_pos] = in[i];
        m_history[m_pos + n] = in[i];
        m_pos = m_pos + 1 == n ? 0 : m_pos + 1;

        const std::complex<float>* x = &m_history[m_pos];
        for (const std::vector<float>& branch : m_phases)
        {
            float re = 0.0f;
            float im = 0.0f;
            for (size_t k = 0; k < n; k++)
            {
                re += x[k].real() * branch[k];
                im += x[k].imag() * branch[k];
            }
            out.emplace_back(re, im);
        }
    }
}

} // namespace modemcellular
