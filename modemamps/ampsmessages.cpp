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

#include "ampsmessages.h"

namespace modemamps
{
namespace words
{

// FOCC mobile station control message (3.7.1.1, TIA/EIA-553-A p.89-90)
const WordDef kAbbreviatedAddress = {"Word 1 - Abbreviated Address Word", false,
    {{"T1T2", 2}, {"DCC", 2}, {"MIN1", 24}}};
const WordDef kExtendedAddressOrder = {"Word 2 - Extended Address Word (SCC = 11)", false,
    {{"T1T2", 2}, {"SCC", 2}, {"MIN2", 10}, {"EF", 1}, {"LOCAL/MSG_TYPE", 5}, {"ORDQ", 3}, {"ORDER", 5}}};
const WordDef kExtendedAddressChannel = {"Word 2 - Extended Address Word (SCC != 11)", false,
    {{"T1T2", 2}, {"SCC", 2}, {"MIN2", 10}, {"VMAC", 3}, {"CHAN", 11}}};

// FOCC overhead messages (3.7.1.2, p.95-101)
const WordDef kSystemParameter1 = {"System Parameter Overhead Message Word 1", false,
    {{"T1T2", 2}, {"DCC", 2}, {"SID1", 14}, {"EP", 1}, {"AUTH", 1}, {"PCI", 1}, {"NAWC", 4}, {"OHD", 3}}};
const WordDef kSystemParameter2 = {"System Parameter Overhead Message Word 2", false,
    {{"T1T2", 2}, {"DCC", 2}, {"S", 1}, {"E", 1}, {"REGH", 1}, {"REGR", 1}, {"DTX", 2}, {"N-1", 5}, {"RCF", 1},
     {"CPA", 1}, {"CMAX-1", 7}, {"END", 1}, {"OHD", 3}}};
const WordDef kRescan = {"Rescan Global Action Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"ACT", 4}, {"RSVD", 16}, {"END", 1}, {"OHD", 3}}};
const WordDef kRegistrationIncrement = {"Registration Increment Global Action Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"ACT", 4}, {"REGINCR", 12}, {"RSVD", 4}, {"END", 1}, {"OHD", 3}}};
const WordDef kLocationArea = {"Location Area Global Action Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"ACT", 4}, {"PUREG", 1}, {"PDREG", 1}, {"LREG", 1}, {"RSVD", 1}, {"LOCAID", 12},
     {"END", 1}, {"OHD", 3}}};
const WordDef kNewAccessChannelSet = {"New Access Channel Set Global Action Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"ACT", 4}, {"NEWACC", 11}, {"RSVD", 5}, {"END", 1}, {"OHD", 3}}};
const WordDef kOverloadControl = {"Overload Control Global Action Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"ACT", 4}, {"OLC 0", 1}, {"OLC 1", 1}, {"OLC 2", 1}, {"OLC 3", 1}, {"OLC 4", 1},
     {"OLC 5", 1}, {"OLC 6", 1}, {"OLC 7", 1}, {"OLC 8", 1}, {"OLC 9", 1}, {"OLC 10", 1}, {"OLC 11", 1},
     {"OLC 12", 1}, {"OLC 13", 1}, {"OLC 14", 1}, {"OLC 15", 1}, {"END", 1}, {"OHD", 3}}};
const WordDef kAccessTypeParameters = {"Access Type Parameters Global Action Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"ACT", 4}, {"BIS", 1}, {"PCI_HOME", 1}, {"PCI_ROAM", 1}, {"BSPC", 4}, {"BSCAP", 3},
     {"RSVD", 6}, {"END", 1}, {"OHD", 3}}};
const WordDef kAccessAttemptParameters = {"Access Attempt Parameters Global Action Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"ACT", 4}, {"MAXBUSY-PGR", 4}, {"MAXSZTR-PGR", 4}, {"MAXBUSY-OTHER", 4},
     {"MAXSZTR-OTHER", 4}, {"END", 1}, {"OHD", 3}}};
const WordDef kRegistrationId = {"Registration ID Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"REGID", 20}, {"END", 1}, {"OHD", 3}}};
const WordDef kControlFiller = {"Control-Filler Message", false,
    {{"T1T2", 2}, {"DCC", 2}, {"010111", 6}, {"CMAC", 3}, {"SDCC1", 2}, {"11", 2}, {"SDCC2", 2}, {"1", 1},
     {"WFOM", 1}, {"1111", 4}, {"OHD", 3}}};

// RECC (2.7.1.1, p.63-65)
const WordDef kReccA = {"Word A - Abbreviated Address Word", true,
    {{"F", 1}, {"NAWC", 3}, {"T", 1}, {"S", 1}, {"E", 1}, {"ER", 1}, {"SCM", 4}, {"MIN1", 24}}};
const WordDef kReccB = {"Word B - Extended Address Word", true,
    {{"F", 1}, {"NAWC", 3}, {"LOCAL/MSG_TYPE", 5}, {"ORDQ", 3}, {"ORDER", 5}, {"LT", 1}, {"EP", 1}, {"SCM(4)", 1},
     {"MPCI", 2}, {"SDCC1", 2}, {"SDCC2", 2}, {"MIN2", 10}}};
const WordDef kReccSerial = {"Word C - Serial Number Word", true,
    {{"F", 1}, {"NAWC", 3}, {"SERIAL", 32}}};
const WordDef kReccPci = {"Word C - Protocol Capability Indicator Word", true,
    {{"F", 1}, {"NAWC", 3}, {"MSPC", 4}, {"MSCAP", 3}, {"RSVD", 25}}};
const WordDef kReccDigits = {"Word D to G - Called Address Word", true,
    {{"F", 1}, {"NAWC", 3}, {"DIGIT 1", 4}, {"DIGIT 2", 4}, {"DIGIT 3", 4}, {"DIGIT 4", 4}, {"DIGIT 5", 4},
     {"DIGIT 6", 4}, {"DIGIT 7", 4}, {"DIGIT 8", 4}}};

// FVC mobile station control message (3.7.2.1, p.105)
const WordDef kFvcOrder = {"Mobile Station Control Message (SCC = 11)", false,
    {{"T1T2", 2}, {"SCC", 2}, {"PSCC", 2}, {"EF", 1}, {"RSVD", 8}, {"LOCAL/MSG_TYPE", 5}, {"ORDQ", 3}, {"ORDER", 5}}};
const WordDef kFvcHandoff = {"Mobile Station Control Message (SCC != 11)", false,
    {{"T1T2", 2}, {"SCC", 2}, {"PSCC", 2}, {"EF", 1}, {"RSVD", 4}, {"DTX", 1}, {"PVI", 1}, {"MEM", 1}, {"VMAC", 3},
     {"CHAN", 11}}};

// RVC (2.7.2.1, p.69-71)
const WordDef kRvcOrderConfirmation = {"Order Confirmation Message", true,
    {{"F", 1}, {"NAWC", 2}, {"T", 1}, {"LOCAL/MSG_TYPE", 5}, {"ORDQ", 3}, {"ORDER", 5}, {"RSVD", 19}}};
const WordDef kRvcDigits = {"Called-Address Message Word", true,
    {{"F", 1}, {"NAWC", 2}, {"T", 1}, {"DIGIT 1", 4}, {"DIGIT 2", 4}, {"DIGIT 3", 4}, {"DIGIT 4", 4},
     {"DIGIT 5", 4}, {"DIGIT 6", 4}, {"DIGIT 7", 4}, {"DIGIT 8", 4}}};

} // namespace words

double satFrequency(int scc)
{
    static const double kSat[3] = {5970.0, 6000.0, 6030.0};
    return (scc >= 0 && scc < 3) ? kSat[scc] : 0.0;
}

int digitCode(char digit)
{
    if (digit >= '1' && digit <= '9') {
        return digit - '0';
    }
    switch (digit)
    {
    case '0': return 10;
    case '*': return 11;
    case '#': return 12;
    default: return -1;
    }
}

char codeDigit(int code)
{
    if (code >= 1 && code <= 9) {
        return static_cast<char>('0' + code);
    }
    switch (code)
    {
    case 10: return '0';
    case 11: return '*';
    case 12: return '#';
    default: return 0;
    }
}

} // namespace modemamps
