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

#ifndef MODEMNMT_NMTPHONEINFO_H_
#define MODEMNMT_NMTPHONEINFO_H_

#include <cstdint>
#include <string>
#include <vector>

#include "export.h"

namespace modemnmt
{

// What a phone shows of itself to a tester, gathered over its frames and calls: its password, the
// numbers it dialled and the numbers on which it spoke to an SMS centre, each with the traffic area
// (Y1, Y2) of the network it was on, since a phone may keep other numbers for other home networks.
// Plain data, not thread safe.
class MODEMNMT_API NmtPhoneInfo
{
public:
    struct Seen
    {
        std::string number;
        int y1 = 0;
        int area = 0;
        int count = 0;              // times seen
    };
    static const size_t kMaxDialled = 5;

    // The password K of a seizure frame (three 4-bit digits). A single frame may be a false decode:
    // it is taken when the same value comes in two frames in a row.
    void notePassword(uint16_t k);
    // A number the phone dialled, on a network with this traffic area. The newest first, at most
    // kMaxDialled different numbers.
    void noteDialled(const std::string& number, int y1, int area);
    // The phone sent a short message or asked for its waiting messages on a call to this number:
    // it is its SMS centre on such a network.
    void noteSmsCentre(const std::string& number, int y1, int area);

    bool hasPassword() const { return m_password >= 0; }
    // "943", empty if not known
    std::string password() const;
    const std::vector<Seen>& dialled() const { return m_dialled; }
    const std::vector<Seen>& smsCentres() const { return m_smsCentres; }
    // "000501 (Y1 8, area 3)", several comma separated; empty if none
    static std::string text(const std::vector<Seen>& seen, bool withArea);
    // a number into a list: the same number on the same kind of network moves to the front with
    // its count, the list keeps at most `max`
    static void note(std::vector<Seen>& list, const std::string& number, int y1, int area, size_t max);

private:

    int m_password = -1;
    int m_candidate = -1;           // a value seen once
    std::vector<Seen> m_dialled;
    std::vector<Seen> m_smsCentres;
};

} // namespace modemnmt

#endif // MODEMNMT_NMTPHONEINFO_H_
