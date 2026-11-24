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

#include "echosuppressor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace modemcellular
{

namespace
{

// osmocom-analog measures the 16 bit samples of the call, the speech level (1.0) at
// SPEECH_LEVEL * 0.7079 of full scale: its levels in dB are relative to that
const double kLevelScale = 0.1585 * 0.7079;

// the longest echo delay (the round trip of a usable set-up: 50 to 250 ms)
const int kMaxEchoDelayMs = 300;
// the energy of the far end is kept in slots of this duration
const int kEnergySlotMs = 4;
// the longest time the far end may be ahead of the mobile or behind, because of the block sizes
const int kMaxBlockSkewMs = 250;
// measurement of the delay: the envelopes over the last 2 s, for lags 0 to 300 ms, every second;
// the lag of the highest correlation, at least 0.7, while the far end talked (-45 dB)
const int kEstSlots = 1024;
const int kEstWindowMs = 2000;
const int kEstIntervalMs = 1000;
const double kEstMinCorr = 0.7;
const double kEstMinTxDb = -45.0;

double energyDb(double sum, int count)
{
    if (count <= 0) {
        return -100.0;
    }
    const double rms = std::sqrt(sum / count);
    return rms < 1e-10 ? -100.0 : 20.0 * std::log10(rms);
}

} // namespace

EchoSuppressor::EchoSuppressor(double sampleRate, const Config& config) :
    m_sampleRate(static_cast<int>(sampleRate)),
    m_rampMs(config.rampMs),
    m_thresholdDb(config.thresholdDb),
    m_doubletalkDb(config.doubletalkDb),
    m_attenuationLinear(std::pow(10.0, -config.attenuationDb / 20.0)),
    m_hangoverSamples(config.hangoverMs * m_sampleRate / 1000),
    m_historyPos(0),
    m_slotAcc(0.0),
    m_slotCount(0),
    m_slotsWritten(0),
    m_rxTotal(0),
    m_delaySamples(0),
    m_autoDelay(!config.echoDelayGiven),
    m_estTx(kEstSlots, 0.0),
    m_estRx(kEstSlots, 0.0),
    m_estRxAcc(0.0),
    m_estRxCount(0),
    m_rxSlots(0),
    m_estNext(kEstIntervalMs / kEnergySlotMs),
    m_estDelayMs(-1),
    m_estCorr(0.0),
    m_txHangover(0),
    m_nearHangover(0),
    m_nearSlot(-1000000),
    m_rxGain(1.0),
    m_rxGainTarget(1.0)
{
    m_slotSamples = std::max(1, kEnergySlotMs * m_sampleRate / 1000);
    m_txHistory.assign(static_cast<size_t>((kMaxEchoDelayMs + kMaxBlockSkewMs) / kEnergySlotMs + 2), -100.0);

    const int attackSamples = config.rampMs * m_sampleRate / 1000;
    m_attackAlpha = attackSamples > 0 ? 1.0 / attackSamples : 1.0;
    const int releaseMs = config.releaseMs > 0 ? config.releaseMs : 300;
    const int releaseSamples = releaseMs * m_sampleRate / 1000;
    m_releaseAlpha = releaseSamples > 0 ? 1.0 / releaseSamples : 0.01;

    setDelay(std::max(0, std::min(kMaxEchoDelayMs, config.echoDelayMs)));
}

// The suppression starts early, so that the gain has reached the full attenuation when the echo
// arrives: the gain falls exponentially with the attack time as time constant.
void EchoSuppressor::setDelay(int echoDelayMs)
{
    int64_t delay = static_cast<int64_t>(echoDelayMs) * m_sampleRate / 1000;
    const int attackSamples = m_rampMs * m_sampleRate / 1000;

    if (attackSamples > 0 && m_attenuationLinear > 0.0 && m_attenuationLinear < 1.0) {
        delay -= static_cast<int64_t>(attackSamples * std::log(1.0 / m_attenuationLinear));
    }
    m_delaySamples = std::max<int64_t>(0, delay);
}

// The lag (in slots) of the best match of the envelopes; the correlation, -1 if the far end had
// no speech.
double EchoSuppressor::estimateDelay(int& lagSlots) const
{
    const int window = kEstWindowMs / kEnergySlotMs;
    const int maxLag = kMaxEchoDelayMs / kEnergySlotMs;
    const int64_t last = m_rxSlots - 1;
    const int64_t first = m_rxSlots - window;
    double best = -1.0;
    double txPeak = 0.0;

    if (first - maxLag < 0 || m_slotsWritten < m_rxSlots - maxLag) {
        return -1.0;
    }
    for (int k = 0; k < window + maxLag; k++)
    {
        const int64_t s = last - k;
        if (s >= 0 && s < m_slotsWritten) {
            txPeak = std::max(txPeak, m_estTx[static_cast<size_t>(s % kEstSlots)]);
        }
    }
    if (20.0 * std::log10(txPeak + 1e-12) < kEstMinTxDb) {
        return -1.0;
    }
    for (int lag = 0; lag <= maxLag; lag++)
    {
        double sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;
        int n = 0;

        for (int64_t s = first; s <= last; s++)
        {
            const int64_t t = s - lag;
            if (t < 0 || t >= m_slotsWritten) {
                continue;
            }
            const double x = m_estTx[static_cast<size_t>(t % kEstSlots)];
            const double y = m_estRx[static_cast<size_t>(s % kEstSlots)];
            sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y;
            n++;
        }
        if (n < window / 2) {
            continue;
        }
        const double cov = sxy - sx * sy / n;
        const double var = (sxx - sx * sx / n) * (syy - sy * sy / n);
        if (var <= 0.0) {
            continue;
        }
        if (cov / std::sqrt(var) > best)
        {
            best = cov / std::sqrt(var);
            lagSlots = lag;
        }
    }
    return best;
}

void EchoSuppressor::processTx(const float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        const double s = samples[i] * kLevelScale;

        m_slotAcc += s * s;
        if (++m_slotCount == m_slotSamples)
        {
            m_txHistory[static_cast<size_t>(m_historyPos)] = energyDb(m_slotAcc, m_slotCount);
            m_historyPos = (m_historyPos + 1) % static_cast<int>(m_txHistory.size());
            m_estTx[static_cast<size_t>(m_slotsWritten % kEstSlots)] = std::sqrt(m_slotAcc / m_slotCount);
            m_slotsWritten++;
            m_slotAcc = 0.0;
            m_slotCount = 0;
        }
    }
}

