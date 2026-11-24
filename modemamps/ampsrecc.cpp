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

#include "ampsrecc.h"

#include "ampsmessages.h"

namespace modemamps
{

namespace
{

// a digit of a MIN: 1..9, 0 is 10 (TIA/EIA-553-A 2.3.1)
int minDigit(char c)
{
    return (c == '0') ? 10 : c - '0';
}

char minChar(int value)
{
    return (value == 10) ? '0' : static_cast<char>('0' + value);
}

// three digits as 10 bits: 100 a + 10 b + c - 111
int threeOf(const char* d)
{
    return minDigit(d[0]) * 100 + minDigit(d[1]) * 10 + minDigit(d[2]) - 111;
}

std::string threeDigits(int value)
{
    if (value < 0 || value > 999) {
        return "???";
    }
    return {minChar(value / 100 + 1), minChar((value / 10) % 10 + 1), minChar(value % 10 + 1)};
}

const int kMaxWords = 8;                // A, B and up to 6 more (osmocom: too many words)

} // namespace

std::string ReccAssembler::min1Digits(uint32_t min1)
{
    const int thousands = static_cast<int>((min1 >> 10) & 0xf);
    const std::string middle = (thousands >= 1 && thousands <= 10) ? std::string(1, minChar(thousands)) : "?";

    return threeDigits(static_cast<int>(min1 >> 14)) + middle + threeDigits(static_cast<int>(min1 & 0x3ff));
}

std::string ReccAssembler::min2Digits(uint16_t min2)
{
    return threeDigits(min2);
}

uint32_t ReccAssembler::min1Of(const std::string& digits)
{
    if (digits.size() != 7) {
        return 0;
    }
    const char* d = digits.c_str();
    return (static_cast<uint32_t>(threeOf(d)) << 14) | (static_cast<uint32_t>(minDigit(d[3])) << 10)
        | static_cast<uint32_t>(threeOf(d + 4));
}

uint16_t ReccAssembler::min2Of(const std::string& digits)
{
    return (digits.size() == 3) ? static_cast<uint16_t>(threeOf(digits.c_str())) : 0;
}

void ReccAssembler::drop(const char* reason)
{
    if (m_active && m_drop) {
        m_drop(reason);
    }
    m_active = false;
}

void ReccAssembler::lost()
{
    drop("a word of the message was not received");
}

// The kind of the word after A and B (index 2 on): the words the system asked for, then digits.
ReccAssembler::Kind ReccAssembler::kindOf(int index) const
{
    int k = 2;

    if (m_si.s)
    {
        if (index == k) {
            return KindSerial;
        }
        k++;
    }
    // the capability word: asked for (PCI home or roam), not with an origination or a page
    // response (ORDER 0), as osmocom; no authentication word, the system has AUTH 0
    if ((m_si.pciHome || m_si.pciRoam) && m_message.order != 0)
    {
        if (index == k) {
            return KindPci;
        }
        k++;
    }
    return KindDigits;
}

bool ReccAssembler::word(uint64_t word, bool first)
{
    const int f = static_cast<int>(wordField(words::kReccA, word, "F"));
    const int nawc = static_cast<int>(wordField(words::kReccA, word, "NAWC"));

    if (first || f == 1)
    {
        if (f != 1)
        {
            // the first word after a sync is word A
            m_active = false;
            if (m_drop) {
                m_drop("the first word is not word A");
            }
            return false;
        }
        if (m_active) {
            drop("a new message came before the end of the last");
        }
        m_active = true;
        m_message = ReccMessage();
        m_index = 0;
        m_digitWords = 0;
        m_remaining = nawc;
        m_message.t = static_cast<uint8_t>(wordField(words::kReccA, word, "T"));
        m_message.scm = static_cast<uint8_t>(wordField(words::kReccA, word, "SCM"));
        m_message.min1 = static_cast<uint32_t>(wordField(words::kReccA, word, "MIN1"));
    }
    else
    {
        if (!m_active) {
            return false;
        }
        // the word count of each word has to be one less than the one before (osmocom only logs a
        // mismatch: the words would be taken for others)
        if (nawc != --m_remaining)
        {
            drop("the NAWC of a word does not follow the one before");
            return false;
        }
        if (m_index >= kMaxWords)
        {
            drop("too many words");
            return false;
        }

        if (m_index == 1)
        {
            m_message.msgType = static_cast<uint8_t>(wordField(words::kReccB, word, "LOCAL/MSG_TYPE"));
            m_message.ordq = static_cast<uint8_t>(wordField(words::kReccB, word, "ORDQ"));
            m_message.order = static_cast<uint8_t>(wordField(words::kReccB, word, "ORDER"));
            m_message.scm |= static_cast<uint8_t>(wordField(words::kReccB, word, "SCM(4)") << 4);
            m_message.mpci = static_cast<uint8_t>(wordField(words::kReccB, word, "MPCI"));
            m_message.sdcc1 = static_cast<uint8_t>(wordField(words::kReccB, word, "SDCC1"));
            m_message.sdcc2 = static_cast<uint8_t>(wordField(words::kReccB, word, "SDCC2"));
            m_message.min2 = static_cast<uint16_t>(wordField(words::kReccB, word, "MIN2"));
        }
        else
        {
            switch (kindOf(m_index))
            {
            case KindSerial:
                m_message.hasEsn = true;
                m_message.esn = static_cast<uint32_t>(wordField(words::kReccSerial, word, "SERIAL"));
                break;
            case KindPci:
                m_message.hasPci = true;
                m_message.mspc = static_cast<uint8_t>(wordField(words::kReccPci, word, "MSPC"));
                m_message.mscap = static_cast<uint8_t>(wordField(words::kReccPci, word, "MSCAP"));
                break;
            case KindDigits:
                if (m_digitWords >= 4)
                {
                    drop("more than 4 words of digits");
                    return false;
                }
                for (int i = 1; i <= 8; i++)
                {
                    const char c = codeDigit(static_cast<int>(wordField(words::kReccDigits, word, "DIGIT " + std::to_string(i))));
                    if (c != 0 && m_message.dialled.size() == static_cast<size_t>(m_digitWords * 8 + i - 1)) {
                        m_message.dialled.push_back(c);
                    }
                }
                m_digitWords++;
                break;
            }
        }
    }

    m_index++;
    m_message.words = m_index;
    if (m_remaining > 0) {
        return true;
    }

    m_active = false;
    if (m_index < 2)
    {
        if (m_drop) {
            m_drop("a message of word A only");
        }
        return false;
    }
    if (m_sink) {
        m_sink(m_message);
    }
    return false;
}

} // namespace modemamps
