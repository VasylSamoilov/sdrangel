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

#ifndef MODEMCELLULAR_AUDIOENDPOINT_H_
#define MODEMCELLULAR_AUDIOENDPOINT_H_

namespace modemcellular
{

// Far end of a call that is not another mobile: a music source, an echo test, later
// a SIP bridge. Audio is mono float at 8 kHz (see calltypes.h).
class AudioEndpoint
{
public:
    virtual ~AudioEndpoint() = default;

    // Audio received from the mobile.
    virtual void fromMobile(const float* samples, int count) = 0;
    // Audio for the mobile. Always writes count samples, silence if nothing is available.
    virtual void toMobile(float* samples, int count) = 0;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_AUDIOENDPOINT_H_
