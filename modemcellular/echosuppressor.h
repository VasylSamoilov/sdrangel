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

#ifndef MODEMCELLULAR_ECHOSUPPRESSOR_H_
#define MODEMCELLULAR_ECHOSUPPRESSOR_H_

#include <cstdint>
#include <vector>

#include "export.h"

namespace modemcellular
{

// One-way echo suppressor of a call, osmocom-analog's libecho: the audio towards the mobile (the
// far end) is the reference and passes unchanged; the audio from the mobile, which carries the
// echo of the far end (the phone's earpiece into its microphone, over the radio round trip), is
// attenuated while the far end talks, the echo delay later. The delay is measured during the call
// (the envelopes of both directions, the lag of the best correlation); double talk (the mobile
// louder than the far end's echo can be) passes. Samples at the speech level (1.0 = the reference
// level of the speech path). Not thread safe.
// osmocom-analog's defaults (liboptions), Bell/CCITT style
struct EchoSuppressorConfig
{
    double thresholdDb = -40.0;     // the far end counts as talking above this
    double attenuationDb = 20.0;    // the phone's echo is measured >= 28 dB down: >= 48 dB then
    int hangoverMs = 60;            // hold time after the far end stops
    int rampMs = 2;                 // attack
    int releaseMs = 200;            // release
    double doubletalkDb = 20.0;     // the mobile above the delayed far end less this talks itself (0 = off)
    int echoDelayMs = 160;          // until measured
    bool echoDelayGiven = false;    // true: the delay is not measured
};

class MODEMCELLULAR_API EchoSuppressor
{
public:
    using Config = EchoSuppressorConfig;

    explicit EchoSuppressor(double sampleRate = 8000.0, const Config& config = Config());

    // the audio towards the mobile: the reference, not changed
    void processTx(const float* samples, int count);
    // the audio from the mobile: suppressed while the far end talks
    void processRx(float* samples, int count);

    // the measured echo delay, -1 while there is none
    int measuredDelayMs() const { return m_estDelayMs; }
    double measuredCorrelation() const { return m_estCorr; }
    // the current gain of the audio from the mobile
    double rxGain() const { return m_rxGain; }

private:
    void setDelay(int echoDelayMs);
    double estimateDelay(int& lagSlots) const;
    double txReferenceEnergy(int count) const;

    int m_sampleRate;
    int m_rampMs;
    double m_thresholdDb;
    double m_doubletalkDb;
    double m_attenuationLinear;
    int m_hangoverSamples;
    double m_attackAlpha;
    double m_releaseAlpha;

    // the energy of the far end in slots of a fixed duration, for the delayed comparison
    int m_slotSamples;
    std::vector<double> m_txHistory;    // dB per slot
    int m_historyPos;
    double m_slotAcc;
    int m_slotCount;
    int64_t m_slotsWritten;
    int64_t m_rxTotal;
    int64_t m_delaySamples;

    // measurement of the delay: the envelopes (rms per slot)
    bool m_autoDelay;
    std::vector<double> m_estTx;
    std::vector<double> m_estRx;
    double m_estRxAcc;
    int m_estRxCount;
    int64_t m_rxSlots;
    int64_t m_estNext;
    int m_estDelayMs;
    double m_estCorr;

    int m_txHangover;
    int m_nearHangover;
    int64_t m_nearSlot;
    double m_rxGain;
    double m_rxGainTarget;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_ECHOSUPPRESSOR_H_
