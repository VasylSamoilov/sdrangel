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

#ifndef MODEMCELLULAR_CARRIERBATCH_H_
#define MODEMCELLULAR_CARRIERBATCH_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace modemcellular
{

// One block of the audio of several carriers of one radio stream, the same time span for all of
// them: what a receiver plugin hands to the network, and what the network hands back for the
// transmitter. The samples are the FM baseband at the audio rate of the network (48 kHz),
// carrier after carrier.
struct CarrierBatch
{
    uint64_t sample = 0;            // index of the first sample on the clock of the receive stream
    int length = 0;                 // samples per carrier
    std::vector<int> carriers;      // carrier ids
    std::vector<float> samples;     // carriers.size() * length

    void reset(uint64_t first, int count, const std::vector<int>& ids)
    {
        sample = first;
        length = count;
        carriers = ids;
        samples.assign(ids.size() * static_cast<size_t>(count > 0 ? count : 0), 0.0f);
    }
    float* carrier(size_t index) { return samples.data() + index * static_cast<size_t>(length); }
    const float* carrier(size_t index) const { return samples.data() + index * static_cast<size_t>(length); }
    // the index of a carrier id, -1 if it is not in the batch
    int find(int id) const
    {
        for (size_t i = 0; i < carriers.size(); i++)
        {
            if (carriers[i] == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CARRIERBATCH_H_
