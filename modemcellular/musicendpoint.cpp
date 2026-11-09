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

#include <cmath>

#include "calltypes.h"
#include "musicendpoint.h"

namespace modemcellular
{

namespace
{

struct Note
{
    double frequency;   // Hz, 0 is a rest
    int milliseconds;
};

// C major arpeggio up and down, then a rest.
const Note kMelody[] = {
    {523.25, 300}, {659.25, 300}, {783.99, 300}, {1046.50, 300},
    {783.99, 300}, {659.25, 300}, {0.0, 300},
};
constexpr int kMelodyNotes = static_cast<int>(sizeof(kMelody) / sizeof(kMelody[0]));

constexpr double kLevel = 0.3;                  // below speech full scale
constexpr int kRampSamples = 80;                // 10 ms attack and release per note
constexpr double kTwoPi = 6.283185307179586;

} // namespace

MusicEndpoint::MusicEndpoint() :
    m_note(0),
    m_notePos(0),
    m_phase(0.0)
{
}

void MusicEndpoint::fromMobile(const float*, int)
{
}

void MusicEndpoint::toMobile(float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        const Note& note = kMelody[m_note];
        const int length = note.milliseconds * kAudioSampleRate / 1000;

        double value = 0.0;

        if (note.frequency > 0.0)
        {
            double envelope = 1.0;
            const int fromEnd = length - m_notePos;

            if (m_notePos < kRampSamples) {
                envelope = static_cast<double>(m_notePos) / kRampSamples;
            } else if (fromEnd < kRampSamples) {
                envelope = static_cast<double>(fromEnd) / kRampSamples;
            }

            value = kLevel * envelope * std::sin(m_phase);
            m_phase += kTwoPi * note.frequency / kAudioSampleRate;

            if (m_phase >= kTwoPi) {
                m_phase -= kTwoPi;
            }
        }

        samples[i] = static_cast<float>(value);
        m_notePos++;

        if (m_notePos >= length)
        {
            m_notePos = 0;
            m_phase = 0.0;      // the envelope is zero at the note boundary, so no click
            m_note = (m_note + 1) % kMelodyNotes;
        }
    }
}

} // namespace modemcellular
