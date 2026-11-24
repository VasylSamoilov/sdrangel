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

#include "ampssysinfo.h"

#include <string>

#include "ampsmessages.h"

namespace modemamps
{

namespace
{

// the values of a global action word of the system: T1T2 11, DCC, ACT, OHD 100
WordValues globalAction(const SystemInfo& si, Act act)
{
    return {{"T1T2", kT1T2Overhead}, {"DCC", si.dcc}, {"ACT", act}, {"OHD", kOhdGlobalAction}};
}

} // namespace

std::vector<uint64_t> SystemInfo::overheadTrain(uint32_t regid) const
{
    // the messages and their values without END (osmocom's prepare_sysinfo)
    std::vector<std::pair<const WordDef*, WordValues>> train;

    train.push_back({&words::kSystemParameter1, {{"T1T2", kT1T2Overhead}, {"DCC", dcc}, {"SID1", sid >> 1}, {"EP", ep},
        {"AUTH", auth}, {"PCI", pci}, {"OHD", kOhdSystemParameter1}}});
    train.push_back({&words::kSystemParameter2, {{"T1T2", kT1T2Overhead}, {"DCC", dcc}, {"S", s}, {"E", e},
        {"REGH", regh}, {"REGR", regr}, {"DTX", dtx}, {"N-1", n1}, {"RCF", rcf()}, {"CPA", cpa}, {"CMAX-1", cmax1},
        {"OHD", kOhdSystemParameter2}}});
    train.push_back({&words::kRegistrationId, {{"T1T2", kT1T2Overhead}, {"DCC", dcc}, {"REGID", regid & 0xfffff},
        {"OHD", kOhdRegistrationId}}});

    if (regincr != 450)
    {
        WordValues v = globalAction(*this, kActRegistrationIncrement);
        v["REGINCR"] = regincr;
        train.push_back({&words::kRegistrationIncrement, v});
    }
    if (pureg != 0 || pdreg != 0 || lreg != 0 || locaid != 0)
    {
        WordValues v = globalAction(*this, kActLocationArea);
        v["PUREG"] = pureg;
        v["PDREG"] = pdreg;
        v["LREG"] = lreg;
        v["LOCAID"] = locaid;
        train.push_back({&words::kLocationArea, v});
    }
    if (newacc != 0)
    {
        WordValues v = globalAction(*this, kActNewAccessChannelSet);
        v["NEWACC"] = newacc;
        train.push_back({&words::kNewAccessChannelSet, v});
    }
    if (olc != 0xffff)
    {
        WordValues v = globalAction(*this, kActOverloadControl);
        for (int i = 0; i < 16; i++) {
            v["OLC " + std::to_string(i)] = (olc >> i) & 1;
        }
        train.push_back({&words::kOverloadControl, v});
    }
    if (bis != 1 || pciHome != 0 || pciRoam != 0 || bspc != 0 || bscap != 0)
    {
        WordValues v = globalAction(*this, kActAccessTypeParameters);
        v["BIS"] = bis;
        v["PCI_HOME"] = pciHome;
        v["PCI_ROAM"] = pciRoam;
        v["BSPC"] = bspc;
        v["BSCAP"] = bscap;
        train.push_back({&words::kAccessTypeParameters, v});
    }
    if (maxbusyPgr != 10 || maxsztrPgr != 10 || maxbusyOther != 10 || maxsztrOther != 10)
    {
        WordValues v = globalAction(*this, kActAccessAttemptParameters);
        v["MAXBUSY-PGR"] = maxbusyPgr;
        v["MAXSZTR-PGR"] = maxsztrPgr;
        v["MAXBUSY-OTHER"] = maxbusyOther;
        v["MAXSZTR-OTHER"] = maxsztrOther;
        train.push_back({&words::kAccessAttemptParameters, v});
    }

    // word 1 counts the words after it, the last one ends the train (osmocom's get_sysinfo)
    train[0].second["NAWC"] = train.size() - 1;
    train.back().second["END"] = 1;

    std::vector<uint64_t> result;
    for (const auto& t : train) {
        result.push_back(encodeWord(*t.first, t.second));
    }
    return result;
}

uint64_t SystemInfo::controlFiller() const
{
    return encodeWord(words::kControlFiller, {{"T1T2", kT1T2Overhead}, {"DCC", dcc}, {"010111", 0x17}, {"CMAC", cmac},
        {"SDCC1", sdcc1}, {"11", 3}, {"SDCC2", sdcc2}, {"1", 1}, {"WFOM", wfom()}, {"1111", 0xf},
        {"OHD", kOhdControlFiller}});
}

} // namespace modemamps
