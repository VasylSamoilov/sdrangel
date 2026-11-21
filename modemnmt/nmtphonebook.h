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

#ifndef MODEMNMT_NMTPHONEBOOK_H_
#define MODEMNMT_NMTPHONEBOOK_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "export.h"
#include "nmtnumbers.h"
#include "nmtphoneinfo.h"

namespace modemnmt
{

// A value of a phone's record: learned from the phone or typed by the user. Typed wins: learning
// does not change a typed value. Clearing a typed value gives it back to learning.
struct NmtBookField
{
    std::string value;
    bool typed = false;
};

// What the tester knows of one phone, kept between sessions.
struct MODEMNMT_API NmtPhoneRecord
{
    enum Field { Name, Notes, Password, SmsCentre, VoiceMail, VoiceMailCode, FieldCount };

    System system = System::Nmt450;
    std::string number;                             // country digit and six digits
    NmtBookField fields[FieldCount];
    std::vector<NmtPhoneInfo::Seen> dialled;        // the newest first
    std::vector<NmtPhoneInfo::Seen> smsCentresUsed; // numbers it really spoke to an SMS centre on
    std::vector<std::pair<int, int>> areas;         // Y1, Y2 of the networks it was heard on, the newest first

    const std::string& value(Field f) const { return fields[f].value; }
    static const char *fieldName(Field f);          // "name", "notes", "password" ...
};

// The records of all phones, by system and number. Observations (what the worker learned) merge
// into them; the user's edits are typed values. Not thread safe.
class MODEMNMT_API NmtPhoneBook
{
public:
    static const size_t kMaxList = 5;

    // the record, made if there is none
    NmtPhoneRecord& record(System system, const std::string& number);
    const NmtPhoneRecord *find(System system, const std::string& number) const;
    bool forget(System system, const std::string& number);
    std::vector<const NmtPhoneRecord*> all() const;
    size_t size() const { return m_records.size(); }

    // Observations: true if the record changed.
    bool learnPassword(System system, const std::string& number, const std::string& password);
    bool learnDialled(System system, const std::string& number, const std::string& dialled, int y1, int area);
    // the phone sent a message to / asked for its messages on this number; it becomes its SMS
    // centre unless one is typed
    bool learnSmsCentre(System system, const std::string& number, const std::string& centre, int y1, int area);
    bool learnArea(System system, const std::string& number, int y1, int area);

    // The user's value; an empty value gives the field back to learning (its learned value is gone
    // until learned again). True if the record changed.
    bool edit(System system, const std::string& number, NmtPhoneRecord::Field field, const std::string& value);

    // A record from elsewhere (settings, Web API): its typed values win, its learned values only
    // fill what is not known, its lists merge (nothing known is lost).
    void merge(const NmtPhoneRecord& other);

private:
    using Key = std::pair<int, std::string>;
    static Key key(System system, const std::string& number) { return Key(static_cast<int>(system), number); }
    static void mergeList(std::vector<NmtPhoneInfo::Seen>& into, const std::vector<NmtPhoneInfo::Seen>& from);

    std::map<Key, NmtPhoneRecord> m_records;
};

} // namespace modemnmt

#endif // MODEMNMT_NMTPHONEBOOK_H_
