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

#ifndef MODEMRADIOPAGING_RADIOPAGINGFSKDEMOD_H
#define MODEMRADIOPAGING_RADIOPAGINGFSKDEMOD_H

#include <cstdint>
#include <vector>       // used by util/movingaverage.h

#include "util/movingaverage.h"
#include "export.h"

// Symbol recovery for 2-level and 4-level FSK from FM discriminator output.
//
// Input samples are discriminator output scaled so that the outer symbol deviation
// is about +/-1. The chain is:
//   matched filter   boxcar over one symbol (integrate and dump for NRZ symbols)
//   symbol timing    Gardner timing error detector on the matched filter output,
//                    sampled twice per symbol with linear interpolation, so any
//                    (fractional) samples per symbol ratio is supported
//   levels           decision directed tracking of DC offset and deviation
//   decision         2-level: sign; 4-level: thresholds at 0 and +/-2/3 of deviation
//
// The symbol clock free runs at the nominal rate and is only nudged by the timing
// error, so an isolated noise transition cannot insert or delete a symbol.
// Acquisition uses higher loop gains than tracking (setLocked()).
class MODEMRADIOPAGING_API RadioPagingFskDemod
{
public:
    struct Symbol
    {
        int m_value;        //!< 2-level: 0 or 1 (1 = positive deviation); 4-level: 0..3 (3 = highest frequency)
        float m_soft;       //!< Normalised symbol value: outer levels +/-1, inner +/-1/3
        float m_error;      //!< Distance from the decided level, normalised
        float m_offset;     //!< The symbol instant lies this many samples (0 to 1) before the last input sample
    };

    RadioPagingFskDemod();

    //!< sampleRate in Hz, symbolRate in symbols/s, levels 2 or 4
    void create(double sampleRate, double symbolRate, int levels);
    void reset();
    //!< Process one discriminator sample. Returns true when a symbol has been decided
    bool process(float sample, Symbol& symbol);

    //!< Use tracking (low) loop gains once a sync word has been found, acquisition gains otherwise
    void setLocked(bool locked) { m_locked = locked; }
    //!< Set DC offset and outer level deviation, e.g. measured over a sync word
    void setLevels(float dc, float deviation);
    //!< Place the next symbol instant samplesToNextSymbol samples after the last input sample,
    //!< e.g. to continue the symbol clock of another demodulator at a new symbol rate
    void startSymbols(double samplesToNextSymbol);

    float getDC() const { return m_dc; }
    float getDeviation() const { return m_deviation; }
    double getSamplesPerSymbol() const { return m_samplesPerSymbol; }
    int getLevels() const { return m_levels; }

private:
    double m_samplesPerSymbol;
    int m_levels;
    bool m_locked;

    MovingAverageUtilVar<float, double> m_matchedFilter;
    float m_prevFiltered;           //!< Matched filter output of the previous input sample
    double m_timeToNext;            //!< Samples until the next half symbol instant
    bool m_nextIsSymbol;            //!< The next half symbol instant is a symbol (not a mid point)
    float m_prevSymbolValue;        //!< Matched filter value at the previous symbol instant
    float m_midValue;               //!< Matched filter value midway between the last two symbols
    bool m_haveMid;
    double m_timingPower;           //!< Running mean square of symbol values, normalises the timing error

    float m_dc;
    float m_deviation;

    int decide(float normalised) const;
    static float levelValue(int symbol, int levels);
};

#endif // MODEMRADIOPAGING_RADIOPAGINGFSKDEMOD_H
