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

#ifndef MODEMCELLULAR_CONSOLELEG_H_
#define MODEMCELLULAR_CONSOLELEG_H_

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "calltypes.h"
#include "cellularsystem.h"
#include "export.h"
#include "mobileleg.h"

namespace modemcellular
{

// The console: the operator's own phone on a network, with a number the mobiles can call. It
// dials out, rings when it is called and is answered by hand (or at once), and carries the
// operator's microphone and speaker (8 kHz, 1.0 = the reference). Touch tones (DTMF) from its
// pad go into the speech it sends. One call at a time; a call while it is busy is refused busy.
//
// Threads: as the TestCaller. The router may call the MobileLeg functions from any thread; they
// change the state and queue the events. dial(), answer(), hangUp(), digit() and process() from
// the thread that runs the system; process() once per audio block.
class MODEMCELLULAR_API ConsoleLeg : public MobileLeg
{
public:
    enum class State
    {
        Idle,
        Calling,        // it dialled: the mobile is paged
        Ringback,       // the mobile rings
        Incoming,       // a mobile calls it: it rings
        Connected
    };
    using EventHandler = std::function<void(const std::string& text)>;
    // the microphone fills the samples; the speaker takes them
    using Microphone = std::function<void(float* samples, int count)>;
    using Speaker = std::function<void(const float* samples, int count)>;

    // the console's number is registered with the router of the system
    ConsoleLeg(CellularSystem& system, const std::string& number);
    ~ConsoleLeg() override;
    ConsoleLeg(const ConsoleLeg&) = delete;
    ConsoleLeg& operator=(const ConsoleLeg&) = delete;

    void setEventHandler(EventHandler handler) { m_handler = std::move(handler); }
    void setMicrophone(Microphone microphone) { m_microphone = std::move(microphone); }
    void setSpeaker(Speaker speaker) { m_speaker = std::move(speaker); }
    // a call that comes in is answered at once (no ringing)
    void setAutoAnswer(bool autoAnswer) { m_autoAnswer = autoAnswer; }

    // Calls a number. False if a call is going on or the router refused it (cause, if not null).
    bool dial(const std::string& number, Cause* cause = nullptr);
    // Answers the call that rings. False if none rings.
    bool answer();
    // Ends the call, or refuses the one that rings (busy).
    void hangUp();
    // A touch tone (0-9, *, #, A-D) into the speech sent, while connected: 100 ms tone, 60 ms pause.
    bool digit(char key);
    // count samples of audio both ways at kAudioSampleRate, and the events since the last time
    void process(int count);

    State state() const;
    const std::string& number() const { return m_number; }
    std::string peer() const;           // the number it calls or that calls it

    // MobileLeg (from the router)
    void setup(CallRef callref, const std::string& callerId, NumberType callerType, const std::string& dialed) override;
    void proceeding(CallRef callref) override;
    void alerting(CallRef callref) override;
    void answer(CallRef callref) override;
    void release(CallRef callref, Cause cause) override;

private:
    void event(const std::string& text);
    void addTones(float* samples, int count);

    CellularSystem& m_system;
    const std::string m_number;
    EventHandler m_handler;
    Microphone m_microphone;
    Speaker m_speaker;
    bool m_autoAnswer = false;
    mutable std::mutex m_mutex;         // the state, the call reference, the peer and the events
    State m_state;
    CallRef m_ref;
    std::string m_peer;
    std::deque<std::string> m_events;
    std::deque<char> m_keys;            // touch tones still to send
    int m_keyPos;                       // samples of the current tone and its pause
    std::vector<float> m_in;
    std::vector<float> m_out;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CONSOLELEG_H_
