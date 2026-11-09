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

#ifndef MODEMRADIOPAGING_RADIOPAGINGFSKMOD_H
#define MODEMRADIOPAGING_RADIOPAGINGFSKMOD_H

#include <deque>
#include <vector>

#include "dsp/dsptypes.h"
#include "dsp/gaussian.h"
#include "dsp/interpolator.h"
#include "export.h"

// 2-level and 4-level FSK modulator of the paging protocols.
//
// Symbols are levels from -1 to +1 (+1 = outer symbol, positive deviation), queued in blocks
// of one baud rate, so a FLEX frame can change rate between its sync and data parts. The
// chain runs at an internal rate of 38400 Hz, where every paging rate has a whole number of
// samples per symbol (512: 75, 600: 64, 1200: 32, 1600: 24, 2400: 16, 3200: 12):
//   NRZ level      held for the samples of its symbol
//   shaping        Gaussian low pass on the NRZ level, unity DC gain: symbol runs reach the
//                  deviation exactly and the deviation is never exceeded. Its 3 dB cutoff is
//                  in Hz, so one filter serves all rates of a protocol (FLEX: 3.2 kHz, the
//                  STD-43A 2.1.9 baseband filter). It also sets the occupied bandwidth
//   resampling     Interpolator from 38400 Hz to the channel sample rate, on the shaped level
//   FM             phase accumulator at the channel sample rate, scaled to the deviation of
//                  the outer symbol: the frequency is exact at any resampling ratio
// The output is unit amplitude complex baseband at the channel sample rate.
class MODEMRADIOPAGING_API RadioPagingFskMod
{
public:
    static const int m_internalRate = 38400;

    RadioPagingFskMod();

    //!< Deviation of the outer symbol, Hz
    void setDeviation(double deviation);
    //!< 3 dB cutoff of the Gaussian shaping filter, Hz; 38400 / cutoff is taken to a whole
    //!< number of samples
    void setShaping(double cutoff);
    void setChannelSampleRate(int sampleRate);
    int getChannelSampleRate() const { return m_channelSampleRate; }

    //!< Drop the queued symbols and clear the filters
    void reset();
    //!< Queue symbols at a baud rate. False (nothing queued) if the rate does not divide 38400
    bool push(int baud, const std::vector<float>& levels);
    //!< Symbols queued and not started yet
    size_t queued() const;
    //!< Symbols are queued, or the last symbol is still in the shaping filter
    bool busy() const { return !m_blocks.empty() || (m_tail > 0); }

    //!< Next sample at the channel sample rate
    Complex next();
    //!< Shaped level of the last internal sample, -1 to +1 (for display)
    Real level() const { return m_level; }

private:
    struct Block
    {
        int m_samplesPerSymbol;
        std::vector<float> m_levels;
    };

    double m_deviation;
    double m_shaping;
    int m_channelSampleRate;

    std::deque<Block> m_blocks;
    size_t m_symbol;                    //!< Symbol of the first block being sent
    int m_sampleInSymbol;
    float m_hold;                       //!< Level held after the last symbol
    int m_tail;                         //!< Samples of the shaping filter delay still to output

    Gaussian<Real> m_gaussian;
    int m_gaussianDelay;                //!< Group delay of the shaping filter, samples
    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;

    double m_phase;
    double m_phaseSensitivity;          //!< At the channel sample rate
    Real m_level;
    Complex m_levelSample;              //!< Shaped level at 38400 Hz, real part

    void applyShaping();
    void applyChannelSampleRate();
    void shapeSample();
};

#endif // MODEMRADIOPAGING_RADIOPAGINGFSKMOD_H
