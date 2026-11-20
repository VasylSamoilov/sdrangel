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

#include <algorithm>

#include "nmtphoneinfo.h"

namespace modemnmt
{

const size_t NmtPhoneInfo::kMaxDialled;

void NmtPhoneInfo::notePassword(uint16_t k)
{
    const int value = k & 0xfff;

    if (value == m_candidate) {
        m_password = value;
    }
    m_candidate = value;
}

std::string NmtPhoneInfo::password() const
{
    if (m_password < 0) {
        return std::string();
    }
    static const char hex[] = "0123456789ABCDEF";
    std::string digits;

    for (int shift = 8; shift >= 0; shift -= 4) {
        digits += hex[(m_password >> shift) & 0xf];
    }
    return digits;
}

void NmtPhoneInfo::note(std::vector<Seen>& list, const std::string& number, int y1, int area, size_t max)
{
    if (number.empty()) {
        return;
    }
    // the same number on the same kind of network moves to the front with its count
    const auto it = std::find_if(list.begin(), list.end(), [&](const Seen& s) {
        return s.number == number && s.y1 == y1 && s.area == area;
    });
    Seen seen;

    if (it != list.end())
    {
        seen = *it;
        list.erase(it);
    }
    else
    {
        seen.number = number;
        seen.y1 = y1;
        seen.area = area;
    }
    seen.count++;
    list.insert(list.begin(), seen);
    if (list.size() > max) {
        list.resize(max);
    }
}

void NmtPhoneInfo::noteDialled(const std::string& number, int y1, int area)
{
    note(m_dialled, number, y1, area, kMaxDialled);
}

void NmtPhoneInfo::noteSmsCentre(const std::string& number, int y1, int area)
{
    note(m_smsCentres, number, y1, area, kMaxDialled);
}

std::string NmtPhoneInfo::text(const std::vector<Seen>& seen, bool withArea)
{
    std::string out;

    for (const Seen& s : seen)
    {
        if (!out.empty()) {
            out += ", ";
        }
        out += s.number;
        if (withArea) {
            out += " (Y1 " + std::to_string(s.y1) + ", area " + std::to_string(s.area) + ")";
        }
    }
    return out;
}

} // namespace modemnmt
