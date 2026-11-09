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

#include "radiopagingfskdemod.h"

#include <algorithm>
#include <cmath>

namespace {

// Timing loop gains, as the fraction of a half symbol corrected per unit
// normalised timing error
const double timingGainAcquire = 0.10;
const double timingGainTrack = 0.02;
// Level tracking gains, per symbol
const float levelGainAcquire = 0.02f;
const float levelGainTrack = 0.005f;
// Smoothing of the symbol power used to normalise the timing error
const double timingPowerAlpha = 0.01;

} // namespace

RadioPagingFskDemod::RadioPagingFskDemod() :
    m_samplesPerSymbol(1.0),
    m_levels(2),
    m_locked(false),
    m_matchedFilter(1)
{
    reset();
}

void RadioPagingFskDemod::create(double sampleRate, double symbolRate, int levels)
{
    m_samplesPerSymbol = sampleRate / symbolRate;
    m_levels = (levels == 4) ? 4 : 2;
    m_matchedFilter.resize(std::max(1, (int) std::lround(m_samplesPerSymbol)));
    reset();
}

void RadioPagingFskDemod::reset()
{
    m_matchedFilter.reset();
    m_prevFiltered = 0.0f;
    m_timeToNext = m_samplesPerSymbol / 2.0;
    m_nextIsSymbol = true;
    m_prevSymbolValue = 0.0f;
    m_midValue = 0.0f;
    m_haveMid = false;
    m_timingPower = 1.0;
    m_dc = 0.0f;
    m_deviation = 1.0f;
    m_locked = false;
}

void RadioPagingFskDemod::setLevels(float dc, float deviation)
{
    m_dc = dc;
    m_deviation = std::max(deviation, 1e-3f);
}

void RadioPagingFskDemod::startSymbols(double samplesToNextSymbol)
{
    m_timeToNext = samplesToNextSymbol;
    m_nextIsSymbol = true;
    m_haveMid = false;
}

float RadioPagingFskDemod::levelValue(int symbol, int levels)
{
    if (levels == 4)
    {
        static const float values[4] = {-1.0f, -1.0f/3.0f, 1.0f/3.0f, 1.0f};
        return values[symbol & 3];
    }
    else
    {
        return symbol ? 1.0f : -1.0f;
    }
}

int RadioPagingFskDemod::decide(float normalised) const
{
    if (m_levels == 4)
    {
        if (normalised > 2.0f/3.0f) {
            return 3;
        } else if (normalised > 0.0f) {
            return 2;
        } else if (normalised > -2.0f/3.0f) {
            return 1;
        } else {
            return 0;
        }
    }
    else
    {
        return normalised >= 0.0f ? 1 : 0;
    }
}

bool RadioPagingFskDemod::process(float sample, Symbol& symbol)
{
    bool produced = false;

    m_matchedFilter(sample);
    float filtered = m_matchedFilter.asFloat();

    m_timeToNext -= 1.0;

    while (m_timeToNext <= 0.0)
    {
        // The instant lies between the previous input sample (fraction 0) and this one (fraction 1)
        float fraction = (float) (1.0 + m_timeToNext);
        float value = m_prevFiltered + (filtered - m_prevFiltered) * fraction - m_dc;
        double halfSymbol = m_samplesPerSymbol / 2.0;

        if (m_nextIsSymbol)
        {
            double correction = 0.0;

            if (m_haveMid)
            {
                // Gardner timing error: positive when sampling early, negative when late
                double error = (m_prevSymbolValue - value) * m_midValue / std::max(m_timingPower, 1e-6);
                error = std::max(-1.0, std::min(1.0, error));
                correction = (m_locked ? timingGainTrack : timingGainAcquire) * error * halfSymbol;
            }

            float normalised = value / m_deviation;
            int decided = decide(normalised);
            float level = levelValue(decided, m_levels);
            float levelGain = m_locked ? levelGainTrack : levelGainAcquire;

            // Decision directed level tracking
            m_dc += levelGain * (normalised - level) * m_deviation;
            m_deviation += levelGain * (std::fabs(normalised) - std::fabs(level)) * m_deviation;
            m_deviation = std::max(0.05f, std::min(20.0f, m_deviation));

            m_timingPower += timingPowerAlpha * ((double) value * value - m_timingPower);
            m_prevSymbolValue = value;

            symbol.m_value = decided;
            symbol.m_soft = normalised;
            symbol.m_error = normalised - level;
            symbol.m_offset = (float) -m_timeToNext;
            produced = true;

            m_timeToNext += halfSymbol + correction;
        }
        else
        {
            m_midValue = value;
            m_haveMid = true;
            m_timeToNext += halfSymbol;
        }

        m_nextIsSymbol = !m_nextIsSymbol;
    }

    m_prevFiltered = filtered;

    return produced;
}
