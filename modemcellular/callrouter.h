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

#ifndef MODEMCELLULAR_CALLROUTER_H_
#define MODEMCELLULAR_CALLROUTER_H_

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "audioendpoint.h"
#include "calltypes.h"
#include "export.h"
#include "mobileleg.h"
#include "timer.h"
#include "tonegenerator.h"

namespace modemcellular
{

// Audio between two mobiles waits in a queue for the one that listens. The queue is bounded:
// above 200 ms the oldest audio is dropped down to 60 ms, so a mobile that starts to listen late
// does not stay behind for the rest of the call (one click, at the moment of the cut).
//
// Routes calls placed by mobiles and acts as the network for them. A dialed number that
// is registered goes to that mobile. Any other number ends on a fallback endpoint (music,
// echo, later SIP).
//
// Progress tones: the calling mobile hears ringback while the called mobile rings. If the
// called mobile is busy or cannot be reached, the caller hears busy or congestion tone and
// the call is released after the tone timeout, or earlier when the caller hangs up. While a
// tone plays, audio from the caller is dropped.
//
// Not thread safe: the owner serialises all calls, including the timer service (see
// CellularSystem). Notifications to a MobileLeg are delivered from inside router calls or
// from TimerService::run(), unless a notification handler is set.
class MODEMCELLULAR_API CallRouter
{
public:
    enum class Fallback
    {
        Music,
        Echo
    };

    using EndpointFactory = std::function<std::unique_ptr<AudioEndpoint>()>;

    struct SetupResult
    {
        CallRef callref;    // 0 if the call was refused
        Cause cause;        // why it was refused
        bool ok() const { return callref != 0; }
    };

    // One call from the router to a MobileLeg.
    struct Notification
    {
        enum class Type
        {
            Setup,
            Proceeding,
            Alerting,
            Answer,
            Release
        };

        Type type = Type::Release;
        MobileLeg* leg = nullptr;
        CallRef callref = 0;
        Cause cause = Cause::Normal;            // Release
        std::string callerId;                   // Setup
        NumberType callerType = NumberType::NotAvailable;   // Setup
        std::string dialed;                     // Setup
    };

    // The tone region is the one of the standard this base station implements.
    CallRouter(TimerService& timers, ToneSet toneSet);
    ~CallRouter();
    CallRouter(const CallRouter&) = delete;
    CallRouter& operator=(const CallRouter&) = delete;

    // Built-in fallback. Replaces a factory set before.
    void setFallback(Fallback fallback);
    // Custom fallback, for example a SIP bridge. A factory returning null refuses the call.
    void setFallbackFactory(EndpointFactory factory);
    // Time from "proceeding" to the answer of a fallback endpoint.
    void setAnswerDelay(double seconds) { m_answerDelay = seconds; }
    // How long a busy or congestion tone plays before the call is released (30 s).
    void setToneTimeout(double seconds) { m_toneTimeout = seconds; }

    // Instead of calling the legs directly, hand every notification to this handler. The
    // handler runs inside router calls and must only queue the notification; deliver() makes
    // the call to the leg later.
    void setNotificationHandler(std::function<void(Notification&&)> handler) { m_handler = std::move(handler); }
    static void deliver(const Notification& notification);

    // Makes a number reachable on a mobile. A number registered again moves to the new leg.
    bool registerSubscriber(const std::string& number, MobileLeg& leg);
    // Removes the number if it is still registered on this leg. Returns true if removed.
    bool unregisterSubscriber(const std::string& number, MobileLeg& leg);
    bool isRegistered(const std::string& number) const;
    // Forgets a leg that goes away: unregisters its numbers and ends its calls. The other
    // side of each call is released with Cause::OutOfOrder (a calling mobile hears the
    // congestion tone first, unless the call is connected); the leg itself is not notified.
    // Returns the numbers that were unregistered.
    std::vector<std::string> detach(MobileLeg& leg);

    // A mobile places a call. The origin leg is told "proceeding" and then "alerting"/"answer"
    // from a later TimerService::run(), never from inside this call. A busy called mobile does
    // not refuse the call: the caller hears busy tone. Refused: empty number, no endpoint.
    SetupResult setup(MobileLeg& origin, const std::string& callerId, const std::string& dialed);
    // The called mobile is ringing / has answered. callref is the one given to MobileLeg::setup().
    void alerting(CallRef callref);
    void answer(CallRef callref);
    // Either side ends a call. Unknown call references are ignored. A called mobile that
    // releases a call that is not connected passes its cause on: busy and similar causes are
    // played to the caller as a tone.
    void release(CallRef callref, Cause cause);

    // Audio of a call. Before the answer, received audio is dropped and silence is returned,
    // except that the calling mobile receives ringback, busy or congestion tone.
    void audioFromMobile(CallRef callref, const float* samples, int count);
    void audioToMobile(CallRef callref, float* samples, int count);

    size_t callCount() const { return m_calls.size(); }

private:
    struct Call;

    Call* findCall(CallRef callref) const;
    CallRef allocateRef();
    std::unique_ptr<AudioEndpoint> makeFallbackEndpoint();
    std::unique_ptr<Call> takeCall(CallRef callref);
    void forgetBusy(const std::string& number);
    void emit(Notification&& notification);
    void notify(Notification::Type type, MobileLeg* leg, CallRef callref, Cause cause = Cause::Normal);

    void onProceeding(CallRef refA);
    void onEndpointAnswer(CallRef refA);
    void onToneTimeout(CallRef refA, Cause cause);
    void farAlerting(CallRef refA);
    void farAnswer(CallRef refA);
    void farEnd(CallRef refA, Cause cause);

    TimerService& m_timers;
    Fallback m_fallback;
    EndpointFactory m_factory;
    double m_answerDelay;
    double m_toneTimeout;
    ToneSet m_toneSet;
    std::function<void(Notification&&)> m_handler;
    CallRef m_nextRef;
    std::map<std::string, MobileLeg*> m_subscribers;
    std::multiset<std::string> m_busy;      // numbers taking part in a call
    std::vector<std::unique_ptr<Call>> m_calls;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CALLROUTER_H_
