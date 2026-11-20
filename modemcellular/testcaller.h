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

#ifndef MODEMCELLULAR_TESTCALLER_H_
#define MODEMCELLULAR_TESTCALLER_H_

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audioendpoint.h"
#include "calltypes.h"
#include "cellularsystem.h"
#include "export.h"
#include "mobileleg.h"

namespace modemcellular
{

// The network calls a mobile: a test call placed by the operator. The far end of the call is an
// audio endpoint (echo or music) that is connected when the mobile answers. One call at a time.
//
// Threads: the router may call the MobileLeg functions from any thread; they only change the
// state and queue what happened. process() runs the audio and tells the events, from the thread
// that runs the system (once per audio block). call(), hangUp() and the destructor from that
// thread too.
class MODEMCELLULAR_API TestCaller : public MobileLeg
{
public:
    enum class State
    {
        Idle,
        Calling,        // the mobile is paged
        Ringing,
        Connected
    };
    using EventHandler = std::function<void(const std::string& text)>;

    explicit TestCaller(CellularSystem& system);
    ~TestCaller() override;
    TestCaller(const TestCaller&) = delete;
    TestCaller& operator=(const TestCaller&) = delete;

    void setEventHandler(EventHandler handler) { m_handler = std::move(handler); }
    // Calls number on the leg called (null: the leg the number is registered on). False if a
    // call is going on or the router refused it (cause, if not null).
    bool call(const std::string& number, const std::string& callerId, MobileLeg* called,
              std::unique_ptr<AudioEndpoint> endpoint, Cause* cause = nullptr);
    // Ends the call, if there is one.
    void hangUp();
    // count samples of audio both ways at kAudioSampleRate, and the events since the last time
    void process(int count);

    State state() const;
    std::string number() const;
    static const char* causeName(Cause cause);

    // MobileLeg (from the router)
    void setup(CallRef callref, const std::string& callerId, NumberType callerType, const std::string& dialed) override;
    void proceeding(CallRef callref) override;
    void alerting(CallRef callref) override;
    void answer(CallRef callref) override;
    void release(CallRef callref, Cause cause) override;

private:
    void event(const std::string& text);

    CellularSystem& m_system;
    EventHandler m_handler;
    mutable std::mutex m_mutex;         // the state, the call reference and the events
    State m_state;
    CallRef m_ref;
    std::string m_number;
    std::deque<std::string> m_events;
    std::unique_ptr<AudioEndpoint> m_endpoint;
    std::vector<float> m_fromMobile;
    std::vector<float> m_toMobile;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_TESTCALLER_H_
