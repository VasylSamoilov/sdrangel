///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
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

#include "ampsfvc.h"

#include "ampsmessages.h"
#include "ampsstream.h"

namespace modemamps
{

namespace fvc
{

uint64_t order(uint8_t pscc, uint8_t order, uint8_t ordq, uint8_t msgType)
{
    return encodeWord(words::kFvcOrder, {{"T1T2", kT1T2Next}, {"SCC", 3}, {"PSCC", pscc}, {"EF", 0},
        {"LOCAL/MSG_TYPE", msgType}, {"ORDQ", ordq}, {"ORDER", order}});
}

uint64_t handoff(uint8_t scc, uint8_t pscc, uint8_t dtx, uint8_t vmac, uint16_t chan)
{
    return encodeWord(words::kFvcHandoff, {{"T1T2", kT1T2Next}, {"SCC", scc}, {"PSCC", pscc}, {"EF", 0},
        {"DTX", dtx}, {"VMAC", vmac}, {"CHAN", chan}});
}

} // namespace fvc

FvcTransmitter::FvcTransmitter() :
    m_bitPos(0)
{
}

void FvcTransmitter::reset()
{
    m_queue.clear();
    m_bits.clear();
    m_bitPos = 0;
}

int FvcTransmitter::nextBit()
{
    if (m_bitPos >= m_bits.size())
    {
        uint64_t word = 0;

        if (!m_queue.empty())
        {
            word = m_queue.front();
            m_queue.pop_front();
        }
        else if (!m_source || !m_source(word))
        {
            m_bits.clear();
            m_bitPos = 0;
            return -1;
        }
        m_bits = fvcWord(word);
        m_bitPos = 0;
    }
    return m_bits[m_bitPos++];
}

} // namespace modemamps
