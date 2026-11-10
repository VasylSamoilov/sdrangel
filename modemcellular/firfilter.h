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

#ifndef MODEMCELLULAR_FIRFILTER_H_
#define MODEMCELLULAR_FIRFILTER_H_

#include <complex>
#include <cstddef>
#include <vector>

#include "export.h"

namespace modemcellular
{

// Low pass taps, windowed sinc (Blackman), the sum of the taps is 1. cutoff and sampleRate in Hz.
MODEMCELLULAR_API std::vector<float> designLowpass(double cutoff, double sampleRate, int taps);

// The number of taps a Blackman window needs for the given transition band, odd.
MODEMCELLULAR_API int lowpassTaps(double transition, double sampleRate);

// Complex FIR low pass that keeps every factor-th output (only those are computed).
class MODEMCELLULAR_API FirDecimator
{
public:
    FirDecimator(int factor, const std::vector<float>& taps);

    // Appends the output samples of count input samples to out.
    void process(const std::complex<float>* in, size_t count, std::vector<std::complex<float>>& out);
    int factor() const { return m_factor; }
    void reset();

private:
    int m_factor;
    std::vector<float> m_taps;
    std::vector<std::complex<float>> m_history;     // twice the taps, written twice (no wrapping)
    size_t m_pos;
    int m_phase;
};

// Complex FIR interpolator: factor outputs per input, polyphase, the gain is 1.
class MODEMCELLULAR_API FirInterpolator
{
public:
    FirInterpolator(int factor, const std::vector<float>& taps);

    // Appends factor output samples per input sample to out.
    void process(const std::complex<float>* in, size_t count, std::vector<std::complex<float>>& out);
    int factor() const { return m_factor; }
    void reset();

private:
    int m_factor;
    int m_phaseTaps;
    std::vector<std::vector<float>> m_phases;       // polyphase branches, times the factor
    std::vector<std::complex<float>> m_history;     // twice the branch length
    size_t m_pos;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_FIRFILTER_H_
