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

#ifndef INCLUDE_FEATURE_CELLULARFAREND_H_
#define INCLUDE_FEATURE_CELLULARFAREND_H_

#include <memory>
#include <vector>

#include <QString>

#include "audio/audiofifo.h"
#include "dsp/interpolator.h"
#include "util/messagequeue.h"

// The sources of the music far end that come from outside: a sound file and an audio input.
namespace CellularFarEndAudio
{
    // A WAV file (16 bit PCM, any rate, mono or stereo mixed) as 8 kHz samples with its full scale
    // at the speech reference (1.0 = 3 kHz deviation: below the limit of the radio); null with a
    // reason if it cannot be read.
    std::shared_ptr<std::vector<float>> loadClip(const QString& path, QString& error);
    // 16 bit samples (mono) at a rate to 8 kHz, full scale to 1.0
    std::vector<float> to8k(const std::vector<float>& samples, int rate);
}

// An SDRangel audio input as a live music source at 8 kHz. The input device delivers into a FIFO;
// fill() takes what is there, resampled; missing samples are silence.
class CellularMusicInput
{
public:
    // device: the input device by name, empty for the default
    explicit CellularMusicInput(const QString& device);
    ~CellularMusicInput();
    CellularMusicInput(const CellularMusicInput&) = delete;
    CellularMusicInput& operator=(const CellularMusicInput&) = delete;

    void fill(float* samples, int count);
    int rate() const { return m_rate; }

private:
    AudioFifo m_fifo;
    MessageQueue m_queue;           // notifications of the device, not used
    int m_rate;
    Interpolator m_resampler;
    Real m_distance;                // input samples per 8 kHz sample
    Real m_remain;
    std::vector<float> m_ready;     // 8 kHz samples not taken yet
    std::vector<AudioSample> m_block;
};

#endif // INCLUDE_FEATURE_CELLULARFAREND_H_
