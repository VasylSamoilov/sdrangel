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

#ifndef MODEMAMPS_TESTS_PHONE_H_
#define MODEMAMPS_TESTS_PHONE_H_

// A test phone for the AMPS tests: it reads the FOCC as a phone and sends RECC bursts, at 48 kHz.

#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "ampsbch.h"
#include "ampsfsk.h"
#include "ampsmessages.h"
#include "ampsrecc.h"
#include "ampsstream.h"

using namespace modemamps;

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static const double kPi = 3.14159265358979323846;
static const double kRate = 48000.0;
static const int kBlock = 480;

// The forward control channel as a phone reads it: after the sync, 5 repeats of words A and B,
// 44 bits each with a busy-idle bit after every 10 (osmocom's amps_decode_bits_focc); word A by
// the vote of its repeats.
class FoccReader
{
public:
    FoccReader() :
        m_demod(kRate, kFskBitRate, kFskDeviation)
    {
        m_demod.setBitSink([this](int bit, double, double) { this->bit(bit); });
    }
    void process(const float* s, int n) { m_demod.process(s, n); }
    std::vector<uint64_t> words;

private:
    void bit(int b)
    {
        if (!m_inFrame)
        {
            m_sync = ((m_sync << 1) | static_cast<uint32_t>(b)) & 0x7ff;
            if (m_sync == kWordSync)
            {
                m_inFrame = true;
                m_bits.clear();
                m_sync = 0x555;
                m_demod.startFrame();
            }
            return;
        }
        m_bits.push_back(static_cast<uint8_t>(b));
        if (m_bits.size() < 441) {
            return;
        }
        // the B/I bit after the sync, then 10 words of 44
        uint64_t a[5];
        for (int w = 0; w < 10; w++)
        {
            uint64_t word = 0;
            for (int j = 0; j < 44; j++)
            {
                if (j % 11 == 10) {
                    continue;
                }
                word = (word << 1) | m_bits[1 + w * 44 + j];
            }
            if ((w & 1) == 0) {
                a[w / 2] = word;
            }
        }
        uint64_t voted = 0;
        for (int b2 = 0; b2 < 40; b2++)
        {
            int ones = 0;
            for (int r = 0; r < 5; r++) {
                ones += static_cast<int>((a[r] >> b2) & 1);
            }
            if (ones >= 3) {
                voted |= 1ull << b2;
            }
        }
        if (bchCheck(voted, kForwardMessageBits)) {
            words.push_back(voted);
        }
        m_inFrame = false;
        m_demod.endFrame();
    }

    ManchesterDemodulator m_demod;
    uint32_t m_sync = 0x555;
    bool m_inFrame = false;
    std::vector<uint8_t> m_bits;
};

// A phone sending bits on the reverse control channel
class PhoneTx
{
public:
    PhoneTx() :
        m_mod(kRate, kFskBitRate, kFskDeviation)
    {
        m_mod.setBitSource([this]() {
            if (m_bits.empty()) {
                return -1;
            }
            const int b = m_bits.front();
            m_bits.pop_front();
            return b;
        });
    }
    void send(const std::vector<uint8_t>& bits) { m_bits.insert(m_bits.end(), bits.begin(), bits.end()); }
    // the samples it sent of this block (the rest is silence)
    int render(float* s, int n)
    {
        const int done = m_mod.render(s, n, false);
        std::fill(s + done, s + n, 0.0f);
        return done;
    }
    bool sending() const { return !m_bits.empty(); }

private:
    ManchesterModulator m_mod;
    std::deque<uint8_t> m_bits;
};

inline bool heardWord(const std::vector<uint64_t>& w, const std::function<bool(uint64_t)>& match)
{
    for (uint64_t x : w)
    {
        if (match(x)) {
            return true;
        }
    }
    return false;
}

#endif // MODEMAMPS_TESTS_PHONE_H_
