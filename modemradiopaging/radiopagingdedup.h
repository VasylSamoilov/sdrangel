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

#ifndef MODEMRADIOPAGING_RADIOPAGINGDEDUP_H
#define MODEMRADIOPAGING_RADIOPAGINGDEDUP_H

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// Duplicate detection for the messages of a receiver. A message is a duplicate of an earlier
// one received within the window on the same network (polarity), to the same address
// (with its function), with the same payload. Duplicates are not dropped: the receiver
// marks them and outputs them.
class RadioPagingDedup
{
public:
    static const size_t m_maxEntries = 64;

    void setWindow(double seconds) { m_window = seconds; }
    void reset() { m_entries.clear(); }
    size_t size() const { return m_entries.size(); }

    //!< True if the message is a duplicate. A message that is not is remembered
    bool check(int polarity, const std::string& address, const std::vector<uint8_t>& data, int bits, double time)
    {
        while (!m_entries.empty() && ((time - m_entries.front().m_time) > m_window)) {
            m_entries.pop_front();
        }

        for (const Entry& e : m_entries)
        {
            if ((e.m_polarity == polarity) && (e.m_bits == bits) && (e.m_address == address) && (e.m_data == data)) {
                return true;
            }
        }

        if (m_entries.size() >= m_maxEntries) {
            m_entries.pop_front();
        }

        m_entries.push_back(Entry{polarity, address, data, bits, time});
        return false;
    }

private:
    struct Entry
    {
        int m_polarity;
        std::string m_address;
        std::vector<uint8_t> m_data;
        int m_bits;
        double m_time;
    };

    double m_window = 60.0;
    std::deque<Entry> m_entries;
};

#endif // MODEMRADIOPAGING_RADIOPAGINGDEDUP_H
