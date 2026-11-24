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

// The character sets of NMT SMS, NMT Doc 450-3/900-3 Annex 24 v.8.0 (1998), as printed there: TP-DCS 0x00 the
// Default GSM Alphabet (7 bits, 2.2.3.10, 4.2), TP-DCS 0x30 + n NMT Character Set n (8 bits, one code a character,
// 4.3): set 0 Cyrillic (4.3.1), 1 Baltic (4.3.2), 2 Central (4.3.3), 3 Western (4.3.4), 4 Thai (4.3.5). 0x00-0x7F of
// every 8-bit set is the GSM default alphabet. The escape 0x1B to the GSM extension table (a character of two codes)
// is no character here: the SMS coding handles it. Codes the standard does not print have no glyph; rows printed
// "<... is also included in GSM Set>" (set 2 0x82, set 3 0x83 0xA0 0xA4 0xAD) hold that letter, encoding takes its GSM
// code (the first). CR and LF are shown as their symbols (U+240D, U+240A), so a message is one line; the control
// characters are taken for them when encoding. A Nokia NMT-450 (Ukrainian firmware) read on air shows set 0 code for
// code.

#include "radiopagingcharsettables.h"

namespace {

#define NMT_GSM_DEFAULT \
    /* 0x00 */ {"@"}, /* 0x01 */ {"\xC2\xA3" /* U+00A3 */}, /* 0x02 */ {"$"}, /* 0x03 */ {"\xC2\xA5" /* U+00A5 */}, \
    /* 0x04 */ {"\xC3\xA8" /* U+00E8 */}, /* 0x05 */ {"\xC3\xA9" /* U+00E9 */}, /* 0x06 */ {"\xC3\xB9" /* U+00F9 */}, /* 0x07 */ {"\xC3\xAC" /* U+00EC */}, \
    /* 0x08 */ {"\xC3\xB2" /* U+00F2 */}, /* 0x09 */ {"\xC3\x87" /* U+00C7 */}, /* 0x0A */ {"\xE2\x90\x8A" /* U+240A symbol for line feed */, "\n"}, /* 0x0B */ {"\xC3\x98" /* U+00D8 */}, \
    /* 0x0C */ {"\xC3\xB8" /* U+00F8 */}, /* 0x0D */ {"\xE2\x90\x8D" /* U+240D symbol for carriage return */, "\r"}, /* 0x0E */ {"\xC3\x85" /* U+00C5 */}, /* 0x0F */ {"\xC3\xA5" /* U+00E5 */}, \
    /* 0x10 */ {"\xCE\x94" /* U+0394 */}, /* 0x11 */ {"_"}, /* 0x12 */ {"\xCE\xA6" /* U+03A6 */}, /* 0x13 */ {"\xCE\x93" /* U+0393 */}, \
    /* 0x14 */ {"\xCE\x9B" /* U+039B */}, /* 0x15 */ {"\xCE\xA9" /* U+03A9 */}, /* 0x16 */ {"\xCE\xA0" /* U+03A0 */}, /* 0x17 */ {"\xCE\xA8" /* U+03A8 */}, \
    /* 0x18 */ {"\xCE\xA3" /* U+03A3 */}, /* 0x19 */ {"\xCE\x98" /* U+0398 */}, /* 0x1A */ {"\xCE\x9E" /* U+039E */}, /* 0x1B */ {}, \
    /* 0x1C */ {"\xC3\x86" /* U+00C6 */}, /* 0x1D */ {"\xC3\xA6" /* U+00E6 */}, /* 0x1E */ {"\xC3\x9F" /* U+00DF */}, /* 0x1F */ {"\xC3\x89" /* U+00C9 */}, \
    /* 0x20 */ {" "}, /* 0x21 */ {"!"}, /* 0x22 */ {"\""}, /* 0x23 */ {"#"}, \
    /* 0x24 */ {"\xC2\xA4" /* U+00A4 */}, /* 0x25 */ {"%"}, /* 0x26 */ {"&"}, /* 0x27 */ {"'"}, \
    /* 0x28 */ {"("}, /* 0x29 */ {")"}, /* 0x2A */ {"*"}, /* 0x2B */ {"+"}, \
    /* 0x2C */ {","}, /* 0x2D */ {"-"}, /* 0x2E */ {"."}, /* 0x2F */ {"/"}, \
    /* 0x30 */ {"0"}, /* 0x31 */ {"1"}, /* 0x32 */ {"2"}, /* 0x33 */ {"3"}, \
    /* 0x34 */ {"4"}, /* 0x35 */ {"5"}, /* 0x36 */ {"6"}, /* 0x37 */ {"7"}, \
    /* 0x38 */ {"8"}, /* 0x39 */ {"9"}, /* 0x3A */ {":"}, /* 0x3B */ {";"}, \
    /* 0x3C */ {"<"}, /* 0x3D */ {"="}, /* 0x3E */ {">"}, /* 0x3F */ {"?"}, \
    /* 0x40 */ {"\xC2\xA1" /* U+00A1 */}, /* 0x41 */ {"A"}, /* 0x42 */ {"B"}, /* 0x43 */ {"C"}, \
    /* 0x44 */ {"D"}, /* 0x45 */ {"E"}, /* 0x46 */ {"F"}, /* 0x47 */ {"G"}, \
    /* 0x48 */ {"H"}, /* 0x49 */ {"I"}, /* 0x4A */ {"J"}, /* 0x4B */ {"K"}, \
    /* 0x4C */ {"L"}, /* 0x4D */ {"M"}, /* 0x4E */ {"N"}, /* 0x4F */ {"O"}, \
    /* 0x50 */ {"P"}, /* 0x51 */ {"Q"}, /* 0x52 */ {"R"}, /* 0x53 */ {"S"}, \
    /* 0x54 */ {"T"}, /* 0x55 */ {"U"}, /* 0x56 */ {"V"}, /* 0x57 */ {"W"}, \
    /* 0x58 */ {"X"}, /* 0x59 */ {"Y"}, /* 0x5A */ {"Z"}, /* 0x5B */ {"\xC3\x84" /* U+00C4 */}, \
    /* 0x5C */ {"\xC3\x96" /* U+00D6 */}, /* 0x5D */ {"\xC3\x91" /* U+00D1 */}, /* 0x5E */ {"\xC3\x9C" /* U+00DC */}, /* 0x5F */ {"\xC2\xA7" /* U+00A7 */}, \
    /* 0x60 */ {"\xC2\xBF" /* U+00BF */}, /* 0x61 */ {"a"}, /* 0x62 */ {"b"}, /* 0x63 */ {"c"}, \
    /* 0x64 */ {"d"}, /* 0x65 */ {"e"}, /* 0x66 */ {"f"}, /* 0x67 */ {"g"}, \
    /* 0x68 */ {"h"}, /* 0x69 */ {"i"}, /* 0x6A */ {"j"}, /* 0x6B */ {"k"}, \
    /* 0x6C */ {"l"}, /* 0x6D */ {"m"}, /* 0x6E */ {"n"}, /* 0x6F */ {"o"}, \
    /* 0x70 */ {"p"}, /* 0x71 */ {"q"}, /* 0x72 */ {"r"}, /* 0x73 */ {"s"}, \
    /* 0x74 */ {"t"}, /* 0x75 */ {"u"}, /* 0x76 */ {"v"}, /* 0x77 */ {"w"}, \
    /* 0x78 */ {"x"}, /* 0x79 */ {"y"}, /* 0x7A */ {"z"}, /* 0x7B */ {"\xC3\xA4" /* U+00E4 */}, \
    /* 0x7C */ {"\xC3\xB6" /* U+00F6 */}, /* 0x7D */ {"\xC3\xB1" /* U+00F1 */}, /* 0x7E */ {"\xC3\xBC" /* U+00FC */}, /* 0x7F */ {"\xC3\xA0" /* U+00E0 */}

// TP-DCS 0x00: the Default GSM Alphabet
const RadioPagingCharset::Entry gsm[128] = {
    NMT_GSM_DEFAULT
};

// TP-DCS 0x30: NMT Character Set 0 - Cyrillic Alphabet, Ukrainian, Russian, Bulgarian (4.3.1)
const RadioPagingCharset::Entry cyrillic[256] = {
    NMT_GSM_DEFAULT,
    /* 0x80 */ {"\xD0\x84" /* U+0404 */}, /* 0x81 */ {"\xD0\x86" /* U+0406 */}, /* 0x82 */ {"\xD0\x87" /* U+0407 */}, /* 0x83 */ {"\xD0\x90" /* U+0410 */},
    /* 0x84 */ {"\xD0\x91" /* U+0411 */}, /* 0x85 */ {"\xD0\x92" /* U+0412 */}, /* 0x86 */ {"\xD0\x93" /* U+0413 */}, /* 0x87 */ {"\xD0\x94" /* U+0414 */},
    /* 0x88 */ {"\xD0\x95" /* U+0415 */}, /* 0x89 */ {"\xD0\x96" /* U+0416 */}, /* 0x8A */ {"\xD0\x97" /* U+0417 */}, /* 0x8B */ {"\xD0\x98" /* U+0418 */},
    /* 0x8C */ {"\xD0\x99" /* U+0419 */}, /* 0x8D */ {"\xD0\x9A" /* U+041A */}, /* 0x8E */ {"\xD0\x9B" /* U+041B */}, /* 0x8F */ {"\xD0\x9C" /* U+041C */},
    /* 0x90 */ {"\xD0\x9D" /* U+041D */}, /* 0x91 */ {"\xD0\x9E" /* U+041E */}, /* 0x92 */ {"\xD0\x9F" /* U+041F */}, /* 0x93 */ {"\xD0\xA0" /* U+0420 */},
    /* 0x94 */ {"\xD0\xA1" /* U+0421 */}, /* 0x95 */ {"\xD0\xA2" /* U+0422 */}, /* 0x96 */ {"\xD0\xA3" /* U+0423 */}, /* 0x97 */ {"\xD0\xA4" /* U+0424 */},
    /* 0x98 */ {"\xD0\xA5" /* U+0425 */}, /* 0x99 */ {"\xD0\xA6" /* U+0426 */}, /* 0x9A */ {"\xD0\xA7" /* U+0427 */}, /* 0x9B */ {"\xD0\xA8" /* U+0428 */},
    /* 0x9C */ {"\xD0\xA9" /* U+0429 */}, /* 0x9D */ {"\xD0\xAA" /* U+042A */}, /* 0x9E */ {"\xD0\xAB" /* U+042B */}, /* 0x9F */ {"\xD0\xAC" /* U+042C */},
    /* 0xA0 */ {"\xD1\x94" /* U+0454 */}, /* 0xA1 */ {"\xD1\x96" /* U+0456 */}, /* 0xA2 */ {"\xD1\x97" /* U+0457 */}, /* 0xA3 */ {"\xD0\xB0" /* U+0430 */},
    /* 0xA4 */ {"\xD0\xB1" /* U+0431 */}, /* 0xA5 */ {"\xD0\xB2" /* U+0432 */}, /* 0xA6 */ {"\xD0\xB3" /* U+0433 */}, /* 0xA7 */ {"\xD0\xB4" /* U+0434 */},
    /* 0xA8 */ {"\xD0\xB5" /* U+0435 */}, /* 0xA9 */ {"\xD0\xB6" /* U+0436 */}, /* 0xAA */ {"\xD0\xB7" /* U+0437 */}, /* 0xAB */ {"\xD0\xB8" /* U+0438 */},
    /* 0xAC */ {"\xD0\xB9" /* U+0439 */}, /* 0xAD */ {"\xD0\xBA" /* U+043A */}, /* 0xAE */ {"\xD0\xBB" /* U+043B */}, /* 0xAF */ {"\xD0\xBC" /* U+043C */},
    /* 0xB0 */ {"\xD0\xBD" /* U+043D */}, /* 0xB1 */ {"\xD0\xBE" /* U+043E */}, /* 0xB2 */ {"\xD0\xBF" /* U+043F */}, /* 0xB3 */ {"\xD1\x80" /* U+0440 */},
    /* 0xB4 */ {"\xD1\x81" /* U+0441 */}, /* 0xB5 */ {"\xD1\x82" /* U+0442 */}, /* 0xB6 */ {"\xD1\x83" /* U+0443 */}, /* 0xB7 */ {"\xD1\x84" /* U+0444 */},
    /* 0xB8 */ {"\xD1\x85" /* U+0445 */}, /* 0xB9 */ {"\xD1\x86" /* U+0446 */}, /* 0xBA */ {"\xD1\x87" /* U+0447 */}, /* 0xBB */ {"\xD1\x88" /* U+0448 */},
    /* 0xBC */ {"\xD1\x89" /* U+0449 */}, /* 0xBD */ {"\xD1\x8A" /* U+044A */}, /* 0xBE */ {"\xD1\x8B" /* U+044B */}, /* 0xBF */ {"\xD1\x8C" /* U+044C */},
    /* 0xC0 */ {"\xD0\xAD" /* U+042D */}, /* 0xC1 */ {"\xD0\xAE" /* U+042E */}, /* 0xC2 */ {"\xD0\xAF" /* U+042F */}, /* 0xC3 */ {"\xD2\x90" /* U+0490 */},
    /* 0xC4 */ {"\xD0\x81" /* U+0401 */}, /* 0xC5 */ {}, /* 0xC6 */ {}, /* 0xC7 */ {},
    /* 0xC8 */ {}, /* 0xC9 */ {}, /* 0xCA */ {}, /* 0xCB */ {},
    /* 0xCC */ {}, /* 0xCD */ {}, /* 0xCE */ {}, /* 0xCF */ {},
    /* 0xD0 */ {}, /* 0xD1 */ {}, /* 0xD2 */ {}, /* 0xD3 */ {},
    /* 0xD4 */ {}, /* 0xD5 */ {}, /* 0xD6 */ {}, /* 0xD7 */ {},
    /* 0xD8 */ {}, /* 0xD9 */ {}, /* 0xDA */ {}, /* 0xDB */ {},
    /* 0xDC */ {}, /* 0xDD */ {}, /* 0xDE */ {}, /* 0xDF */ {},
    /* 0xE0 */ {"\xD1\x8D" /* U+044D */}, /* 0xE1 */ {"\xD1\x8E" /* U+044E */}, /* 0xE2 */ {"\xD1\x8F" /* U+044F */}, /* 0xE3 */ {"\xD2\x91" /* U+0491 */},
    /* 0xE4 */ {"\xD1\x91" /* U+0451 */}, /* 0xE5 */ {}, /* 0xE6 */ {}, /* 0xE7 */ {},
    /* 0xE8 */ {}, /* 0xE9 */ {}, /* 0xEA */ {}, /* 0xEB */ {},
    /* 0xEC */ {}, /* 0xED */ {}, /* 0xEE */ {}, /* 0xEF */ {},
    /* 0xF0 */ {}, /* 0xF1 */ {}, /* 0xF2 */ {}, /* 0xF3 */ {},
    /* 0xF4 */ {}, /* 0xF5 */ {}, /* 0xF6 */ {}, /* 0xF7 */ {},
    /* 0xF8 */ {}, /* 0xF9 */ {}, /* 0xFA */ {}, /* 0xFB */ {},
    /* 0xFC */ {}, /* 0xFD */ {}, /* 0xFE */ {}, /* 0xFF */ {},
};

// TP-DCS 0x31: NMT Character Set 1 - Baltic Alphabet, Lithuanian, Latvian, Estonian, Lappish (4.3.2)
const RadioPagingCharset::Entry baltic[256] = {
    NMT_GSM_DEFAULT,
    /* 0x80 */ {"\xC3\x81" /* U+00C1 */}, /* 0x81 */ {"\xC3\x95" /* U+00D5 */}, /* 0x82 */ {"\xC4\x80" /* U+0100 */}, /* 0x83 */ {"\xC4\x84" /* U+0104 */},
    /* 0x84 */ {"\xC4\x8C" /* U+010C */}, /* 0x85 */ {"\xC4\x90" /* U+0110 */}, /* 0x86 */ {"\xC4\x92" /* U+0112 */}, /* 0x87 */ {"\xC4\x96" /* U+0116 */},
    /* 0x88 */ {"\xC4\x98" /* U+0118 */}, /* 0x89 */ {"\xC4\xA2" /* U+0122 */}, /* 0x8A */ {"\xC4\xAA" /* U+012A */}, /* 0x8B */ {"\xC4\xAE" /* U+012E */},
    /* 0x8C */ {"\xC4\xB6" /* U+0136 */}, /* 0x8D */ {"\xC4\xBB" /* U+013B */}, /* 0x8E */ {"\xC5\x85" /* U+0145 */}, /* 0x8F */ {"\xC5\x8A" /* U+014A */},
    /* 0x90 */ {"\xC5\x96" /* U+0156 */}, /* 0x91 */ {"\xC5\xA0" /* U+0160 */}, /* 0x92 */ {"\xC5\xA6" /* U+0166 */}, /* 0x93 */ {"\xC5\xAA" /* U+016A */},
    /* 0x94 */ {"\xC5\xB2" /* U+0172 */}, /* 0x95 */ {"\xC5\xBD" /* U+017D */}, /* 0x96 */ {}, /* 0x97 */ {},
    /* 0x98 */ {}, /* 0x99 */ {}, /* 0x9A */ {}, /* 0x9B */ {},
    /* 0x9C */ {}, /* 0x9D */ {}, /* 0x9E */ {}, /* 0x9F */ {},
    /* 0xA0 */ {"\xC3\xA1" /* U+00E1 */}, /* 0xA1 */ {"\xC3\xB5" /* U+00F5 */}, /* 0xA2 */ {"\xC4\x81" /* U+0101 */}, /* 0xA3 */ {"\xC4\x85" /* U+0105 */},
    /* 0xA4 */ {"\xC4\x8D" /* U+010D */}, /* 0xA5 */ {"\xC4\x91" /* U+0111 */}, /* 0xA6 */ {"\xC4\x93" /* U+0113 */}, /* 0xA7 */ {"\xC4\x97" /* U+0117 */},
    /* 0xA8 */ {"\xC4\x99" /* U+0119 */}, /* 0xA9 */ {"\xC4\xA3" /* U+0123 */}, /* 0xAA */ {"\xC4\xAB" /* U+012B */}, /* 0xAB */ {"\xC4\xAF" /* U+012F */},
    /* 0xAC */ {"\xC4\xB7" /* U+0137 */}, /* 0xAD */ {"\xC4\xBC" /* U+013C */}, /* 0xAE */ {"\xC5\x86" /* U+0146 */}, /* 0xAF */ {"\xC5\x8B" /* U+014B */},
    /* 0xB0 */ {"\xC5\x97" /* U+0157 */}, /* 0xB1 */ {"\xC5\xA1" /* U+0161 */}, /* 0xB2 */ {"\xC5\xA7" /* U+0167 */}, /* 0xB3 */ {"\xC5\xAB" /* U+016B */},
    /* 0xB4 */ {"\xC5\xB3" /* U+0173 */}, /* 0xB5 */ {"\xC5\xBE" /* U+017E */}, /* 0xB6 */ {}, /* 0xB7 */ {},
    /* 0xB8 */ {}, /* 0xB9 */ {}, /* 0xBA */ {}, /* 0xBB */ {},
    /* 0xBC */ {}, /* 0xBD */ {}, /* 0xBE */ {}, /* 0xBF */ {},
    /* 0xC0 */ {}, /* 0xC1 */ {}, /* 0xC2 */ {}, /* 0xC3 */ {},
    /* 0xC4 */ {}, /* 0xC5 */ {}, /* 0xC6 */ {}, /* 0xC7 */ {},
    /* 0xC8 */ {}, /* 0xC9 */ {}, /* 0xCA */ {}, /* 0xCB */ {},
    /* 0xCC */ {}, /* 0xCD */ {}, /* 0xCE */ {}, /* 0xCF */ {},
    /* 0xD0 */ {}, /* 0xD1 */ {}, /* 0xD2 */ {}, /* 0xD3 */ {},
    /* 0xD4 */ {}, /* 0xD5 */ {}, /* 0xD6 */ {}, /* 0xD7 */ {},
    /* 0xD8 */ {}, /* 0xD9 */ {}, /* 0xDA */ {}, /* 0xDB */ {},
    /* 0xDC */ {}, /* 0xDD */ {}, /* 0xDE */ {}, /* 0xDF */ {},
    /* 0xE0 */ {}, /* 0xE1 */ {}, /* 0xE2 */ {}, /* 0xE3 */ {},
    /* 0xE4 */ {}, /* 0xE5 */ {}, /* 0xE6 */ {}, /* 0xE7 */ {},
    /* 0xE8 */ {}, /* 0xE9 */ {}, /* 0xEA */ {}, /* 0xEB */ {},
    /* 0xEC */ {}, /* 0xED */ {}, /* 0xEE */ {}, /* 0xEF */ {},
    /* 0xF0 */ {}, /* 0xF1 */ {}, /* 0xF2 */ {}, /* 0xF3 */ {},
    /* 0xF4 */ {}, /* 0xF5 */ {}, /* 0xF6 */ {}, /* 0xF7 */ {},
    /* 0xF8 */ {}, /* 0xF9 */ {}, /* 0xFA */ {}, /* 0xFB */ {},
    /* 0xFC */ {}, /* 0xFD */ {}, /* 0xFE */ {}, /* 0xFF */ {},
};

// TP-DCS 0x32: NMT Character Set 2 - Central Alphabet, Czech, Croatian, Hungarian, Polish, Romanian, Slovak, Slovene,
// Turkish (4.3.3)
const RadioPagingCharset::Entry central[256] = {
    NMT_GSM_DEFAULT,
    /* 0x80 */ {"\xC3\x81" /* U+00C1 */}, /* 0x81 */ {"\xC3\x82" /* U+00C2 */}, /* 0x82 */ {"\xC3\x87" /* U+00C7, also GSM 0x09 */}, /* 0x83 */ {"\xC3\x8D" /* U+00CD */},
    /* 0x84 */ {"\xC3\x8E" /* U+00CE */}, /* 0x85 */ {"\xC3\x93" /* U+00D3 */}, /* 0x86 */ {"\xC3\x94" /* U+00D4 */}, /* 0x87 */ {"\xC3\x9A" /* U+00DA */},
    /* 0x88 */ {"\xC4\x82" /* U+0102 */}, /* 0x89 */ {"\xC4\x84" /* U+0104 */}, /* 0x8A */ {"\xC4\x86" /* U+0106 */}, /* 0x8B */ {"\xC4\x8C" /* U+010C */},
    /* 0x8C */ {"\xC4\x8E" /* U+010E */}, /* 0x8D */ {"\xC4\x90" /* U+0110 */}, /* 0x8E */ {"\xC4\x98" /* U+0118 */}, /* 0x8F */ {"\xC4\x9A" /* U+011A */},
    /* 0x90 */ {"\xC4\x9E" /* U+011E */}, /* 0x91 */ {"\xC4\xB0" /* U+0130 */}, /* 0x92 */ {"\xC4\xB9" /* U+0139 */}, /* 0x93 */ {"\xC4\xBD" /* U+013D */},
    /* 0x94 */ {"\xC5\x81" /* U+0141 */}, /* 0x95 */ {"\xC5\x83" /* U+0143 */}, /* 0x96 */ {"\xC5\x87" /* U+0147 */}, /* 0x97 */ {"\xC5\x90" /* U+0150 */},
    /* 0x98 */ {"\xC5\x94" /* U+0154 */}, /* 0x99 */ {"\xC5\x98" /* U+0158 */}, /* 0x9A */ {"\xC5\x9A" /* U+015A */}, /* 0x9B */ {"\xC5\x9E" /* U+015E */},
    /* 0x9C */ {"\xC5\xA0" /* U+0160 */}, /* 0x9D */ {"\xC5\xA2" /* U+0162 */}, /* 0x9E */ {"\xC5\xA4" /* U+0164 */}, /* 0x9F */ {"\xC5\xB0" /* U+0170 */},
    /* 0xA0 */ {"\xC3\xA1" /* U+00E1 */}, /* 0xA1 */ {"\xC3\xA2" /* U+00E2 */}, /* 0xA2 */ {"\xC3\xA7" /* U+00E7 */}, /* 0xA3 */ {"\xC3\xAD" /* U+00ED */},
    /* 0xA4 */ {"\xC3\xAE" /* U+00EE */}, /* 0xA5 */ {"\xC3\xB3" /* U+00F3 */}, /* 0xA6 */ {"\xC3\xB4" /* U+00F4 */}, /* 0xA7 */ {"\xC3\xBA" /* U+00FA */},
    /* 0xA8 */ {"\xC4\x83" /* U+0103 */}, /* 0xA9 */ {"\xC4\x85" /* U+0105 */}, /* 0xAA */ {"\xC4\x87" /* U+0107 */}, /* 0xAB */ {"\xC4\x8D" /* U+010D */},
    /* 0xAC */ {"\xC4\x8F" /* U+010F */}, /* 0xAD */ {"\xC4\x91" /* U+0111 */}, /* 0xAE */ {"\xC4\x99" /* U+0119 */}, /* 0xAF */ {"\xC4\x9B" /* U+011B */},
    /* 0xB0 */ {"\xC4\x9F" /* U+011F */}, /* 0xB1 */ {"\xC4\xB1" /* U+0131 */}, /* 0xB2 */ {"\xC4\xBA" /* U+013A */}, /* 0xB3 */ {"\xC4\xBE" /* U+013E */},
    /* 0xB4 */ {"\xC5\x82" /* U+0142 */}, /* 0xB5 */ {"\xC5\x84" /* U+0144 */}, /* 0xB6 */ {"\xC5\x88" /* U+0148 */}, /* 0xB7 */ {"\xC5\x91" /* U+0151 */},
    /* 0xB8 */ {"\xC5\x95" /* U+0155 */}, /* 0xB9 */ {"\xC5\x99" /* U+0159 */}, /* 0xBA */ {"\xC5\x9B" /* U+015B */}, /* 0xBB */ {"\xC5\x9F" /* U+015F */},
    /* 0xBC */ {"\xC5\xA1" /* U+0161 */}, /* 0xBD */ {"\xC5\xA3" /* U+0163 */}, /* 0xBE */ {"\xC5\xA5" /* U+0165 */}, /* 0xBF */ {"\xC5\xB1" /* U+0171 */},
    /* 0xC0 */ {"\xC5\xAE" /* U+016E */}, /* 0xC1 */ {"\xC3\x9D" /* U+00DD */}, /* 0xC2 */ {"\xC5\xB9" /* U+0179 */}, /* 0xC3 */ {"\xC5\xBB" /* U+017B */},
    /* 0xC4 */ {"\xC5\xBD" /* U+017D */}, /* 0xC5 */ {}, /* 0xC6 */ {}, /* 0xC7 */ {},
    /* 0xC8 */ {}, /* 0xC9 */ {}, /* 0xCA */ {}, /* 0xCB */ {},
    /* 0xCC */ {}, /* 0xCD */ {}, /* 0xCE */ {}, /* 0xCF */ {},
    /* 0xD0 */ {}, /* 0xD1 */ {}, /* 0xD2 */ {}, /* 0xD3 */ {},
    /* 0xD4 */ {}, /* 0xD5 */ {}, /* 0xD6 */ {}, /* 0xD7 */ {},
    /* 0xD8 */ {}, /* 0xD9 */ {}, /* 0xDA */ {}, /* 0xDB */ {},
    /* 0xDC */ {}, /* 0xDD */ {}, /* 0xDE */ {}, /* 0xDF */ {},
    /* 0xE0 */ {"\xC5\xAF" /* U+016F */}, /* 0xE1 */ {"\xC3\xBD" /* U+00FD */}, /* 0xE2 */ {"\xC5\xBA" /* U+017A */}, /* 0xE3 */ {"\xC5\xBC" /* U+017C */},
    /* 0xE4 */ {"\xC5\xBE" /* U+017E */}, /* 0xE5 */ {}, /* 0xE6 */ {}, /* 0xE7 */ {},
    /* 0xE8 */ {}, /* 0xE9 */ {}, /* 0xEA */ {}, /* 0xEB */ {},
    /* 0xEC */ {}, /* 0xED */ {}, /* 0xEE */ {}, /* 0xEF */ {},
    /* 0xF0 */ {}, /* 0xF1 */ {}, /* 0xF2 */ {}, /* 0xF3 */ {},
    /* 0xF4 */ {}, /* 0xF5 */ {}, /* 0xF6 */ {}, /* 0xF7 */ {},
    /* 0xF8 */ {}, /* 0xF9 */ {}, /* 0xFA */ {}, /* 0xFB */ {},
    /* 0xFC */ {}, /* 0xFD */ {}, /* 0xFE */ {}, /* 0xFF */ {},
};

// TP-DCS 0x33: NMT Character Set 3 - Western Alphabet, Danish, Dutch, English, Finnish, French, German, Icelandic,
// Norwegian, Swedish (4.3.4)
const RadioPagingCharset::Entry western[256] = {
    NMT_GSM_DEFAULT,
    /* 0x80 */ {"\xC3\x80" /* U+00C0 */}, /* 0x81 */ {"\xC3\x81" /* U+00C1 */}, /* 0x82 */ {"\xC3\x82" /* U+00C2 */}, /* 0x83 */ {"\xC3\x87" /* U+00C7, also GSM 0x09 */},
    /* 0x84 */ {"\xC3\x88" /* U+00C8 */}, /* 0x85 */ {"\xC3\x8A" /* U+00CA */}, /* 0x86 */ {"\xC3\x8B" /* U+00CB */}, /* 0x87 */ {"\xC3\x8D" /* U+00CD */},
    /* 0x88 */ {"\xC3\x8E" /* U+00CE */}, /* 0x89 */ {"\xC3\x8F" /* U+00CF */}, /* 0x8A */ {"\xC3\x90" /* U+00D0 */}, /* 0x8B */ {"\xC3\x93" /* U+00D3 */},
    /* 0x8C */ {"\xC3\x94" /* U+00D4 */}, /* 0x8D */ {"\xC3\x99" /* U+00D9 */}, /* 0x8E */ {"\xC3\x9A" /* U+00DA */}, /* 0x8F */ {"\xC3\x9B" /* U+00DB */},
    /* 0x90 */ {"\xC3\x9D" /* U+00DD */}, /* 0x91 */ {"\xC3\x9E" /* U+00DE */}, /* 0x92 */ {}, /* 0x93 */ {},
    /* 0x94 */ {}, /* 0x95 */ {}, /* 0x96 */ {}, /* 0x97 */ {},
    /* 0x98 */ {}, /* 0x99 */ {}, /* 0x9A */ {}, /* 0x9B */ {},
    /* 0x9C */ {}, /* 0x9D */ {}, /* 0x9E */ {}, /* 0x9F */ {},
    /* 0xA0 */ {"\xC3\xA0" /* U+00E0, also GSM 0x7F */}, /* 0xA1 */ {"\xC3\xA1" /* U+00E1 */}, /* 0xA2 */ {"\xC3\xA2" /* U+00E2 */}, /* 0xA3 */ {"\xC3\xA7" /* U+00E7 */},
    /* 0xA4 */ {"\xC3\xA8" /* U+00E8, also GSM 0x04 */}, /* 0xA5 */ {"\xC3\xAA" /* U+00EA */}, /* 0xA6 */ {"\xC3\xAB" /* U+00EB */}, /* 0xA7 */ {"\xC3\xAD" /* U+00ED */},
    /* 0xA8 */ {"\xC3\xAE" /* U+00EE */}, /* 0xA9 */ {"\xC3\xAF" /* U+00EF */}, /* 0xAA */ {"\xC3\xB0" /* U+00F0 */}, /* 0xAB */ {"\xC3\xB3" /* U+00F3 */},
    /* 0xAC */ {"\xC3\xB4" /* U+00F4 */}, /* 0xAD */ {"\xC3\xB9" /* U+00F9, also GSM 0x06 */}, /* 0xAE */ {"\xC3\xBA" /* U+00FA */}, /* 0xAF */ {"\xC3\xBB" /* U+00FB */},
    /* 0xB0 */ {"\xC3\xBD" /* U+00FD */}, /* 0xB1 */ {"\xC3\xBE" /* U+00FE */}, /* 0xB2 */ {}, /* 0xB3 */ {},
    /* 0xB4 */ {}, /* 0xB5 */ {}, /* 0xB6 */ {}, /* 0xB7 */ {},
    /* 0xB8 */ {}, /* 0xB9 */ {}, /* 0xBA */ {}, /* 0xBB */ {},
    /* 0xBC */ {}, /* 0xBD */ {}, /* 0xBE */ {}, /* 0xBF */ {},
    /* 0xC0 */ {}, /* 0xC1 */ {}, /* 0xC2 */ {}, /* 0xC3 */ {},
    /* 0xC4 */ {}, /* 0xC5 */ {}, /* 0xC6 */ {}, /* 0xC7 */ {},
    /* 0xC8 */ {}, /* 0xC9 */ {}, /* 0xCA */ {}, /* 0xCB */ {},
    /* 0xCC */ {}, /* 0xCD */ {}, /* 0xCE */ {}, /* 0xCF */ {},
    /* 0xD0 */ {}, /* 0xD1 */ {}, /* 0xD2 */ {}, /* 0xD3 */ {},
    /* 0xD4 */ {}, /* 0xD5 */ {}, /* 0xD6 */ {}, /* 0xD7 */ {},
    /* 0xD8 */ {}, /* 0xD9 */ {}, /* 0xDA */ {}, /* 0xDB */ {},
    /* 0xDC */ {}, /* 0xDD */ {}, /* 0xDE */ {}, /* 0xDF */ {},
    /* 0xE0 */ {}, /* 0xE1 */ {}, /* 0xE2 */ {}, /* 0xE3 */ {},
    /* 0xE4 */ {}, /* 0xE5 */ {}, /* 0xE6 */ {}, /* 0xE7 */ {},
    /* 0xE8 */ {}, /* 0xE9 */ {}, /* 0xEA */ {}, /* 0xEB */ {},
    /* 0xEC */ {}, /* 0xED */ {}, /* 0xEE */ {}, /* 0xEF */ {},
    /* 0xF0 */ {}, /* 0xF1 */ {}, /* 0xF2 */ {}, /* 0xF3 */ {},
    /* 0xF4 */ {}, /* 0xF5 */ {}, /* 0xF6 */ {}, /* 0xF7 */ {},
    /* 0xF8 */ {}, /* 0xF9 */ {}, /* 0xFA */ {}, /* 0xFB */ {},
    /* 0xFC */ {}, /* 0xFD */ {}, /* 0xFE */ {}, /* 0xFF */ {},
};

// TP-DCS 0x34: NMT Character Set 4 - Thai Alphabet (4.3.5; the list of 4.3 still names set 4 "spare"). 0x80 and
// 0xBB-0xBE are printed empty.
const RadioPagingCharset::Entry thai[256] = {
    NMT_GSM_DEFAULT,
    /* 0x80 */ {}, /* 0x81 */ {"\xE0\xB8\x81" /* U+0E01 */}, /* 0x82 */ {"\xE0\xB8\x82" /* U+0E02 */}, /* 0x83 */ {"\xE0\xB8\x83" /* U+0E03 */},
    /* 0x84 */ {"\xE0\xB8\x84" /* U+0E04 */}, /* 0x85 */ {"\xE0\xB8\x85" /* U+0E05 */}, /* 0x86 */ {"\xE0\xB8\x86" /* U+0E06 */}, /* 0x87 */ {"\xE0\xB8\x87" /* U+0E07 */},
    /* 0x88 */ {"\xE0\xB8\x88" /* U+0E08 */}, /* 0x89 */ {"\xE0\xB8\x89" /* U+0E09 */}, /* 0x8A */ {"\xE0\xB8\x8A" /* U+0E0A */}, /* 0x8B */ {"\xE0\xB8\x8B" /* U+0E0B */},
    /* 0x8C */ {"\xE0\xB8\x8C" /* U+0E0C */}, /* 0x8D */ {"\xE0\xB8\x8D" /* U+0E0D */}, /* 0x8E */ {"\xE0\xB8\x8E" /* U+0E0E */}, /* 0x8F */ {"\xE0\xB8\x8F" /* U+0E0F */},
    /* 0x90 */ {"\xE0\xB8\x90" /* U+0E10 */}, /* 0x91 */ {"\xE0\xB8\x91" /* U+0E11 */}, /* 0x92 */ {"\xE0\xB8\x92" /* U+0E12 */}, /* 0x93 */ {"\xE0\xB8\x93" /* U+0E13 */},
    /* 0x94 */ {"\xE0\xB8\x94" /* U+0E14 */}, /* 0x95 */ {"\xE0\xB8\x95" /* U+0E15 */}, /* 0x96 */ {"\xE0\xB8\x96" /* U+0E16 */}, /* 0x97 */ {"\xE0\xB8\x97" /* U+0E17 */},
    /* 0x98 */ {"\xE0\xB8\x98" /* U+0E18 */}, /* 0x99 */ {"\xE0\xB8\x99" /* U+0E19 */}, /* 0x9A */ {"\xE0\xB8\x9A" /* U+0E1A */}, /* 0x9B */ {"\xE0\xB8\x9B" /* U+0E1B */},
    /* 0x9C */ {"\xE0\xB8\x9C" /* U+0E1C */}, /* 0x9D */ {"\xE0\xB8\x9D" /* U+0E1D */}, /* 0x9E */ {"\xE0\xB8\x9E" /* U+0E1E */}, /* 0x9F */ {"\xE0\xB8\x9F" /* U+0E1F */},
    /* 0xA0 */ {"\xE0\xB8\xA0" /* U+0E20 */}, /* 0xA1 */ {"\xE0\xB8\xA1" /* U+0E21 */}, /* 0xA2 */ {"\xE0\xB8\xA2" /* U+0E22 */}, /* 0xA3 */ {"\xE0\xB8\xA3" /* U+0E23 */},
    /* 0xA4 */ {"\xE0\xB8\xA4" /* U+0E24 */}, /* 0xA5 */ {"\xE0\xB8\xA5" /* U+0E25 */}, /* 0xA6 */ {"\xE0\xB8\xA6" /* U+0E26 */}, /* 0xA7 */ {"\xE0\xB8\xA7" /* U+0E27 */},
    /* 0xA8 */ {"\xE0\xB8\xA8" /* U+0E28 */}, /* 0xA9 */ {"\xE0\xB8\xA9" /* U+0E29 */}, /* 0xAA */ {"\xE0\xB8\xAA" /* U+0E2A */}, /* 0xAB */ {"\xE0\xB8\xAB" /* U+0E2B */},
    /* 0xAC */ {"\xE0\xB8\xAC" /* U+0E2C */}, /* 0xAD */ {"\xE0\xB8\xAD" /* U+0E2D */}, /* 0xAE */ {"\xE0\xB8\xAE" /* U+0E2E */}, /* 0xAF */ {"\xE0\xB8\xAF" /* U+0E2F */},
    /* 0xB0 */ {"\xE0\xB8\xB0" /* U+0E30 */}, /* 0xB1 */ {"\xE0\xB8\xB1" /* U+0E31 */}, /* 0xB2 */ {"\xE0\xB8\xB2" /* U+0E32 */}, /* 0xB3 */ {"\xE0\xB8\xB3" /* U+0E33 */},
    /* 0xB4 */ {"\xE0\xB8\xB4" /* U+0E34 */}, /* 0xB5 */ {"\xE0\xB8\xB5" /* U+0E35 */}, /* 0xB6 */ {"\xE0\xB8\xB6" /* U+0E36 */}, /* 0xB7 */ {"\xE0\xB8\xB7" /* U+0E37 */},
    /* 0xB8 */ {"\xE0\xB8\xB8" /* U+0E38 */}, /* 0xB9 */ {"\xE0\xB8\xB9" /* U+0E39 */}, /* 0xBA */ {"\xE0\xB8\xBA" /* U+0E3A */}, /* 0xBB */ {},
    /* 0xBC */ {}, /* 0xBD */ {}, /* 0xBE */ {}, /* 0xBF */ {"\xE0\xB8\xBF" /* U+0E3F */},
    /* 0xC0 */ {"\xE0\xB9\x80" /* U+0E40 */}, /* 0xC1 */ {"\xE0\xB9\x81" /* U+0E41 */}, /* 0xC2 */ {"\xE0\xB9\x82" /* U+0E42 */}, /* 0xC3 */ {"\xE0\xB9\x83" /* U+0E43 */},
    /* 0xC4 */ {"\xE0\xB9\x84" /* U+0E44 */}, /* 0xC5 */ {"\xE0\xB9\x85" /* U+0E45 */}, /* 0xC6 */ {"\xE0\xB9\x86" /* U+0E46 */}, /* 0xC7 */ {"\xE0\xB9\x87" /* U+0E47 */},
    /* 0xC8 */ {"\xE0\xB9\x88" /* U+0E48 */}, /* 0xC9 */ {"\xE0\xB9\x89" /* U+0E49 */}, /* 0xCA */ {"\xE0\xB9\x8A" /* U+0E4A */}, /* 0xCB */ {"\xE0\xB9\x8B" /* U+0E4B */},
    /* 0xCC */ {"\xE0\xB9\x8C" /* U+0E4C */}, /* 0xCD */ {"\xE0\xB9\x8D" /* U+0E4D */}, /* 0xCE */ {"\xE0\xB9\x8E" /* U+0E4E */}, /* 0xCF */ {"\xE0\xB9\x8F" /* U+0E4F */},
    /* 0xD0 */ {"\xE0\xB9\x90" /* U+0E50 */}, /* 0xD1 */ {"\xE0\xB9\x91" /* U+0E51 */}, /* 0xD2 */ {"\xE0\xB9\x92" /* U+0E52 */}, /* 0xD3 */ {"\xE0\xB9\x93" /* U+0E53 */},
    /* 0xD4 */ {"\xE0\xB9\x94" /* U+0E54 */}, /* 0xD5 */ {"\xE0\xB9\x95" /* U+0E55 */}, /* 0xD6 */ {"\xE0\xB9\x96" /* U+0E56 */}, /* 0xD7 */ {"\xE0\xB9\x97" /* U+0E57 */},
    /* 0xD8 */ {"\xE0\xB9\x98" /* U+0E58 */}, /* 0xD9 */ {"\xE0\xB9\x99" /* U+0E59 */}, /* 0xDA */ {"\xE0\xB9\x9A" /* U+0E5A */}, /* 0xDB */ {"\xE0\xB9\x9B" /* U+0E5B */},
    /* 0xDC */ {}, /* 0xDD */ {}, /* 0xDE */ {}, /* 0xDF */ {},
    /* 0xE0 */ {}, /* 0xE1 */ {}, /* 0xE2 */ {}, /* 0xE3 */ {},
    /* 0xE4 */ {}, /* 0xE5 */ {}, /* 0xE6 */ {}, /* 0xE7 */ {},
    /* 0xE8 */ {}, /* 0xE9 */ {}, /* 0xEA */ {}, /* 0xEB */ {},
    /* 0xEC */ {}, /* 0xED */ {}, /* 0xEE */ {}, /* 0xEF */ {},
    /* 0xF0 */ {}, /* 0xF1 */ {}, /* 0xF2 */ {}, /* 0xF3 */ {},
    /* 0xF4 */ {}, /* 0xF5 */ {}, /* 0xF6 */ {}, /* 0xF7 */ {},
    /* 0xF8 */ {}, /* 0xF9 */ {}, /* 0xFA */ {}, /* 0xFB */ {},
    /* 0xFC */ {}, /* 0xFD */ {}, /* 0xFE */ {}, /* 0xFF */ {},
};

#undef NMT_GSM_DEFAULT

} // namespace

const RadioPagingCharset::Table RadioPagingCharsetTables::nmtGsm = {7, gsm};
const RadioPagingCharset::Table RadioPagingCharsetTables::nmtCyrillic = {8, cyrillic};
const RadioPagingCharset::Table RadioPagingCharsetTables::nmtBaltic = {8, baltic};
const RadioPagingCharset::Table RadioPagingCharsetTables::nmtCentral = {8, central};
const RadioPagingCharset::Table RadioPagingCharsetTables::nmtWestern = {8, western};
const RadioPagingCharset::Table RadioPagingCharsetTables::nmtThai = {8, thai};
