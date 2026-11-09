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

#ifndef MODEMCELLULAR_MOBILELEG_H_
#define MODEMCELLULAR_MOBILELEG_H_

#include <string>

#include "calltypes.h"

namespace modemcellular
{

// What the call router tells a mobile. Implemented by the protocol layer of a channel
// (one object per radio channel or per mobile). All calls come from the router; the
// protocol layer answers through the CallRouter functions with the same call reference.
class MobileLeg
{
public:
    virtual ~MobileLeg() = default;

    // Call for this mobile: page it, ring it. Reply with CallRouter::alerting() and
    // CallRouter::answer(), or CallRouter::release() if it cannot be reached.
    virtual void setup(CallRef callref, const std::string& callerId, NumberType callerType,
                       const std::string& dialed) = 0;
    // The call this mobile placed was accepted: a voice channel can be assigned now.
    virtual void proceeding(CallRef callref) = 0;
    // The far end is ringing.
    virtual void alerting(CallRef callref) = 0;
    // The far end answered: connect the audio.
    virtual void answer(CallRef callref) = 0;
    // The call ended on the far end or inside the router. The call reference is gone;
    // calling CallRouter::release() for it is allowed and ignored.
    virtual void release(CallRef callref, Cause cause) = 0;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_MOBILELEG_H_
