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

#ifndef MODEMAMPS_AMPSSYSINFO_H_
#define MODEMAMPS_AMPSSYSINFO_H_

#include <cstdint>
#include <vector>

#include "export.h"

// The system information a control channel broadcasts in its overhead train (TIA/EIA-553-A
// 3.7.1.2), with the defaults of osmocom-analog's amps (sysinfo.c init_sysinfo and the defaults of
// its command line, main_common.c), which work with the phones.
namespace modemamps
{

struct MODEMAMPS_API SystemInfo
{
    uint16_t sid = 0;               // the 15 bit system identification; SID1 is its upper 14 bits
    uint8_t dcc = 0;
    // the power level of the mobile on the control channel (filler CMAC) and on the voice channels
    // (VMAC of a designation): osmocom's ms_power
    uint8_t cmac = 4;
    uint8_t vmac = 4;
    // control filler
    uint8_t sdcc1 = 0;
    uint8_t sdcc2 = 0;
    // word 1
    uint8_t ep = 0;
    uint8_t auth = 0;               // no authentication
    uint8_t pci = 0;
    // word 2
    uint8_t s = 1;                  // the serial number with every access
    uint8_t e = 1;                  // the extended address
    uint8_t regh = 1;
    uint8_t regr = 1;
    uint8_t dtx = 0;
    uint8_t n1 = 20;                // all 21 dedicated control channels can be paging channels
    uint8_t cpa = 1;                // combined paging and access
    uint8_t cmax1 = 20;
    // global actions, broadcast if they differ from the default of the standard
    uint16_t regincr = 300;         // default 450
    uint8_t pureg = 1;              // location area, default 0 0 0 0
    uint8_t pdreg = 1;
    uint8_t lreg = 0;
    uint16_t locaid = 0;
    uint16_t newacc = 0;            // default 0
    uint16_t olc = 0xffff;          // overload control OLC 15 .. OLC 0, default all 1
    // busy-idle: 0, the phone does not wait for the busy-idle bit (the base cannot answer within
    // its time); then RCF and WFOM are 1 and the access type message is always sent
    uint8_t bis = 0;
    uint8_t pciHome = 1;            // access type, default 1 0 0 0 0
    uint8_t pciRoam = 1;
    uint8_t bspc = 0;
    uint8_t bscap = 1;
    uint8_t maxbusyPgr = 10;        // access attempt, default 10 10 10 10
    uint8_t maxsztrPgr = 10;
    uint8_t maxbusyOther = 10;
    uint8_t maxsztrOther = 10;

    uint8_t rcf() const { return bis ? 0 : 1; }
    uint8_t wfom() const { return bis ? 0 : 1; }

    // The words of an overhead train: word 1 (NAWC: the words after it), word 2, the registration
    // ID, the global actions that differ from their default; END in the last one. regid is
    // osmocom's: the time in seconds, its 20 lower bits.
    std::vector<uint64_t> overheadTrain(uint32_t regid) const;
    // the control filler
    uint64_t controlFiller() const;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSSYSINFO_H_
