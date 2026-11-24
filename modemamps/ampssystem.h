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

#ifndef MODEMAMPS_AMPSSYSTEM_H_
#define MODEMAMPS_AMPSSYSTEM_H_

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ampsfocc.h"
#include "ampsplan.h"
#include "ampsrecc.h"
#include "ampssysinfo.h"
#include "clock.h"
#include "timer.h"
#include "export.h"

// The call processing of an AMPS base station, as osmocom-analog's amps (amps.c, transaction.c,
// frame.c amps_tx_frame_focc / amps_tx_frame_fvc), which works with the phones. It only takes events
// (the messages of the phones, SAT and signalling tone, the transmitters asking for their next
// message, timers, the network) and says what each channel does (its mode) and sends; the channels
// do the signal processing.
namespace modemamps
{

// What a channel sends and receives (osmocom's DSP modes)
enum class AmpsMode
{
    Off,            // a voice channel that is idle
    Control,        // FOCC out, RECC in
    Audio,          // speech and SAT out, speech, SAT, signalling tone and RVC in
    Silence,        // SAT out (and no speech), the same in
    Frame           // FVC orders out in place of the speech, the same in
};

MODEMAMPS_API const char *ampsModeName(AmpsMode mode);

struct MODEMAMPS_API AmpsSubscriber
{
    uint32_t min1 = 0;
    uint16_t min2 = 0;
    uint32_t esn = 0;
    uint8_t scm = 0;

    // the 10 digits of the MIN: area code (MIN2) and number (MIN1)
    std::string number() const;
};

struct AmpsChannelConfig
{
    int channel = 0;
    AmpsChannelType type = AmpsChannelType::Voice;
    int scc = 0;                // SAT colour code 0..2 of a voice channel
};

struct AmpsSystemConfig
{
    SystemInfo si;
    std::vector<AmpsChannelConfig> channels;
    // the round trip of the radio (s): what the phone sends in answer to us arrives this much later
    double roundTrip = 0.0;
};

class MODEMAMPS_API AmpsSystem
{
public:
    // osmocom amps.c
    static constexpr double kSatTimeout1 = 5.0;         // the SAT of the phone on the new voice channel
    static constexpr double kSatTimeout2 = 5.0;         // the SAT lost in a call
    static constexpr double kReleaseTimeout = 5.0;      // the phone confirming our release
    static constexpr double kFlashMin = 0.2;            // signalling tone: a flash
    static constexpr double kFlashMax = 0.8;            // longer: a release
    static constexpr double kStReleaseTime = 3.0;       // a release of the phone if the tone lasts

    enum class State
    {
        RegisterAck, RegisterAckSend,       // the registration acknowledged on the FOCC
        Reorder, ReorderSend,               // the call refused on the FOCC
        MoAssign, MoAssignSend,             // the voice channel designated on the FOCC
        MoAssignConfirm,                    // on the voice channel, waiting for the SAT of the phone
        Call,
        Release, ReleaseSend                // our release on the FVC, waiting for the phone's tone
    };

    // the phone's call: true if it goes on (a voice channel is assigned), false refuses it (reorder)
    using CallHandler = std::function<bool(const AmpsSubscriber& subscriber, const std::string& dialled)>;
    // the phone's call ended other than by releaseCall()
    using ClearHandler = std::function<void(const AmpsSubscriber& subscriber)>;
    // a registration; powerDown: the phone switches off
    using RegistrationHandler = std::function<void(const AmpsSubscriber& subscriber, bool powerDown)>;
    using AudioFromMobile = std::function<void(const AmpsSubscriber& subscriber, const float* samples, int count)>;
    using AudioToMobile = std::function<void(const AmpsSubscriber& subscriber, float* samples, int count)>;
    using LogHandler = std::function<void(const std::string& text)>;

