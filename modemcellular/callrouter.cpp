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

#include <algorithm>
#include <deque>
#include <utility>

#include "callrouter.h"
#include "echoendpoint.h"
#include "musicendpoint.h"

namespace modemcellular
{

namespace
{

// Audio waiting for a mobile in a mobile to mobile call is limited to one second.
constexpr size_t kMaxQueuedSamples = kAudioSampleRate;

// A tone that announces the end of a call plays for this long, then the call is released.
constexpr double kDefaultToneTimeout = 30.0;

void pushAudio(std::deque<float>& queue, const float* samples, int count)
{
    for (int i = 0; i < count; i++) {
        queue.push_back(samples[i]);
    }

    while (queue.size() > kMaxQueuedSamples) {
        queue.pop_front();
    }
}

void popAudio(std::deque<float>& queue, float* samples, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (queue.empty()) {
            samples[i] = 0.0f;
        } else {
            samples[i] = queue.front();
            queue.pop_front();
        }
    }
}

Tone toneForCause(Cause cause)
{
    switch (cause)
    {
    case Cause::Normal:
        return Tone::Off;
    case Cause::Busy:
        return Tone::Busy;
    case Cause::NoChannel:
    case Cause::OutOfOrder:
    case Cause::InvalidNumber:
    case Cause::TempFail:
        return Tone::Congestion;
    }

    return Tone::Congestion;
}

} // namespace

// One call. Leg A placed it. Leg B is the called mobile; it is null if a fallback endpoint
// is the far end, or after the called mobile has left the call.
struct CallRouter::Call
{
    explicit Call(ToneSet toneSet) : tone(toneSet) {}

    CallRef refA = 0;
    CallRef refB = 0;
    MobileLeg* legA = nullptr;
    MobileLeg* legB = nullptr;
    std::string numberA;
    std::string numberB;
    bool proceeded = false;     // leg A has been told "proceeding"
    bool connected = false;
    // Events from the far end that happened before leg A was told "proceeding". They are
    // passed on right after it, in this order.
    bool alertingPending = false;
    bool answerPending = false;
    bool endPending = false;
    Cause endCause = Cause::Normal;
    std::unique_ptr<AudioEndpoint> endpoint;
    ToneGenerator tone;         // plays to leg A
    std::deque<float> toA;      // mobile to mobile audio waiting for leg A
    std::deque<float> toB;      // mobile to mobile audio waiting for leg B
    std::unique_ptr<Timer> proceedingTimer;
    std::unique_ptr<Timer> answerTimer;
    std::unique_ptr<Timer> toneTimer;
};

CallRouter::CallRouter(TimerService& timers, ToneSet toneSet) :
    m_timers(timers),
    m_fallback(Fallback::Music),
    m_answerDelay(0.0),
    m_toneTimeout(kDefaultToneTimeout),
    m_toneSet(toneSet),
    m_nextRef(1)
{
}

CallRouter::~CallRouter() = default;

void CallRouter::setFallback(Fallback fallback)
{
    m_fallback = fallback;
    m_factory = nullptr;
}

void CallRouter::setFallbackFactory(EndpointFactory factory)
{
    m_factory = std::move(factory);
}

std::unique_ptr<AudioEndpoint> CallRouter::makeFallbackEndpoint()
{
    if (m_factory) {
        return m_factory();
    }

    if (m_fallback == Fallback::Echo) {
        return std::unique_ptr<AudioEndpoint>(new EchoEndpoint());
    }

    return std::unique_ptr<AudioEndpoint>(new MusicEndpoint());
}

void CallRouter::deliver(const Notification& n)
{
    switch (n.type)
    {
    case Notification::Type::Setup:
        n.leg->setup(n.callref, n.callerId, n.callerType, n.dialed);
        break;
    case Notification::Type::Proceeding:
        n.leg->proceeding(n.callref);
        break;
    case Notification::Type::Alerting:
        n.leg->alerting(n.callref);
        break;
    case Notification::Type::Answer:
        n.leg->answer(n.callref);
        break;
    case Notification::Type::Release:
        n.leg->release(n.callref, n.cause);
        break;
    }
}

void CallRouter::emit(Notification&& notification)
{
    if (m_handler) {
        m_handler(std::move(notification));
    } else {
        deliver(notification);
    }
}

void CallRouter::notify(Notification::Type type, MobileLeg* leg, CallRef callref, Cause cause)
{
    Notification n;
    n.type = type;
    n.leg = leg;
    n.callref = callref;
    n.cause = cause;
    emit(std::move(n));
}

bool CallRouter::registerSubscriber(const std::string& number, MobileLeg& leg)
{
    if (number.empty()) {
        return false;
    }

    m_subscribers[number] = &leg;
    return true;
}

