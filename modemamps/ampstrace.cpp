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

#include "ampstrace.h"

#include <cstdio>

#include "ampsmessages.h"
#include "ampsword.h"

namespace modemamps
{

namespace
{

std::string number(uint32_t min1, uint16_t min2)
{
    return ReccAssembler::min2Digits(min2) + ReccAssembler::min1Digits(min1);
}

std::string format(const char *fmt, unsigned a, unsigned b = 0, unsigned c = 0)
{
    char text[96];
    std::snprintf(text, sizeof(text), fmt, a, b, c);
    return text;
}

const char *presentationName(unsigned pi)
{
    switch (pi)
    {
    case 0: return "presentation allowed";
    case 1: return "presentation restricted";
    case 2: return "number not available";
    default: return "reserved";
    }
}

const char *screeningName(unsigned si)
{
    switch (si)
    {
    case 0: return "user provided, not screened";
    case 1: return "user provided, verified and passed";
    case 2: return "user provided, verified and failed";
    default: return "network provided";
    }
}

} // namespace

std::string ampsOrderName(uint8_t order, uint8_t ordq, uint8_t msgType)
{
    // osmocom frame.c amps_order_names, the analog ones
    switch (order)
    {
    case kOrderPage:
        if (ordq == 0 && msgType == 0) {
            return "Page";
        }
        break;
    case kOrderAlert:
        if (ordq == 0) {
            return "Alert";
        }
        if (ordq == 1) {
            return "Abbreviated Alert";
        }
        break;
    case kOrderRelease:
        if (ordq == 0) {
            return "Release";
        }
        if (ordq == 3) {
            return "Release Complete";
        }
        break;
    case kOrderReorder:
        return "Reorder";
    case kOrderMessageWaiting:
        if (ordq <= 2)
        {
            static const char *kinds[] = {"Voice", "SMS", "G3-Fax"};
            return std::string(kinds[ordq]) + " Message Waiting" + format(" (%u)", msgType);
        }
        break;
    case kOrderStopAlert:
        return "Stop Alert";
    case kOrderAudit:
        return "Audit";
    case kOrderSendCalledAddress:
        return "Send Called-address";
    case kOrderIntercept:
        return "Intercept";
    case kOrderMaintenance:
        return "Maintenance";
    case kOrderChangePower:
        return format("Change Power to Power Level %u", ordq);
    case kOrderDirectedRetry:
        return ordq == 1 ? "Directed Retry - last try" : "Directed Retry - not last try";
    case kOrderRegistration:
        if (msgType == 0 && ordq <= 3)
        {
            static const char *names[] = {
                "Non-autonomous Registration - Do not make whereabouts known",
                "Non-autonomous Registration - Make whereabouts known",
                "Autonomous Registration - Do not make whereabouts known",
                "Autonomous Registration - Make whereabouts known"};
            return names[ordq];
        }
        if (msgType == 1 && ordq == 3) {
            return "Autonomous Registration - Power Down";
        }
        break;
    case kOrderParameterUpdate:
        if (ordq == 0) {
            return "Parameter Update";
        }
        if (ordq == 1) {
            return "Serial Number Request / Response";
        }
        break;
    case kOrderAlertWithInfo:
        return "Alert With Info";
    case kOrderFlashWithInfo:
        return "Flash With Info";
    case kOrderLocalControl:
        return format("Local Control %u", msgType);
    default:
        break;
    }
    return format("ORDER %u ORDQ %u MSG_TYPE %u", order, ordq, msgType);
}

std::string describeFocc(const FoccMessage& m)
{
    const uint32_t min1 = static_cast<uint32_t>(wordField(words::kAbbreviatedAddress, m.word1, "MIN1"));
    const uint16_t min2 = static_cast<uint16_t>(wordField(words::kExtendedAddressOrder, m.word2, "MIN2"));
    const std::string to = " to " + number(min1, min2);

    if (wordField(words::kExtendedAddressOrder, m.word2, "SCC") == 3)
    {
        return ampsOrderName(static_cast<uint8_t>(wordField(words::kExtendedAddressOrder, m.word2, "ORDER")),
            static_cast<uint8_t>(wordField(words::kExtendedAddressOrder, m.word2, "ORDQ")),
            static_cast<uint8_t>(wordField(words::kExtendedAddressOrder, m.word2, "LOCAL/MSG_TYPE"))) + to;
    }
    return "Initial Voice Channel Designation" + to + format(": CHAN %u, SCC %u, VMAC %u",
        static_cast<unsigned>(wordField(words::kExtendedAddressChannel, m.word2, "CHAN")),
        static_cast<unsigned>(wordField(words::kExtendedAddressChannel, m.word2, "SCC")),
        static_cast<unsigned>(wordField(words::kExtendedAddressChannel, m.word2, "VMAC")));
}

std::string describeRecc(const ReccMessage& m)
{
    const std::string from = " from " + number(m.min1, m.min2);
    std::string text;

    if (m.order == kOrderPage && m.ordq == 0 && m.msgType == 0) {
        text = m.t == 1 ? "Origination" + from + " to " + m.dialled : "Page Response" + from;
    } else if (m.t == 0) {
        text = "Order Confirmation: " + ampsOrderName(m.order, m.ordq, m.msgType) + from;     // T 0: the response to an order
    } else {
        text = ampsOrderName(m.order, m.ordq, m.msgType) + from;
    }
    if (m.hasEsn) {
        text += format(", ESN 0x%08x", m.esn);
    }
    text += format(", SCM 0x%02x", m.scm);
    if (m.hasPci) {
        text += format(", MSPC %u, MSCAP %u", m.mspc, m.mscap);
    }
    return text;
}

std::string describeRvc(uint64_t word)
{
    if (wordField(words::kRvcOrderConfirmation, word, "T") == 1)
    {
        return "Order Confirmation: " + ampsOrderName(static_cast<uint8_t>(wordField(words::kRvcOrderConfirmation, word, "ORDER")),
            static_cast<uint8_t>(wordField(words::kRvcOrderConfirmation, word, "ORDQ")),
            static_cast<uint8_t>(wordField(words::kRvcOrderConfirmation, word, "LOCAL/MSG_TYPE")));
    }
    std::string digits;
    for (int i = 1; i <= 8; i++)
    {
        const char d = codeDigit(static_cast<int>(wordField(words::kRvcDigits, word, "DIGIT " + std::to_string(i))));
        if (d != 0) {
            digits += d;
        }
    }
    return "Called Address: " + digits;
}

bool FvcTrace::complete() const
{
    if (m_words.empty()) {
        return false;
    }
    const uint64_t w1 = m_words[0];
    if (wordField(words::kFvcOrder, w1, "SCC") != 3) {
        return true;                            // handoff: one word
    }
    const uint64_t order = wordField(words::kFvcOrder, w1, "ORDER");
    if (order != kOrderAlertWithInfo && order != kOrderFlashWithInfo) {
        return true;
    }
    if (m_words.size() < 2) {
        return false;
    }
    return m_words.size() >= 2 + wordField(order == kOrderFlashWithInfo ? words::kFvcFlashInfo : words::kFvcAlertInfo,
        m_words[1], "RL_W");
}

std::string FvcTrace::describe(bool cut) const
{
    const uint64_t w1 = m_words[0];
    std::string text;

    if (wordField(words::kFvcOrder, w1, "SCC") != 3)
    {
        text = format("Handoff: CHAN %u, SCC %u, VMAC %u", static_cast<unsigned>(wordField(words::kFvcHandoff, w1, "CHAN")),
            static_cast<unsigned>(wordField(words::kFvcHandoff, w1, "SCC")),
            static_cast<unsigned>(wordField(words::kFvcHandoff, w1, "VMAC")));
    }
    else
    {
        text = ampsOrderName(static_cast<uint8_t>(wordField(words::kFvcOrder, w1, "ORDER")),
            static_cast<uint8_t>(wordField(words::kFvcOrder, w1, "ORDQ")),
            static_cast<uint8_t>(wordField(words::kFvcOrder, w1, "LOCAL/MSG_TYPE")));
        if (m_words.size() >= 2)
        {
            const uint64_t info = m_words[1];
            // the flash's word 2 has no SIGNAL: its fields sit elsewhere
            const bool flash = wordField(words::kFvcOrder, w1, "ORDER") == kOrderFlashWithInfo;
            const WordDef& def = flash ? words::kFvcFlashInfo : words::kFvcAlertInfo;
            const size_t length = static_cast<size_t>(wordField(def, info, "CPN_RL"));
            std::string characters;
            for (size_t i = 2; i < m_words.size(); i++)
            {
                for (int k = 1; k <= 3; k++)
                {
                    const char c = static_cast<char>(wordField(words::kFvcInfoCharacters, m_words[i], "CHARACTER " + std::to_string(k)));
                    if (c != 0 && characters.size() < length) {
                        characters += c;
                    }
                }
            }
            text += ": " + (characters.empty() ? std::string("no number") : characters)
                + " (PI " + presentationName(static_cast<unsigned>(wordField(def, info, "PI")))
                + ", SI " + screeningName(static_cast<unsigned>(wordField(def, info, "SI")))
                + (flash ? std::string(")") : format(", SIGNAL %u)", static_cast<unsigned>(wordField(def, info, "SIGNAL"))));
        }
    }
    if (cut) {
        text += format(" (cut short after %u words)", static_cast<unsigned>(m_words.size()));
    }
    return text;
}

std::vector<std::string> FvcTrace::add(uint64_t word)
{
    std::vector<std::string> lines;

    // a first word (T1T2 10) while a message is not complete: that one was cut short
    if (wordField(words::kFvcOrder, word, "T1T2") == kT1T2Next && !m_words.empty())
    {
        lines.push_back(describe(true));
        m_words.clear();
    }
    if (m_words.empty() && wordField(words::kFvcOrder, word, "T1T2") != kT1T2Next) {
        return lines;                           // a next word without its first: nothing to show
    }
    m_words.push_back(word);
    if (complete())
    {
        lines.push_back(describe(false));
        m_words.clear();
    }
    return lines;
}

} // namespace modemamps
