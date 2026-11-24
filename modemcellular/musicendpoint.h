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

#ifndef MODEMCELLULAR_MUSICENDPOINT_H_
#define MODEMCELLULAR_MUSICENDPOINT_H_

#include <functional>
#include <memory>
#include <vector>

#include "audioendpoint.h"
#include "export.h"

namespace modemcellular
{

// What the music plays and how.
struct MusicOptions
{
    double levelDb = 0.0;       // at most 0: never louder than the source
    bool loop = true;           // false: plays once, then the call ends (finished())
    // a clip (8 kHz, 1.0 = the reference), e.g. a sound file; null: the built-in melody
    std::shared_ptr<const std::vector<float>> clip;
    // a live source (8 kHz) that fills the samples, e.g. an audio input; set: it plays and never
    // ends by itself
    std::function<void(float* samples, int count)> input;
};

// Plays music to the mobile and ignores the audio received from it: the built-in melody (sine
// notes with short ramps), a clip, or a live source.
class MODEMCELLULAR_API MusicEndpoint : public AudioEndpoint
{
public:
    explicit MusicEndpoint(MusicOptions options = MusicOptions());

    void fromMobile(const float* samples, int count) override;
    void toMobile(float* samples, int count) override;
    bool finished() const override { return m_finished; }

private:
    void melody(float* samples, int count);
    void clip(float* samples, int count);

    MusicOptions m_options;
    float m_gain;
    bool m_finished;
    int m_note;             // index into the melody
    int m_notePos;          // samples played of the current note
    double m_phase;         // radians
    size_t m_clipPos;       // samples played of the clip
};

} // namespace modemcellular

#endif // MODEMCELLULAR_MUSICENDPOINT_H_
