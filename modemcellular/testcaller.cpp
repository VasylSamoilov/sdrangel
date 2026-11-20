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

#include "testcaller.h"

namespace modemcellular
{

TestCaller::TestCaller(CellularSystem& system) :
    m_system(system),
    m_state(State::Idle),
    m_ref(0)
{
}

TestCaller::~TestCaller()
{
    hangUp();
    // no callback to this leg runs or comes after this
    m_system.detach(*this);
}

const char* TestCaller::causeName(Cause cause)
{
    switch (cause)
    {
    case Cause::Normal: return "normal";
    case Cause::Busy: return "busy";
    case Cause::NoChannel: return "no channel";
    case Cause::OutOfOrder: return "out of order";
    case Cause::InvalidNumber: return "invalid number";
    case Cause::TempFail: return "temporary failure";
    }
    return "?";
}

bool TestCaller::call(const std::string& number, const std::string& callerId, MobileLeg* called,
                      std::unique_ptr<AudioEndpoint> endpoint, Cause* cause)
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
        m_number = number;
        m_endpoint = std::move(endpoint);
    }

    // the router may call back into this leg before setup returns
    const CallRouter::SetupResult result = m_system.setup(*this, callerId, number, called);
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!result.ok())
    {
        m_state = State::Idle;
        m_endpoint.reset();
        if (cause) {
            *cause = result.cause;
        }
        return false;
    }
    // a release that came first leaves the state idle
    if (m_state != State::Idle) {
        m_ref = result.callref;
    }
    return true;
}

void TestCaller::hangUp()
{
    CallRef ref;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ref = m_ref;
        m_ref = 0;
        m_state = State::Idle;
        m_endpoint.reset();
    }
    if (ref != 0) {
        m_system.release(ref, Cause::Normal);
    }
}

void TestCaller::process(int count)
{
    std::deque<std::string> events;
    CallRef ref = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        events.swap(m_events);
        if (m_state == State::Connected && m_endpoint) {
            ref = m_ref;
        }
    }
    for (const std::string& text : events)
    {
        if (m_handler) {
            m_handler(text);
        }
    }
    if (ref == 0 || count <= 0) {
        return;
    }

    // what the mobile says goes to the endpoint, what the endpoint plays goes to the mobile
    m_fromMobile.assign(static_cast<size_t>(count), 0.0f);
    m_toMobile.assign(static_cast<size_t>(count), 0.0f);
    m_system.audioToMobile(ref, m_fromMobile.data(), count);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_endpoint) {
            return;
        }
        m_endpoint->fromMobile(m_fromMobile.data(), count);
        m_endpoint->toMobile(m_toMobile.data(), count);
    }
    m_system.audioFromMobile(ref, m_toMobile.data(), count);
}

TestCaller::State TestCaller::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

std::string TestCaller::number() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_number;
}

void TestCaller::event(const std::string& text)
{
    m_events.push_back(text);
}

void TestCaller::setup(CallRef callref, const std::string&, NumberType, const std::string&)
{
    // the test caller takes no calls
    m_system.release(callref, Cause::Busy);
}

void TestCaller::proceeding(CallRef)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != State::Idle) {
        event("call to " + m_number + ": paging");
    }
}

void TestCaller::alerting(CallRef)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::Calling)
    {
        m_state = State::Ringing;
        event("call to " + m_number + ": ringing");
    }
}

void TestCaller::answer(CallRef)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == State::Calling || m_state == State::Ringing)
    {
        m_state = State::Connected;
        event("call to " + m_number + ": answered");
    }
}

void TestCaller::release(CallRef, Cause cause)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state != State::Idle)
    {
        event("call to " + m_number + ": ended (" + causeName(cause) + ")");
        m_state = State::Idle;
        m_ref = 0;
        m_endpoint.reset();
    }
}

} // namespace modemcellular
