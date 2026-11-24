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

#include "ampsfsk.h"

#include <algorithm>
#include <cmath>

namespace modemamps
{

namespace
{

const double kPi = 3.14159265358979323846;
// osmocom: the best quality a bit has (the difference of the quarter bits around the zero crossing
// relative to twice the deviation)
const double kBestQuality = 0.68;
// osmocom: bits the clock runs after the last dotting before it stops without a frame
const int kDottingLife = 12;
// zero crossings of the dotting checked (osmocom: 7 bits between 8 crossings)
const int kDottingCrossings = 8;
// points of a mean between the samples
const int kMeanPoints = 8;

} // namespace

ManchesterModulator::ManchesterModulator(double sampleRate, double bitRate, double deviation) :
    m_deviation(deviation),
    m_halvesPerSample(2.0 * bitRate / sampleRate)
{
    reset();
}

void ManchesterModulator::reset()
{
    m_halfPos = 0.0;
    m_half = -1;
    // osmocom starts as after a 0: the frequency is low
    m_last = -m_deviation;
    m_from = m_last;
    m_to = m_last;
    m_bit = 0;
}

int ManchesterModulator::render(float* samples, int length, bool add)
{
    for (int i = 0; i < length; i++)
    {
        if (m_half < 0)
        {
            const int bit = m_source ? m_source() : -1;

            if (bit < 0)
            {
                m_halfPos = 0.0;
                return i;
            }
            m_bit = bit;
            m_half = 0;
            m_from = m_last;
            m_to = (m_bit != 0) ? -m_deviation : m_deviation;  // a 1 starts low, a 0 high
        }

        const double value = (m_from + m_to) / 2.0 + (m_from - m_to) / 2.0 * std::cos(kPi * m_halfPos);
        samples[i] = add ? samples[i] + static_cast<float>(value) : static_cast<float>(value);

        m_halfPos += m_halvesPerSample;
        while (m_halfPos >= 1.0 && m_half >= 0)
        {
            m_halfPos -= 1.0;
            m_last = m_to;
            if (m_half == 0)
            {
                m_half = 1;
                m_from = m_last;
                m_to = -m_to;
            }
            else
            {
                m_half = -1;
            }
        }
    }
    return length;
}

ManchesterDemodulator::ManchesterDemodulator(double sampleRate, double bitRate, double deviation) :
    m_deviation(deviation),
    m_samplesPerBit(sampleRate / bitRate),
    m_bitsPerSample(bitRate / sampleRate),
    m_window(static_cast<size_t>(std::ceil(sampleRate / bitRate)) + 4, 0.0f),
    m_pos(0),
    m_count(0),
    m_last(0.0f),
    m_elapsed(0.0),
    m_dottingPos(0),
    m_dottingAverage(0.0),
    m_dottingLife(0),
    m_bitCount(0.0),
    m_state(StateNone)
{
    std::fill(std::begin(m_dotting), std::end(m_dotting), 0.0);
}

void ManchesterDemodulator::startFrame()
{
    m_state = StateFrame;
}

void ManchesterDemodulator::endFrame()
{
    m_state = StateNone;
}

void ManchesterDemodulator::process(const float* samples, int length)
{
    for (int i = 0; i < length; i++)
    {
        const float sample = samples[i];

        m_window[m_pos] = sample;
        m_pos = (m_pos + 1) % static_cast<int>(m_window.size());
        m_count = std::min(m_count + 1, static_cast<int>(m_window.size()));

        if (m_state != StateFrame && ((m_last <= 0.0f && sample > 0.0f) || (m_last > 0.0f && sample <= 0.0f)))
        {
            // where between the last sample and this one the frequency crossed zero
            const double f = static_cast<double>(m_last) / (static_cast<double>(m_last) - static_cast<double>(sample));
            const double since = (1.0 - f) * m_bitsPerSample;

            crossing(m_elapsed + f * m_bitsPerSample, since);
            m_elapsed = since;
        }
        else
        {
            m_elapsed += m_bitsPerSample;
        }
        m_last = sample;

        if (m_state != StateNone)
        {
            m_bitCount += m_bitsPerSample;
            if (m_bitCount >= 1.0)
            {
                m_bitCount -= 1.0;
                bit(m_bitCount * m_samplesPerBit);
            }
        }
    }
}

void ManchesterDemodulator::crossing(double elapsed, double sinceCrossing)
{
    m_dotting[m_dottingPos++] = elapsed;

    // the last crossings have to be one bit apart, each within half a bit
    unsigned char pos = m_dottingPos;
    double average = 0.0;
    double sum = 0.0;

    for (int i = 1; i < kDottingCrossings; i++)
    {
        sum += m_dotting[--pos];
        const double offset = sum - static_cast<double>(i);

        if (offset >= 0.5 || offset <= -0.5) {
            return;
        }
        average += offset;
    }
    average /= static_cast<double>(kDottingCrossings);

    m_dottingLife = kDottingLife;
    // a better dotting than the one found (that was noise) sets the clock again
    if (m_state == StateNone || std::fabs(average) < m_dottingAverage)
    {
        m_state = StateDotting;
        m_dottingAverage = std::fabs(average);
        // the bit is decided half a bit after its crossing; this sample is already past it
        m_bitCount = 0.5 + average + sinceCrossing - m_bitsPerSample;
    }
}

double ManchesterDemodulator::at(double back) const
{
    const int size = static_cast<int>(m_window.size());
    const int n = static_cast<int>(back);
    const double f = back - n;

    if (n + 1 >= m_count) {
        return 0.0;
    }
    const double s0 = m_window[(m_pos - 1 - n + 2 * size) % size];
    const double s1 = m_window[(m_pos - 2 - n + 2 * size) % size];
    return s0 + (s1 - s0) * f;
}

void ManchesterDemodulator::bit(double back)
{
    // the quarter bit after the crossing (half a bit back) and the quarter bit before it
    const double quarter = m_samplesPerBit / 4.0;
    double second = 0.0;
    double first = 0.0;
    double max = -1e30;
    double min = 1e30;

    for (int j = 0; j < kMeanPoints; j++)
    {
        const double s = at(back + quarter * (1.0 + (j + 0.5) / kMeanPoints));
        const double f = at(back + quarter * (2.0 + (j + 0.5) / kMeanPoints));

        second += s;
        first += f;
        max = std::max(max, std::max(s, f));
        min = std::min(min, std::min(s, f));
    }
    second /= kMeanPoints;
    first /= kMeanPoints;

    const int value = (second > first) ? 1 : 0;
    const double level = (max - min) / m_deviation / 2.0;
    const double quality = std::fabs(second - first) / m_deviation / 2.0 / kBestQuality;

    if (m_sink) {
        m_sink(value, quality, level);
    }
    // no frame started on this bit: the dotting lives on for a few bits
    if (m_state == StateDotting && --m_dottingLife == 0) {
        m_state = StateNone;
    }
}

} // namespace modemamps
