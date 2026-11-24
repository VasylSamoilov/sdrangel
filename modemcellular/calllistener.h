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

#ifndef MODEMCELLULAR_CALLLISTENER_H_
#define MODEMCELLULAR_CALLLISTENER_H_

#include <cstdint>
#include <deque>
#include <vector>

#include "dsp/interpolator.h"
#include "export.h"

namespace modemcellular
{

// Listening in to a call: the speech from the phone and to the phone (8 kHz, 1.0 = the reference)
// to a speaker (stereo 16 bit at its rate). Both directions: the phone on the left, the far end on
// the right; one direction: mono on both sides. It only listens. The two directions come in step
// (the same blocks of the call); one that stops (a call on hold, the far end silent) is taken as
// silence once the other is 200 ms ahead. Not thread safe.
class MODEMCELLULAR_API CallListener
{
public:
    enum class Mode
    {
        Off,
        Both,           // phone left, far end right
        FromPhone,      // the phone, mono
        ToPhone         // the far end, mono
    };

    static constexpr double kCallRate = 8000.0;
    // the reference level on the speaker: -12 dB of full scale (headroom for the peaks of speech)
    static constexpr double kReferenceLevel = 0.25;

    explicit CallListener(int outputRate);
    CallListener(const CallListener&) = delete;
    CallListener& operator=(const CallListener&) = delete;

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    // the volume in dB, at most 0
    void setVolumeDb(double db);

    void fromPhone(const float* samples, int count);
    void toPhone(const float* samples, int count);
    // the stereo frames (left, right interleaved) that both directions allow, appended to out;
    // returns the frames added
    size_t take(std::vector<int16_t>& out);
    void reset();

private:
    int m_outputRate;
    Mode m_mode;
    double m_gain;
    Interpolator m_interpolator;
    Real m_step;            // call samples per output frame
    Real m_remain;
    std::deque<float> m_from;
    std::deque<float> m_to;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CALLLISTENER_H_
