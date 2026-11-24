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

#ifndef MODEMAMPS_AMPSRECC_H_
#define MODEMAMPS_AMPSRECC_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "ampssysinfo.h"
#include "export.h"

// The messages of the phones on the reverse control channel, from their words, as osmocom-analog's
// amps (frame.c amps_decode_word_recc): word A, word B, then the words the system asked for in its
// overhead (the serial number word C when S is set; the protocol capability word C when PCI is asked
// for and the access is not an origination or page response, ORDER 0), then the called address,
// 8 digits per word (D to G). The message is done when the NAWC of a word is 0.
namespace modemamps
{

struct MODEMAMPS_API ReccMessage
{
    uint8_t t = 0;              // 1 origination or order (registration), 0 order response or page response
    uint8_t scm = 0;            // station class mark: SCM of word A, SCM(4) of word B as bit 4
    uint32_t min1 = 0;
    uint16_t min2 = 0;
    uint8_t msgType = 0;
    uint8_t ordq = 0;
    uint8_t order = 0;
    uint8_t mpci = 0;
    uint8_t sdcc1 = 0;
    uint8_t sdcc2 = 0;
    bool hasEsn = false;
    uint32_t esn = 0;
    bool hasPci = false;
    uint8_t mspc = 0;
    uint8_t mscap = 0;
    std::string dialled;        // the called address of an origination, empty otherwise
    int words = 0;
};

class MODEMAMPS_API ReccAssembler
{
public:
    using MessageSink = std::function<void(const ReccMessage&)>;
    // a message given up: why (for the log)
    using DropSink = std::function<void(const char* reason)>;

    void setSystemInfo(const SystemInfo& si) { m_si = si; }
    void setMessageSink(MessageSink sink) { m_sink = std::move(sink); }
    void setDropSink(DropSink sink) { m_drop = std::move(sink); }

    // A word of a burst (the vote, its parity right); first: after the sync. Returns true if
    // another word follows (the receiver reads it without a sync).
    bool word(uint64_t word, bool first);
    // the burst had no word with a right parity: a message being received is given up
    void lost();

    // the 7 digit number of a MIN1 and the 3 digits of a MIN2 (TIA/EIA-553-A 2.3.1)
    static std::string min1Digits(uint32_t min1);
    static std::string min2Digits(uint16_t min2);
    static uint32_t min1Of(const std::string& digits);  // 7 digits
    static uint16_t min2Of(const std::string& digits);  // 3 digits

private:
    enum Kind { KindSerial, KindPci, KindDigits };

    void drop(const char* reason);
    Kind kindOf(int index) const;

    SystemInfo m_si;
    MessageSink m_sink;
    DropSink m_drop;
    bool m_active = false;
    int m_remaining = 0;        // words still to come after the last one
    int m_index = 0;            // words received
    int m_digitWords = 0;
    ReccMessage m_message;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSRECC_H_