// The highest energy of the far end in the slots this block of the mobile's audio is compared
// with: the time span of the block, the echo delay ago. While the delay is being measured, any
// delay up to the longest is possible, so all of them are taken.
double EchoSuppressor::txReferenceEnergy(int count) const
{
    const int64_t history = static_cast<int64_t>(m_txHistory.size());
    int64_t first;
    int64_t last;

    if (m_autoDelay && m_estDelayMs < 0)
    {
        const int64_t d = static_cast<int64_t>(kMaxEchoDelayMs) * m_sampleRate / 1000;
        first = m_rxTotal > d ? (m_rxTotal - d) / m_slotSamples : 0;
        last = (m_rxTotal + count - 1) / m_slotSamples;
    }
    else
    {
        if (m_rxTotal < m_delaySamples) {
            return -100.0;
        }
        first = (m_rxTotal - m_delaySamples) / m_slotSamples;
        last = (m_rxTotal + count - 1 - m_delaySamples) / m_slotSamples;
    }
    // the far end may not have got that far yet, or too far to remember it
    last = std::min(last, m_slotsWritten - 1);
    first = std::max(first, m_slotsWritten - history + 1);
    first = std::max<int64_t>(first, 0);

    double energy = -100.0;
    for (int64_t slot = first; slot <= last; slot++) {
        energy = std::max(energy, m_txHistory[static_cast<size_t>(slot % history)]);
    }
    return energy;
}

void EchoSuppressor::processRx(float* samples, int count)
{
    if (count <= 0) {
        return;
    }

    // the energy of the block, and the envelope before the suppression for the delay measurement
    double sum = 0.0;
    for (int i = 0; i < count; i++)
    {
        const double s = samples[i] * kLevelScale;

        sum += s * s;
        m_estRxAcc += s * s;
        if (++m_estRxCount == m_slotSamples)
        {
            m_estRx[static_cast<size_t>(m_rxSlots % kEstSlots)] = std::sqrt(m_estRxAcc / m_estRxCount);
            m_rxSlots++;
            m_estRxAcc = 0.0;
            m_estRxCount = 0;
        }
    }
    const double rxEnergy = energyDb(sum, count);

    if (m_rxSlots >= m_estNext && m_rxSlots - m_nearSlot > kEstWindowMs / kEnergySlotMs)
    {
        int lag = 0;

        m_estNext = m_rxSlots + kEstIntervalMs / kEnergySlotMs;
        const double corr = estimateDelay(lag);
        const int ms = lag * kEnergySlotMs;
        if (corr >= kEstMinCorr)
        {
            if (m_autoDelay && (m_estDelayMs < 0 || std::abs(ms - m_estDelayMs) > kEnergySlotMs)) {
                setDelay(ms);
            }
            m_estDelayMs = ms;
            m_estCorr = corr;
        }
    }

    // the far end, the echo delay ago
    const double txRef = txReferenceEnergy(count);
    m_rxTotal += count;
    if (txRef > m_thresholdDb) {
        m_txHangover = m_hangoverSamples;
    }
    m_txHangover = std::max(0, m_txHangover - count);

    // Double talk: the echo of the phone comes back at least 28 dB below the far end (on air,
    // NMT and AMPS phones on the table); louder than the far end less doubletalkDb is the mobile
    // talking. It passes (break-in) for the hangover, and the delay measurement pauses.
    if (m_doubletalkDb > 0.0 && rxEnergy > -60.0 && rxEnergy > txRef - m_doubletalkDb)
    {
        m_nearHangover = m_hangoverSamples;
        m_nearSlot = m_rxSlots;
    }
    m_nearHangover = std::max(0, m_nearHangover - count);

    if (m_nearHangover > 0) {
        m_rxGainTarget = 1.0;
    } else if (m_txHangover > 0) {
        m_rxGainTarget = m_attenuationLinear;
    } else {
        m_rxGainTarget = 1.0;
    }

    // the gain moves to the target per sample: fast attack, slow release; the mobile breaking in
    // opens as fast as the suppression closes, so its first syllable is not lost
    const double alpha = m_rxGainTarget < m_rxGain ? m_attackAlpha
        : (m_nearHangover > 0 ? m_attackAlpha : m_releaseAlpha);
    for (int i = 0; i < count; i++)
    {
        m_rxGain += alpha * (m_rxGainTarget - m_rxGain);
        samples[i] = static_cast<float>(samples[i] * m_rxGain);
    }
}

} // namespace modemcellular
