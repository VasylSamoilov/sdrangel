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

#ifndef MODEMNMT_NMTTRACEFOLD_H_
#define MODEMNMT_NMTTRACEFOLD_H_

#include <string>

namespace modemnmt
{

// A run of the same frame in a trace is shown once and then counted: the first frame of a run is
// shown, the repeats are not; when the run ends (another frame, or the channel goes quiet) the
// count of repeats is shown. Not thread safe.
class NmtTraceFold
{
public:
    // True: the frame repeats the run, show nothing. False: a new run starts with it; show first
    // what flush() returns for the run before, then the frame.
    bool repeat(const std::string& frame)
    {
        if (!m_last.empty() && frame == m_last)
        {
            m_repeats++;
            return true;
        }
        m_pending = m_repeats;
        m_last = frame;
        m_repeats = 0;
        return false;
    }

    // The repeats of the run that ended before the frame repeat() just took, or of the run going
    // on when the channel went quiet (then the run ends here). 0: nothing to show.
    int flush()
    {
        const int n = m_pending;
        m_pending = 0;
        return n;
    }

    // The channel went quiet: the run ends; its repeats, 0 if none.
    int end()
    {
        const int n = m_repeats + m_pending;
        m_last.clear();
        m_repeats = 0;
        m_pending = 0;
        return n;
    }

    bool running() const { return m_repeats > 0; }

private:
    std::string m_last;
    int m_repeats = 0;              // of the run going on, beyond its first frame
    int m_pending = 0;              // of the run that ended, not shown yet
};

} // namespace modemnmt

#endif // MODEMNMT_NMTTRACEFOLD_H_
