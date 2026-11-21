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

#include "nmtphonebook.h"

namespace modemnmt
{

const size_t NmtPhoneBook::kMaxList;

const char *NmtPhoneRecord::fieldName(Field f)
{
    static const char *names[FieldCount] = {"name", "notes", "password", "smsCentre", "voiceMail", "voiceMailCode"};
    return (f >= 0 && f < FieldCount) ? names[f] : "";
}

NmtPhoneRecord& NmtPhoneBook::record(System system, const std::string& number)
{
    NmtPhoneRecord& r = m_records[key(system, number)];

    r.system = system;
    r.number = number;
    return r;
}

const NmtPhoneRecord *NmtPhoneBook::find(System system, const std::string& number) const
{
    const auto it = m_records.find(key(system, number));
    return it != m_records.end() ? &it->second : nullptr;
}

bool NmtPhoneBook::forget(System system, const std::string& number)
{
    return m_records.erase(key(system, number)) > 0;
}

std::vector<const NmtPhoneRecord*> NmtPhoneBook::all() const
{
    std::vector<const NmtPhoneRecord*> out(m_records.size());

    std::transform(m_records.begin(), m_records.end(), out.begin(), [](const auto& r) { return &r.second; });
    return out;
}

// a learned value: only into a field the user did not type
static bool learn(NmtBookField& field, const std::string& value)
{
    if (field.typed || value.empty() || field.value == value) {
        return false;
    }
    field.value = value;
    return true;
}

bool NmtPhoneBook::learnPassword(System system, const std::string& number, const std::string& password)
{
    return learn(record(system, number).fields[NmtPhoneRecord::Password], password);
}

bool NmtPhoneBook::learnDialled(System system, const std::string& number, const std::string& dialled, int y1, int area)
{
    if (dialled.empty()) {
        return false;
    }
    NmtPhoneInfo::note(record(system, number).dialled, dialled, y1, area, kMaxList);
    return true;
}

bool NmtPhoneBook::learnSmsCentre(System system, const std::string& number, const std::string& centre, int y1, int area)
{
    if (centre.empty()) {
        return false;
    }
    NmtPhoneRecord& r = record(system, number);
    NmtPhoneInfo::note(r.smsCentresUsed, centre, y1, area, kMaxList);
    learn(r.fields[NmtPhoneRecord::SmsCentre], centre);
    return true;
}

bool NmtPhoneBook::learnArea(System system, const std::string& number, int y1, int area)
{
    NmtPhoneRecord& r = record(system, number);
    const std::pair<int, int> a(y1, area);

    if (!r.areas.empty() && r.areas.front() == a) {
        return false;
    }
    r.areas.erase(std::remove(r.areas.begin(), r.areas.end(), a), r.areas.end());
    r.areas.insert(r.areas.begin(), a);
    if (r.areas.size() > kMaxList) {
        r.areas.resize(kMaxList);
    }
    return true;
}

bool NmtPhoneBook::edit(System system, const std::string& number, NmtPhoneRecord::Field field, const std::string& value)
{
    if (field < 0 || field >= NmtPhoneRecord::FieldCount) {
        return false;
    }
    NmtBookField& f = record(system, number).fields[field];
    const NmtBookField before = f;

    f.value = value;
    f.typed = !value.empty();
    return f.value != before.value || f.typed != before.typed;
}

void NmtPhoneBook::mergeList(std::vector<NmtPhoneInfo::Seen>& into, const std::vector<NmtPhoneInfo::Seen>& from)
{
    for (const NmtPhoneInfo::Seen& s : from)
    {
        const auto it = std::find_if(into.begin(), into.end(), [&s](const NmtPhoneInfo::Seen& t) {
            return t.number == s.number && t.y1 == s.y1 && t.area == s.area;
        });
        if (it != into.end()) {
            it->count = std::max(it->count, s.count);
        } else if (into.size() < kMaxList) {
            into.push_back(s);
        }
    }
}

void NmtPhoneBook::merge(const NmtPhoneRecord& other)
{
    NmtPhoneRecord& r = record(other.system, other.number);

    for (int f = 0; f < NmtPhoneRecord::FieldCount; f++)
    {
        const NmtBookField& theirs = other.fields[f];
        NmtBookField& mine = r.fields[f];

        if (theirs.typed) {
            mine = theirs;
        } else if (!mine.typed && mine.value.empty()) {
            mine.value = theirs.value;
        }
    }
    mergeList(r.dialled, other.dialled);
    mergeList(r.smsCentresUsed, other.smsCentresUsed);
    for (const std::pair<int, int>& a : other.areas)
    {
        if (std::find(r.areas.begin(), r.areas.end(), a) == r.areas.end() && r.areas.size() < kMaxList) {
            r.areas.push_back(a);
        }
    }
}

} // namespace modemnmt