    AmpsSystem(const modemcellular::Clock& clock, const AmpsSystemConfig& config);
    ~AmpsSystem();
    AmpsSystem(const AmpsSystem&) = delete;
    AmpsSystem& operator=(const AmpsSystem&) = delete;

    const AmpsSystemConfig& config() const { return m_config; }
    void setCallHandler(CallHandler handler) { m_callHandler = std::move(handler); }
    void setClearHandler(ClearHandler handler) { m_clearHandler = std::move(handler); }
    void setRegistrationHandler(RegistrationHandler handler) { m_registrationHandler = std::move(handler); }
    void setAudioHandlers(AudioFromMobile fromMobile, AudioToMobile toMobile)
    {
        m_audioFromMobile = std::move(fromMobile);
        m_audioToMobile = std::move(toMobile);
    }
    void setLogHandler(LogHandler handler) { m_log = std::move(handler); }

    // the events of the channels (index into config().channels)
    void receivedRecc(int channel, const ReccMessage& message);
    // an order confirmation of the phone on the RVC (T 1)
    void receivedRvcOrder(int channel, uint8_t order, uint8_t ordq);
    // the SAT of the phone: its colour code, -1 lost
    void satChanged(int channel, int scc);
    void signallingTone(int channel, bool on);
    // the transmitters asking for their next message (FoccTransmitter / FvcTransmitter sources)
    bool nextFocc(int channel, FoccMessage& message);
    bool nextFvc(int channel, uint64_t& word);
    // the speech of a channel in a call
    void audioFromMobile(int channel, const float* samples, int count);
    void audioToMobile(int channel, float* samples, int count);

    AmpsMode mode(int channel) const;
    // the network releases the call of the phone with this number (10 digits)
    bool releaseCall(const std::string& number);
    // fires the timers that are due
    void run() { m_timers.run(); }

    struct TransactionInfo
    {
        AmpsSubscriber subscriber;
        State state;
        int channel;            // where it is: the control channel or the voice channel
        int voiceChannel;       // the assigned voice channel, -1 none
    };
    std::vector<TransactionInfo> transactions() const;
    static const char *stateName(State state);
    // the phones that registered (and did not power down since), by number
    size_t registeredCount() const { return m_registered.size(); }
    void forEachRegistered(const std::function<void(const AmpsSubscriber&)>& function) const
    {
        for (const auto& r : m_registered) {
            function(r.second);
        }
    }

private:
    struct Transaction;
    struct Channel
    {
        AmpsChannelConfig config;
        AmpsMode mode = AmpsMode::Off;
        bool busy = false;
        int sat = -1;               // the SAT of the phone, -1 none
    };

    Transaction* create(const AmpsSubscriber& subscriber, State state, int channel);
    void destroy(Transaction* transaction);
    void goIdle(int channel);
    Transaction* transactionOf(uint32_t min1, uint16_t min2);
    Transaction* transactionOn(int channel);
    int freeVoiceChannel() const;
    void reorder(const AmpsSubscriber& subscriber, int channel);
    void assignVoiceChannel(Transaction* transaction);
    void release(Transaction* transaction, bool tellNetwork);
    void phoneReleased(Transaction* transaction);
    void timeout(Transaction* transaction);
    void setState(Transaction* transaction, State state);
    void log(const std::string& text);
    bool valid(int channel) const { return channel >= 0 && channel < static_cast<int>(m_channels.size()); }

    const modemcellular::Clock& m_clock;
    AmpsSystemConfig m_config;
    modemcellular::TimerService m_timers;
    std::vector<Channel> m_channels;
    std::list<std::unique_ptr<Transaction>> m_transactions;
    std::map<std::string, AmpsSubscriber> m_registered;
    CallHandler m_callHandler;
    ClearHandler m_clearHandler;
    RegistrationHandler m_registrationHandler;
    AudioFromMobile m_audioFromMobile;
    AudioToMobile m_audioToMobile;
    LogHandler m_log;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSSYSTEM_H_
