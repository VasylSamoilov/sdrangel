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

#ifndef MODEMAMPS_AMPSTRACE_H_
#define MODEMAMPS_AMPSTRACE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ampsfocc.h"
#include "ampsrecc.h"
#include "export.h"

// One line of text for each message on the air, for the trace of the log (as modemnmt's
// describeFrame): the orders by the names of osmocom-analog's amps (frame.c amps_order_names), the
// phone by its 10 digits.
namespace modemamps
{

// The order of ORDER, ORDQ and MSG_TYPE (Table 3.7.1-1): "Alert", "Change Power to Power Level 4",
// "Autonomous Registration - Power Down"; an unknown one by its numbers.
MODEMAMPS_API std::string ampsOrderName(uint8_t order, uint8_t ordq, uint8_t msgType);

// FOCC: "Page to 2162177526", "Initial Voice Channel Designation to 2162177526: CHAN 356, SCC 0,
// VMAC 4"
MODEMAMPS_API std::string describeFocc(const FoccMessage& message);

// RECC: "Origination from 2162177526 to 555, ESN 0x..., SCM 0x...", "Page Response from ...",
// "Autonomous Registration - Make whereabouts known from ..."
MODEMAMPS_API std::string describeRecc(const ReccMessage& message);

// RVC: "Order Confirmation: Release"; a called-address word by its digits
MODEMAMPS_API std::string describeRvc(uint64_t word);

// FVC: the words one by one as the system gives them; a message of more than one word (alert with
// info) is one line once its last word is in: "Alert With Info: 1000001 (PI presentation allowed, SI
// network provided)", "Alert", "Handoff: CHAN 359, SCC 1, VMAC 4".
class MODEMAMPS_API FvcTrace
{
public:
    // the lines that are complete with this word (a message cut short by the next is shown as it is)
    std::vector<std::string> add(uint64_t word);
    void reset() { m_words.clear(); }

private:
    std::vector<uint64_t> m_words;
    bool complete() const;
    std::string describe(bool cut) const;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSTRACE_H_
