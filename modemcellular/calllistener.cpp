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

#include "calllistener.h"

#include <algorithm>
#include <cmath>

namespace modemcellular
{

namespace
{

// SDRangel's polyphase Interpolator as the transmitter of the NMT channels uses it from 8 kHz
const int kPhases = 96;
const double kTapsPerPhase = 24.0 / kPhases;
// a direction that stopped: the other may be this far ahead before it is taken as silence
const size_t kAhead = 1600;

int16_t toPcm(double v)
{
    const double s = std::max(-32767.0, std::min(32767.0, v * 32767.0));
    return static_cast<int16_t>(std::lround(s));
}

} // namespace

CallListener::CallListener(int outputRate) :
    m_outputRate(outputRate),
    m_mode(Mode::Off),
    m_gain(kReferenceLevel),
    m_step(static_cast<Real>(kCallRate / outputRate)),
    m_remain(0.0f)
{
    reset();
}

void CallListener::reset()
{
    m_interpolator.create(kPhases, kCallRate, 3400.0, kTapsPerPhase);
    m_remain = 0.0f;
    m_from.clear();
    m_to.clear();
}

void CallListener::setMode(Mode mode)
{
    if (mode != m_mode) {
        reset();
    }
    m_mode = mode;
}

void CallListener::setVolumeDb(double db)
{
    m_gain = kReferenceLevel * std::pow(10.0, std::min(db, 0.0) / 20.0);
}

void CallListener::fromPhone(const float* samples, int count)
{
    if (m_mode != Mode::Off) {
        m_from.insert(m_from.end(), samples, samples + count);
    }
}

void CallListener::toPhone(const float* samples, int count)
{
    if (m_mode != Mode::Off) {
        m_to.insert(m_to.end(), samples, samples + count);
    }
}

size_t CallListener::take(std::vector<int16_t>& out)
{
    if (m_mode == Mode::Off) {
        return 0;
    }
    // a direction that stopped is silence once the other is far enough ahead
    if (m_from.size() > m_to.size() + kAhead) {
        m_to.resize(m_from.size(), 0.0f);
    } else if (m_to.size() > m_from.size() + kAhead) {
        m_from.resize(m_to.size(), 0.0f);
    }
    size_t frames = 0;

    while (true)
    {
        Complex in(0.0f, 0.0f);
        if (m_remain >= 1.0f)
        {
            // the interpolator takes the next call sample: the phone on the real part, the far end
            // on the imaginary part
            if (m_from.empty() || m_to.empty()) {
                break;
            }
            const float a = std::isfinite(m_from.front()) ? m_from.front() : 0.0f;
            const float b = std::isfinite(m_to.front()) ? m_to.front() : 0.0f;
            m_from.pop_front();
            m_to.pop_front();
            in = Complex(a, b);
        }
        Complex v;
        m_interpolator.interpolate(&m_remain, in, &v);
        m_remain += m_step;

        double left = v.real(), right = v.imag();
        if (m_mode == Mode::FromPhone) {
            right = left;
        } else if (m_mode == Mode::ToPhone) {
            left = right;
        }
        out.push_back(toPcm(left * m_gain));
        out.push_back(toPcm(right * m_gain));
        frames++;
    }
    return frames;
}

} // namespace modemcellular
