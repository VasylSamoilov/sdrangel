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

#ifndef MODEMRADIOPAGING_RADIOPAGINGTONEDETECTOR_H
#define MODEMRADIOPAGING_RADIOPAGINGTONEDETECTOR_H

#include <cmath>
#include <complex>
#include <vector>

#include "export.h"

// Non-coherent detection of 2-level or 4-level FSK from IQ: a correlator for each tone (the carrier plus and
// minus the deviation, and for 4 levels plus and minus a third of it), each integrating over a window of
// samples. The output, sum(E_k * level_k) / sum(E_k), is +1 for the high tone and -1 for the low one, as a
// discriminator scaled to the deviation gives them: it feeds the same symbol recovery. Unlike a discriminator
// it has no threshold: each correlator sees the noise of its window's bandwidth only, about the symbol rate,
// and not the whole channel. The strongest tone is the maximum likelihood decision of non-coherent orthogonal
// FSK: for 4 levels it decides the symbols far better than the level of the output, which the noise of the
// three other tones pulls towards 0
class MODEMRADIOPAGING_API RadioPagingToneDetector
{
public:
    RadioPagingToneDetector();

    //!< deviation: Hz of the outer tones from the carrier; window: samples integrated; levels: 2 (tones at
    //!< plus and minus the deviation) or 4 (also at plus and minus a third of it)
    void create(double sampleRate, double deviation, int window, int levels = 2);
    void reset();
    //!< A new deviation, the correlators going on
    void setDeviation(double sampleRate, double deviation) { m_phaseStep = 2.0 * M_PI * deviation / (3.0 * sampleRate); }
    //!< The frequency of the sample from the energy of each tone, sum(E_k * level_k) / sum(E_k): the levels
    //!< of the tones, +/-1 and +/-1/3 for 4 levels
    float process(const std::complex<float>& sample);
    //!< The tone with the most energy at the last sample: 0 lowest .. levels-1 highest (the maximum likelihood
    //!< decision of non-coherent orthogonal FSK)
    int strongest() const { return m_strongest; }
    //!< Energy of the strongest tone over the mean energy of the others at the last sample: about 1 for noise,
    //!< large for a clean tone. A measure of the signal that does not depend on any decoding
    float quality() const { return m_quality; }

private:
    double m_phaseStep;                 //!< A third of the outer tone, radians a sample
    double m_phase;
    int m_window;
    int m_levels;
    int m_pos;
    std::vector<std::complex<float>> m_products[4];    //!< Products with each tone over the window
    std::complex<double> m_sums[4];
    int m_sinceResum;
    int m_strongest = 0;
    float m_quality = 0.0f;
};

// Carrier offset and deviation of 2-level FSK from IQ: the power spectrum averaged over blocks, and the pair
// of tones (offset plus and minus the deviation) that has the most power, the deviation searched between
// limits. The spectrum integrates the tones over a long time in narrow bins, so the estimate holds far
// below the threshold of a discriminator
class MODEMRADIOPAGING_API RadioPagingToneOffset
{
public:
    RadioPagingToneOffset();

    //!< seconds: time constant of the spectrum average; range: largest offset searched, Hz; the deviation
    //!< is searched from minDeviation to maxDeviation, Hz
    void create(double sampleRate, double minDeviation, double maxDeviation, double seconds, double range, int levels = 2);
    void reset();
    void process(const std::complex<float>& sample);
    double offset() const { return m_offset; }
    double deviation() const { return m_deviation; }

    static const int m_blockSize = 1024;
    //!< A 4-level fit is replaced by the 2-level one it may be the alias of when that one's outer pair has this
    //!< many times the power (random 4-level data has about as much on both)
    static constexpr double m_aliasRatio = 1.5;

private:
    double m_sampleRate;
    double m_minDeviation;
    double m_maxDeviation;
    int m_levels = 2;
    double m_deviation;
    double m_alpha;
    double m_range;
    std::vector<std::complex<float>> m_block;
    int m_fill;
    std::vector<double> m_power;
    double m_offset;

    void update();
};

#endif // MODEMRADIOPAGING_RADIOPAGINGTONEDETECTOR_H
