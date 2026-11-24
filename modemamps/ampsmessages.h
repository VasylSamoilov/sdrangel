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

#ifndef MODEMAMPS_AMPSMESSAGES_H_
#define MODEMAMPS_AMPSMESSAGES_H_

#include <cstdint>

#include "ampsword.h"
#include "export.h"

// The words of the AMPS messages an analog system needs for a call (TIA/EIA-553-A 2.7, 3.7): the
// layouts of osmocom-analog's frame.c (it works with the phones), the names of the standard. A field
// of fixed bits is named as the standard prints it ("010111").
namespace modemamps
{
namespace words
{
// FOCC (3.7.1): the mobile station control message and the overhead messages
MODEMAMPS_API extern const WordDef kAbbreviatedAddress;        // word 1: T1T2 DCC MIN1
MODEMAMPS_API extern const WordDef kExtendedAddressOrder;      // word 2, SCC = 11: an order or a page
MODEMAMPS_API extern const WordDef kExtendedAddressChannel;    // word 2, SCC != 11: initial voice channel designation
MODEMAMPS_API extern const WordDef kSystemParameter1;          // OHD 110
MODEMAMPS_API extern const WordDef kSystemParameter2;          // OHD 111
MODEMAMPS_API extern const WordDef kRescan;                    // global action, ACT 0001
MODEMAMPS_API extern const WordDef kRegistrationIncrement;     // global action, ACT 0010
MODEMAMPS_API extern const WordDef kLocationArea;              // global action, ACT 0011
MODEMAMPS_API extern const WordDef kNewAccessChannelSet;       // global action, ACT 0110
MODEMAMPS_API extern const WordDef kOverloadControl;           // global action, ACT 1000: OLC 0 .. OLC 15
MODEMAMPS_API extern const WordDef kAccessTypeParameters;      // global action, ACT 1001
MODEMAMPS_API extern const WordDef kAccessAttemptParameters;   // global action, ACT 1010
MODEMAMPS_API extern const WordDef kRegistrationId;            // OHD 000
MODEMAMPS_API extern const WordDef kControlFiller;             // OHD 001

// RECC (2.7.1)
MODEMAMPS_API extern const WordDef kReccA;                     // abbreviated address word
MODEMAMPS_API extern const WordDef kReccB;                     // extended address word
MODEMAMPS_API extern const WordDef kReccSerial;                // word C, serial number
MODEMAMPS_API extern const WordDef kReccDigits;                // words D to G: F NAWC and 8 digits (DIGIT 1 .. DIGIT 8)

// FVC (3.7.2)
MODEMAMPS_API extern const WordDef kFvcOrder;                  // mobile station control message, SCC = 11
MODEMAMPS_API extern const WordDef kFvcHandoff;                // SCC != 11

// RVC (2.7.2)
MODEMAMPS_API extern const WordDef kRvcOrderConfirmation;
MODEMAMPS_API extern const WordDef kRvcDigits;                 // called-address words: F NAWC T and 8 digits
} // namespace words

// T1T2 of a forward word (3.7.1.1)
enum T1T2 : uint8_t { kT1T2Single = 0, kT1T2First = 1, kT1T2Next = 2, kT1T2Overhead = 3 };
// OHD of an overhead word (Table 3.7.1-3)
enum Ohd : uint8_t { kOhdRegistrationId = 0, kOhdControlFiller = 1, kOhdGlobalAction = 4, kOhdSystemParameter1 = 6,
    kOhdSystemParameter2 = 7 };
// ACT of a global action word (Table 3.7.1-4)
enum Act : uint8_t { kActRescan = 1, kActRegistrationIncrement = 2, kActLocationArea = 3, kActNewAccessChannelSet = 6,
    kActOverloadControl = 8, kActAccessTypeParameters = 9, kActAccessAttemptParameters = 10 };

// ORDER (Table 3.7.1-1); with ORDQ and LOCAL/MSG_TYPE 0 unless noted
enum Order : uint8_t
{
    kOrderPage = 0x00,              // page (or origination)
    kOrderAlert = 0x01,
    kOrderRelease = 0x03,
    kOrderReorder = 0x04,
    kOrderMessageWaiting = 0x05,
    kOrderStopAlert = 0x06,
    kOrderAudit = 0x07,
    kOrderSendCalledAddress = 0x08,
    kOrderIntercept = 0x09,
    kOrderMaintenance = 0x0a,
    kOrderChangePower = 0x0b,       // ORDQ: the power level 0..7
    kOrderDirectedRetry = 0x0c,
    kOrderRegistration = 0x0d,      // ORDQ: 0 non-autonomous, 1 make whereabouts known, 2/3 autonomous
    kOrderParameterUpdate = 0x0f,   // ORDQ 0; 1 serial number request/response
    kOrderAlertWithInfo = 0x11,
    kOrderFlashWithInfo = 0x12,
    kOrderRegistrationAuth = 0x18,
    kOrderLocalControl = 0x1e,
};

// The SAT frequency of a SAT colour code (Table 3.7.1-2), 0 for SCC 11 (no analog channel)
MODEMAMPS_API double satFrequency(int scc);
// The code of a dialled digit (Table 2.7.1-2): 1-9, 0 = 10, * = 11, # = 12; -1 for another character
MODEMAMPS_API int digitCode(char digit);
// The digit of a code, 0 for null or a reserved code
MODEMAMPS_API char codeDigit(int code);

} // namespace modemamps

#endif // MODEMAMPS_AMPSMESSAGES_H_