bool CallRouter::unregisterSubscriber(const std::string& number, MobileLeg& leg)
{
    auto it = m_subscribers.find(number);

    if ((it != m_subscribers.end()) && (it->second == &leg))
    {
        m_subscribers.erase(it);
        return true;
    }

    return false;
}

bool CallRouter::isRegistered(const std::string& number) const
{
    return m_subscribers.find(number) != m_subscribers.end();
}

std::vector<std::string> CallRouter::detach(MobileLeg& leg)
{
    std::vector<std::string> removed;

    for (auto it = m_subscribers.begin(); it != m_subscribers.end();)
    {
        if (it->second == &leg)
        {
            removed.push_back(it->first);
            it = m_subscribers.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (;;)
    {
        auto it = std::find_if(m_calls.begin(), m_calls.end(),
            [&leg](const std::unique_ptr<Call>& c) { return (c->legA == &leg) || (c->legB == &leg); });

        if (it == m_calls.end()) {
            break;
        }

        const CallRef refA = (*it)->refA;

        if ((*it)->legA == &leg)
        {
            std::unique_ptr<Call> call = takeCall(refA);

            if (call->legB) {
                notify(Notification::Type::Release, call->legB, call->refB, Cause::OutOfOrder);
            }
        }
        else
        {
            farEnd(refA, Cause::OutOfOrder);    // clears legB, so the loop ends
        }
    }

    return removed;
}

CallRef CallRouter::allocateRef()
{
    CallRef ref = m_nextRef++;

    if (m_nextRef == 0) {
        m_nextRef = 1;
    }

    return ref;
}

CallRouter::Call* CallRouter::findCall(CallRef callref) const
{
    if (callref == 0) {
        return nullptr;
    }

    for (const auto& c : m_calls)
    {
        if ((c->refA == callref) || (c->refB == callref)) {
            return c.get();
        }
    }

    return nullptr;
}

std::unique_ptr<CallRouter::Call> CallRouter::takeCall(CallRef callref)
{
    for (auto it = m_calls.begin(); it != m_calls.end(); ++it)
    {
        if (((*it)->refA == callref) || ((*it)->refB == callref))
        {
            std::unique_ptr<Call> call = std::move(*it);
            m_calls.erase(it);
            forgetBusy(call->numberA);
            forgetBusy(call->numberB);
            return call;
        }
    }

    return nullptr;
}

void CallRouter::forgetBusy(const std::string& number)
{
    if (number.empty()) {
        return;
    }

    auto it = m_busy.find(number);

    if (it != m_busy.end()) {
        m_busy.erase(it);
    }
}

CallRouter::SetupResult CallRouter::setup(MobileLeg& origin, const std::string& callerId, const std::string& dialed)
{
    if (dialed.empty()) {
        return SetupResult{0, Cause::InvalidNumber};
    }

    std::unique_ptr<Call> call(new Call(m_toneSet));
    call->legA = &origin;
    call->numberA = callerId;

    MobileLeg* target = nullptr;
    bool busy = false;
    auto sub = m_subscribers.find(dialed);

    if (sub != m_subscribers.end())
    {
        if ((sub->second == &origin) || (m_busy.count(dialed) > 0)) {
            busy = true;
        } else {
            target = sub->second;
        }
    }
    else
    {
        call->endpoint = makeFallbackEndpoint();

        if (!call->endpoint) {
            return SetupResult{0, Cause::NoChannel};
        }
    }

    call->refA = allocateRef();

    if (target)
    {
        call->refB = allocateRef();
        call->legB = target;
        call->numberB = dialed;
        m_busy.insert(dialed);
    }

    if (busy)
    {
        // The caller is told "proceeding" first, then hears busy tone.
        call->endPending = true;
        call->endCause = Cause::Busy;
    }

    if (!callerId.empty()) {
        m_busy.insert(callerId);
    }

    const CallRef refA = call->refA;
    const CallRef refB = call->refB;

    // The origin leg is told "proceeding" from the next timer run, after it has the call reference.
    call->proceedingTimer.reset(new Timer(m_timers, [this, refA] { onProceeding(refA); }));
    call->proceedingTimer->start(0.0);
    m_calls.push_back(std::move(call));

    if (target)
    {
        Notification n;
        n.type = Notification::Type::Setup;
        n.leg = target;
        n.callref = refB;
        n.callerId = callerId;
        n.callerType = callerId.empty() ? NumberType::NotAvailable : NumberType::Subscriber;
        n.dialed = dialed;
        emit(std::move(n));     // may call back into the router; the call is not touched after this
    }

    return SetupResult{refA, Cause::Normal};
}

void CallRouter::onProceeding(CallRef refA)
{
    Call* call = findCall(refA);

    if (!call) {
        return;
    }

    call->proceeded = true;

    if (call->endpoint)
    {
        call->answerTimer.reset(new Timer(m_timers, [this, refA] { onEndpointAnswer(refA); }));
        call->answerTimer->start(m_answerDelay);
    }

    notify(Notification::Type::Proceeding, call->legA, refA);

    // With direct delivery the leg may have released the call from inside "proceeding",
    // so the call is looked up again before each step.
    call = findCall(refA);

    if (call && call->alertingPending)
    {
        call->alertingPending = false;
        farAlerting(refA);
        call = findCall(refA);
    }

    if (call && call->answerPending)
    {
        call->answerPending = false;
        farAnswer(refA);
        call = findCall(refA);
    }

    if (call && call->endPending)
    {
        call->endPending = false;
        farEnd(refA, call->endCause);
    }
}

void CallRouter::onEndpointAnswer(CallRef refA)
{
    Call* call = findCall(refA);

    if (!call || call->connected) {
        return;
    }

    call->connected = true;
    notify(Notification::Type::Answer, call->legA, refA);
}

void CallRouter::farAlerting(CallRef refA)
{
    Call* call = findCall(refA);

    if (!call || call->connected) {
        return;
    }

    call->tone.setTone(Tone::Ringback);
    notify(Notification::Type::Alerting, call->legA, refA);
}

void CallRouter::farAnswer(CallRef refA)
{
    Call* call = findCall(refA);

    if (!call || call->connected) {
        return;
    }

    call->connected = true;
    call->tone.setTone(Tone::Off);
    notify(Notification::Type::Answer, call->legA, refA);
}

// The called side is gone: the called mobile released the call or its channel vanished,
// or the called mobile was busy from the start.
void CallRouter::farEnd(CallRef refA, Cause cause)
{
    Call* call = findCall(refA);

    if (!call) {
        return;
    }

    call->legB = nullptr;
    call->refB = 0;
    forgetBusy(call->numberB);
    call->numberB.clear();

    if (!call->proceeded)
    {
        call->endPending = true;
        call->endCause = cause;
        return;
    }

    if (call->connected || (cause == Cause::Normal))
    {
        std::unique_ptr<Call> ended = takeCall(refA);
        notify(Notification::Type::Release, ended->legA, refA, cause);
        return;
    }

    call->tone.setTone(toneForCause(cause));
    call->toneTimer.reset(new Timer(m_timers, [this, refA, cause] { onToneTimeout(refA, cause); }));
    call->toneTimer->start(m_toneTimeout);
}

void CallRouter::onToneTimeout(CallRef refA, Cause cause)
{
    std::unique_ptr<Call> ended = takeCall(refA);

    if (ended) {
        notify(Notification::Type::Release, ended->legA, refA, cause);
    }
}

void CallRouter::alerting(CallRef callref)
{
    Call* call = findCall(callref);

    if (!call || !call->legB || (call->refB != callref)) {
        return;
    }

    if (!call->proceeded) {
        call->alertingPending = true;
    } else {
        farAlerting(call->refA);
    }
}

void CallRouter::answer(CallRef callref)
{
    Call* call = findCall(callref);

    if (!call || !call->legB || (call->refB != callref) || call->connected) {
        return;
    }

    if (!call->proceeded) {
        call->answerPending = true;
    } else {
        farAnswer(call->refA);
    }
}

void CallRouter::release(CallRef callref, Cause cause)
{
    Call* call = findCall(callref);

    if (!call) {
        return;
    }

    if (call->refA == callref)
    {
        // The calling mobile hangs up. The called mobile, if still part of the call, is released.
        std::unique_ptr<Call> ended = takeCall(callref);

        if (ended->legB) {
            notify(Notification::Type::Release, ended->legB, ended->refB, cause);
        }
    }
    else
    {
        farEnd(call->refA, cause);
    }
}

void CallRouter::audioFromMobile(CallRef callref, const float* samples, int count)
{
    Call* call = findCall(callref);

    if (!call || !call->connected) {
        return;
    }

    if (call->endpoint) {
        call->endpoint->fromMobile(samples, count);
    } else if (call->refA == callref) {
        pushAudio(call->toB, samples, count);
    } else {
        pushAudio(call->toA, samples, count);
    }
}

void CallRouter::audioToMobile(CallRef callref, float* samples, int count)
{
    Call* call = findCall(callref);

    if (!call)
    {
        std::fill(samples, samples + count, 0.0f);
        return;
    }

    // Progress tones go to the calling mobile only.
    if ((call->refA == callref) && (call->tone.tone() != Tone::Off))
    {
        call->tone.read(samples, count);
        return;
    }

    if (!call->connected)
    {
        std::fill(samples, samples + count, 0.0f);
        return;
    }

    if (call->endpoint) {
        call->endpoint->toMobile(samples, count);
    } else if (call->refA == callref) {
        popAudio(call->toA, samples, count);
    } else {
        popAudio(call->toB, samples, count);
    }
}

} // namespace modemcellular
