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

#ifndef MODEMCELLULAR_TXAUDIOSOURCE_H_
#define MODEMCELLULAR_TXAUDIOSOURCE_H_

#include <functional>
#include <utility>
#include <vector>

#include "dsp/interpolator.h"
#include "export.h"

namespace modemcellular
{

// Audio from the call towards the radio: 8 kHz to the channel rate, pulled in blocks of 160. The
// resampler is SDRangel's polyphase Interpolator as its DSD demodulator uses it from 8 kHz (3.4
// kHz; 96 phases, 24 taps per phase): 3 kHz -0.35 dB, the images about 60 dB down.
class MODEMCELLULAR_API TxAudioSource
{
public:
    using Pull = std::function<void(float* samples, int count)>;

    static constexpr int kBlock = 160;

    explicit TxAudioSource(double sampleRate);
    // the Interpolator holds its taps by pointer and must not be copied
    TxAudioSource(const TxAudioSource&) = delete;
    TxAudioSource& operator=(const TxAudioSource&) = delete;

    void setPull(Pull pull) { m_pull = std::move(pull); }
    // Writes length samples (replace, or add), scaled by gain
    void generate(float* samples, int length, bool add, double gain);
    void reset();

private:
    double nextCallSample();

    Real m_step;                        // call samples per output sample
    Interpolator m_interpolator;
    Real m_remain;                      // position of the output after the last call sample taken
    Pull m_pull;
    std::vector<float> m_block;
    size_t m_blockPos;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_TXAUDIOSOURCE_H_
