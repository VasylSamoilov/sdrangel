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

#ifndef MODEMCELLULAR_DTMFGENERATOR_H_
#define MODEMCELLULAR_DTMFGENERATOR_H_

#include <cstdint>
#include <deque>

#include "export.h"

namespace modemcellular
{

// Push-button signalling (ITU-T Q.23, the telephone network's MFT): digits 0-9, * # A B C D, each a
// pair of tones, 100 ms of tone and 50 ms of pause, mono float at 8 kHz, 0.2 peak for each of
// the two tones. Digits wait in a queue (64 at most). Not thread safe.
class MODEMCELLULAR_API DtmfGenerator
{
public:
    static const int kMaxQueued = 64;

    DtmfGenerator();

    // False if it is no digit or the queue is full.
    bool queue(char digit);
    // A digit is being sent or waits.
    bool active() const { return m_remaining > 0 || !m_queue.empty(); }
    // Adds the tones to count samples (silence adds nothing).
    void mix(float* samples, int count);
    void reset();

    // the two frequencies of a digit, 0 if it is no digit
    static bool frequencies(char digit, double& low, double& high);

private:
    std::deque<char> m_queue;
    int m_remaining;            // samples left of the digit that is going out, tone and pause
    double m_low;
    double m_high;
    double m_phaseLow;
    double m_phaseHigh;
    int m_pos;                  // position in the digit
};

} // namespace modemcellular

#endif // MODEMCELLULAR_DTMFGENERATOR_H_
