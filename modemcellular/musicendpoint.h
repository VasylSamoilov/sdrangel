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

#include "audioendpoint.h"
#include "export.h"

namespace modemcellular
{

// Plays a built-in looping melody (sine notes with short ramps) to the mobile and
// ignores the audio received from it. Selecting a sound file is not supported yet.
class MODEMCELLULAR_API MusicEndpoint : public AudioEndpoint
{
public:
    MusicEndpoint();

    void fromMobile(const float* samples, int count) override;
    void toMobile(float* samples, int count) override;

private:
    int m_note;             // index into the melody
    int m_notePos;          // samples played of the current note
    double m_phase;         // radians
};

} // namespace modemcellular

#endif // MODEMCELLULAR_MUSICENDPOINT_H_
