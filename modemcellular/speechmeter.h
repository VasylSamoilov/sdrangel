///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov                                             //
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

#ifndef MODEMCELLULAR_SPEECHMETER_H_
#define MODEMCELLULAR_SPEECHMETER_H_

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "export.h"

template <typename Type, uint32_t Order> class IIRFilter;

namespace modemcellular
{

// What a speech meter counted: where the speech samples landed on the level scale, how much of
// the time was pause, how many samples reached the limit, and how the speech energy splits into
// six bands of the telephone band. Levels are relative to the reference (1.0 = 0 dB).
struct MODEMCELLULAR_API SpeechStats
{
    static constexpr int kBins = 16;
    static constexpr double kLowDb = -40.0;      // the scale: kLowDb .. kHighDb in kBins bins
    static constexpr double kHighDb = 4.0;
    static constexpr int kBands = 6;
    // band edges in Hz: 300-500, 500-800, 800-1200, 1200-2000, 2000-3000, 3000-3400
    static const std::array<double, kBands + 1> kBandEdges;

    std::array<uint64_t, kBins> bins{};     // speech samples per level bin (below the scale: bin 0)
    uint64_t speech = 0;                    // samples of speech
    uint64_t pause = 0;                     // samples of pause (blocks below the gate)
    uint64_t atLimit = 0;                   // speech samples at or above the limit
    double energy = 0.0;                    // sum of the squared speech samples
    std::array<double, kBands> bandEnergy{};
    double peak = 0.0;                      // largest speech sample (absolute)

    void add(const SpeechStats& other);
    bool empty() const { return speech == 0 && pause == 0; }
    // share of the time in pauses, 0..1
    double pauseShare() const;
    // the level 99 % of the speech samples are below (upper edge of that bin), dB
    double p99Db() const;
    // share of the speech samples at the limit, 0..1
    double limitShare() const;
    // speech level (rms of the speech samples), dB
    double levelDb() const;
    // each band's energy relative to the whole speech energy, dB
    std::array<double, kBands> bandsDb() const;
};

// Measures one direction of speech of a call, at 8 kHz: the last two seconds (in steps of 200 ms)
// and the whole call. A block of 20 ms whose rms is below the gate is pause; its samples count only
// as pause. The limit is where the stage clips (the red line), relative to the reference. Not
// thread safe: the owner copies the stats out.
class MODEMCELLULAR_API SpeechMeter
{
public:
    static constexpr double kSampleRate = 8000.0;
    static constexpr int kBlock = 160;          // 20 ms
    static constexpr int kStep = 1600;          // 200 ms
    static constexpr int kSteps = 10;           // 2 s

    SpeechMeter(double gate, double limit);
    ~SpeechMeter();
    SpeechMeter(const SpeechMeter&) = delete;
    SpeechMeter& operator=(const SpeechMeter&) = delete;

    void process(const float* samples, int count);
    // the last two seconds (the steps that are complete, and the one being filled)
    SpeechStats recent() const;
    // the whole call so far
    const SpeechStats& call() const { return m_call; }
    double limit() const { return m_limit; }
    // a new call
    void reset();

private:
    void endBlock();

    double m_gate;
    double m_limit;
    struct Coeffs { std::array<double, 3> a; std::array<double, 3> b; };
    std::vector<Coeffs> m_coeffs;                                   // per band
    std::vector<std::unique_ptr<IIRFilter<double, 2>>> m_filters;  // two sections per band
    float m_block[kBlock];
    double m_bandBlock[SpeechStats::kBands];
    int m_blockFill;
    std::array<SpeechStats, kSteps> m_steps;
    int m_step;                                 // the step being filled
    int m_stepFill;                             // its samples
    SpeechStats m_call;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_SPEECHMETER_H_
