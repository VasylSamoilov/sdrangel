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

#include "consoleleg.h"

#include <cmath>

#include "testcaller.h"

namespace modemcellular
{

namespace
{

const int kToneSamples = kAudioSampleRate / 10;         // 100 ms
const int kPauseSamples = kAudioSampleRate * 6 / 100;   // 60 ms
const double kToneLevel = 0.25;                         // each of the two tones, -12 dB of the reference

// the two frequencies of a key (ITU-T Q.23), false for a key that is none
bool dtmf(char key, double& low, double& high)
{
    static const char keys[4][5] = {"123A", "456B", "789C", "*0#D"};
    static const double lows[4] = {697.0, 770.0, 852.0, 941.0};
    static const double highs[4] = {1209.0, 1336.0, 1477.0, 1633.0};

    for (int r = 0; r < 4; r++)
    {
        for (int c = 0; c < 4; c++)
        {
            if (keys[r][c] == key)
            {
                low = lows[r];
                high = highs[c];
                return true;
            }
        }
    }
    return false;
}

} // namespace

ConsoleLeg::ConsoleLeg(CellularSystem& system, const std::string& number) :
    m_system(system),
    m_number(number),
    m_state(State::Idle),
    m_ref(0),
    m_keyPos(0)
{
    m_system.registerSubscriber(m_number, *this);
}

ConsoleLeg::~ConsoleLeg()
{
    hangUp();
    // no callback to this leg runs or comes after this; the number goes with it
    m_system.detach(*this);
}

bool ConsoleLeg::dial(const std::string& number, Cause* cause)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_state != State::Idle)
        {
            if (cause) {
                *cause = Cause::Busy;
            }
            return false;
        }
        m_state = State::Calling;
        m_peer = number;
    }

    // the router may call back into this leg before setup returns
    const CallRouter::SetupResult result = m_system.setup(*this, m_number, number);
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!result.ok())
    {
        m_state = State::Idle;
        if (cause) {
            *cause = result.cause;
        }
        return false;
    }
    if (m_state != State::Idle) {
        m_ref = result.callref;
    }
    return true;
}

bool ConsoleLeg::answer()
{
    CallRef ref;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::Incoming) {
            return false;
        }
        m_state = State::Connected;
        ref = m_ref;
        event("call from " + m_peer + ": answered");
    }
    m_system.answer(ref);
    return true;
}

void ConsoleLeg::hangUp()
{
    CallRef ref;
    State was;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ref = m_ref;
        was = m_state;
        m_ref = 0;
        m_state = State::Idle;
        m_keys.clear();
        if (was != State::Idle) {
            event((was == State::Incoming ? "call from " + m_peer + ": refused" : "call with " + m_peer + ": hung up"));
        }
    }
    if (ref != 0) {
        m_system.release(ref, was == State::Incoming ? Cause::Busy : Cause::Normal);
    }
}

bool ConsoleLeg::digit(char key)
{
    double low, high;
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_state != State::Connected || !dtmf(key, low, high)) {
        return false;
    }
    m_keys.push_back(key);
    event(std::string("touch tone ") + key + " sent");
    return true;
}

void ConsoleLeg::addTones(float* samples, int count)
{
    for (int i = 0; i < count && !m_keys.empty(); i++)
    {
        double low, high;
        if (m_keyPos < kToneSamples && dtmf(m_keys.front(), low, high))
        {
            const double t = static_cast<double>(m_keyPos) / kAudioSampleRate;
            samples[i] += static_cast<float>(kToneLevel * (std::sin(2.0 * M_PI * low * t) + std::sin(2.0 * M_PI * high * t)));
        }
        if (++m_keyPos >= kToneSamples + kPauseSamples)
        {
            m_keyPos = 0;
            m_keys.pop_front();
        }
    }
}

void ConsoleLeg::process(int count)
{
    std::deque<std::string> events;
    CallRef ref = 0;        // connected: both ways
    CallRef heard = 0;      // any call: what the far end plays (ringback, tones) to the speaker
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        events.swap(m_events);
        heard = m_ref;
        if (m_state == State::Connected) {
            ref = m_ref;
        }
    }
    for (const std::string& text : events)
    {
        if (m_handler) {
            m_handler(text);
        }
    }
    if (count <= 0 || heard == 0) {
        return;         // no call: nothing to the speaker either
    }
    m_in.assign(static_cast<size_t>(count), 0.0f);
    m_out.assign(static_cast<size_t>(count), 0.0f);
    // the far end (also its ringback and tones before the answer) to the speaker
    if (heard != 0) {
        m_system.audioToMobile(heard, m_in.data(), count);
    }
    if (m_speaker) {
        m_speaker(m_in.data(), count);
    }
    if (ref == 0) {
        return;
    }
    // the microphone and the touch tones to the far end
    if (m_microphone) {
        m_microphone(m_out.data(), count);
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        addTones(m_out.data(), count);
    }
    m_system.audioFromMobile(ref, m_out.data(), count);
}

ConsoleLeg::State ConsoleLeg::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

std::string ConsoleLeg::peer() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_peer;
}

void ConsoleLeg::event(const std::string& text)
{
    m_events.push_back(text);
}

void ConsoleLeg::setup(CallRef callref, const std::string& callerId, NumberType, const std::string&)
{
    bool now = false;
    bool taken = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::Idle)
        {
            event("call from " + callerId + ": refused, the console is busy");
        }
        else
        {
            m_state = m_autoAnswer ? State::Connected : State::Incoming;
            m_ref = callref;
            m_peer = callerId;
            now = m_autoAnswer;
            taken = true;
            event("call from " + callerId + (now ? ": answered at once" : ": ringing"));
        }
    }
    if (!taken)
    {
        m_system.release(callref, Cause::Busy);
        return;
    }
    m_system.alerting(callref);
    if (now) {
        m_system.answer(callref);
    }
}

void ConsoleLeg::proceeding(CallRef)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::Calling) {
        event("call to " + m_peer + ": paging");
    }
}

void ConsoleLeg::alerting(CallRef)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::Calling)
    {
        m_state = State::Ringback;
        event("call to " + m_peer + ": ringing");
    }
}

void ConsoleLeg::answer(CallRef)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::Calling || m_state == State::Ringback)
    {
        m_state = State::Connected;
        event("call to " + m_peer + ": answered");
    }
}

void ConsoleLeg::release(CallRef, Cause cause)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != State::Idle)
    {
        event("call with " + m_peer + ": ended (" + TestCaller::causeName(cause) + ")");
        m_state = State::Idle;
        m_ref = 0;
        m_keys.clear();
    }
}

} // namespace modemcellular
