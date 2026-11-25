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

std::vector<uint64_t> alertWithInfo(uint8_t pscc, const std::string& callerId)
{
    std::vector<uint64_t> w;
    const std::string id = callerId.substr(0, 63);          // CPN_RL is 6 bits
    const int length = static_cast<int>(id.size());

    w.push_back(order(pscc, kOrderAlertWithInfo, 0, 0));
    w.push_back(encodeWord(words::kFvcAlertInfo, {{"T1T2", kT1T2First}, {"RL_W", (length + 2) / 3}, {"SIGNAL", 1},
        {"CPN_RL", length}, {"PI", id.empty() ? 2 : 0}, {"SI", 3}}));
    characterWords(id, w);
    return w;
}

std::vector<uint64_t> flashWithInfo(uint8_t pscc, const std::string& text)
{
    std::vector<uint64_t> w;
    const std::string t = text.substr(0, kFlashInfoMax);
    const int length = static_cast<int>(t.size());

    w.push_back(order(pscc, kOrderFlashWithInfo, 0, 0));
    w.push_back(encodeWord(words::kFvcFlashInfo, {{"T1T2", kT1T2First}, {"RL_W", (length + 2) / 3},
        {"CPN_RL", length}, {"PI", t.empty() ? 2 : 0}, {"SI", 3}}));
    characterWords(t, w);
    return w;
}

void characterWords(const std::string& text, std::vector<uint64_t>& out)
{
    const int length = static_cast<int>(text.size());

    for (int i = 0; i < length; i += 3)
    {
        WordValues v = {{"T1T2", kT1T2First}};
        for (int k = 0; k < 3 && i + k < length; k++) {
            v["CHARACTER " + std::to_string(k + 1)] = static_cast<uint8_t>(text[static_cast<size_t>(i + k)]) & 0x7f;
        }
        out.push_back(encodeWord(words::kFvcInfoCharacters, v));
    }
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
