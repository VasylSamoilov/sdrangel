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

#ifndef MODEMCELLULAR_ECHOENDPOINT_H_
#define MODEMCELLULAR_ECHOENDPOINT_H_

#include <deque>

#include "audioendpoint.h"
#include "export.h"

namespace modemcellular
{

// Plays the received audio back to the mobile after a fixed delay.
class MODEMCELLULAR_API EchoEndpoint : public AudioEndpoint
{
public:
    explicit EchoEndpoint(double delaySeconds = 1.0);

    void fromMobile(const float* samples, int count) override;
    void toMobile(float* samples, int count) override;

private:
    std::deque<float> m_queue;
    size_t m_maxQueued;     // oldest audio is dropped beyond this, so the delay cannot grow
};

} // namespace modemcellular

#endif // MODEMCELLULAR_ECHOENDPOINT_H_
