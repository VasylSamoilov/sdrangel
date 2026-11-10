///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Andreas Eversberg <jolly@eversberg.eu>                     //
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

#ifndef MODEMNMT_NMTSYSTEM_H_
#define MODEMNMT_NMTSYSTEM_H_

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "clock.h"
#include "export.h"
#include "nmtchannel.h"
#include "timer.h"

namespace modemnmt
{

// One NMT base station: its radio channels, the subscribers that roamed in and the
// transactions that are going on. Timers run on the clock that is given; run() fires the due
// ones and is called at block boundaries. Not thread safe: the caller serialises every call
// into the system and its channels.
class MODEMNMT_API NmtSystem : public NmtHost
{
public:
    using RegistrationListener = std::function<void(const NmtSubscriber& subscriber)>;

    explicit NmtSystem(const modemcellular::Clock& clock);
    ~NmtSystem();
    NmtSystem(const NmtSystem&) = delete;
    NmtSystem& operator=(const NmtSystem&) = delete;

    // Adds a radio channel; the reference stays valid for the life of the system.
    NmtChannel& addChannel(const ChannelConfig& config, double sampleRate, double deviationFactor);
    size_t channelCount() const { return m_channels.size(); }
    NmtChannel& channel(size_t index) { return *m_channels[index]; }

    // The handlers are also called while the system is destroyed (a call to a phone that loses
    // its last channel is reported as failed): whoever sets them has to clear them, or outlive
    // the system.
    // Called each time a phone has been confirmed on a roaming update.
    void setRegistrationListener(RegistrationListener listener) { m_listener = std::move(listener); }

    // A phone dialled a number. Returns true if the call is taken; without a handler no call is.
    using CallHandler = std::function<bool(const NmtSubscriber& subscriber, const std::string& dialled)>;
    // A phone cleared a call that was taken.
    using ClearHandler = std::function<void(const NmtSubscriber& subscriber)>;
    void setCallHandler(CallHandler handler) { m_callHandler = std::move(handler); }
    void setClearHandler(ClearHandler handler) { m_clearHandler = std::move(handler); }
    // The far end ended the call of a phone: the phone is released. False if the phone has no
    // call that can be released.
    bool releaseCall(char country, const std::string& number);

    // Switches a connected call in progress to another traffic channel of this base station
    // (NMT Doc 450-1 para 4.4.1.4, scheme C2': the short procedure). `channel` is the radio
    // channel number wanted, 0 for the first free traffic channel. False if the phone has no
    // connected call on a single channel, a switch is going on, or there is no such free
    // traffic channel. If the phone does not show up on the new channel, the call stays on the
    // old one.
    bool moveCall(char country, const std::string& number, int channel = 0);

    // Tells the phone that messages wait (frame 5c, flags of MwiFlag; NMT Doc 450-1 4.4.1.16). A
    // phone in a call hears it when the call is cleared. A phone that is on a calling channel
    // is paged and called without ringing (4.4.1.12). False if the phone is busy with something
    // else, there is no idle calling channel or traffic channel, or the flags are invalid. The handler tells how it ended:
    // Undelivered flags can be sent again.
    bool sendMwi(char country, const std::string& number, uint8_t flags);
    using MwiHandler = std::function<void(const NmtSubscriber& subscriber, uint8_t flags, NmtServiceEnd result)>;
    void setMwiHandler(MwiHandler handler) { m_mwiHandler = std::move(handler); }

    // Orders the phone of a connected call to another power level 0..3 (NMT Doc 450-1 4.4.1.8).
    // False if the phone has no connected call, or it has that level already. The result is
    // told to the power handler: the level, and whether the phone took it. The new level is
    // kept for the call; the next call starts with the level of its channel.
    bool changePower(char country, const std::string& number, int level);
    using PowerHandler = std::function<void(const NmtSubscriber& subscriber, int level, bool done)>;
    void setPowerHandler(PowerHandler handler) { m_powerHandler = std::move(handler); }

    // The phone presses the MFT button during a call and dials: the digits (0-9, *, #, A-D) are
    // for the network, one call per digit. And a register recall: the digits after the R button;
    // the handler says true when it has enough and the recall ends, the end handler gets all
    // digits (also after a pause of the phone).
    using DigitHandler = std::function<void(const NmtSubscriber& subscriber, char digit)>;
    using RecallDigitHandler = std::function<bool(const NmtSubscriber& subscriber, char digit)>;
    using RecallEndHandler = std::function<void(const NmtSubscriber& subscriber, const std::string& digits)>;
    void setMftHandler(DigitHandler handler) { m_mftHandler = std::move(handler); }
    void setRecallHandlers(RecallDigitHandler digit, RecallEndHandler end)
    {
        m_recallDigitHandler = std::move(digit);
        m_recallEndHandler = std::move(end);
    }

    // A call to a phone: it is paged on the calling channels and moved to a traffic channel
    // when it answers. False, and at once, if the phone has a call or a roaming update going on
    // (cause Busy), or there is no idle calling channel (cause NoChannel); `cause` may be null.
    // With no idle traffic channel when the phone answers, the call is queued. The caller is told
    // through the call handlers set below. The call can be ended with releaseCall().
    bool startCall(char country, const std::string& number, const std::string& callerId,
                   modemcellular::NumberType callerType, modemcellular::Cause* cause = nullptr);
    // There is an idle calling channel to page with (a combined channel is one too).
    bool canCall() const;
    using CallEventHandler = std::function<void(const NmtSubscriber& subscriber)>;
    using CallFailedHandler = std::function<void(const NmtSubscriber& subscriber, modemcellular::Cause cause)>;
    // The phone rings; the phone answered; the call to the phone failed before it was answered.
    void setCallToPhoneHandlers(CallEventHandler alerting, CallEventHandler answered, CallFailedHandler failed)
    {
        m_alerting = std::move(alerting);
        m_answered = std::move(answered);
        m_failed = std::move(failed);
    }

    // Speech of a connected call, 8 kHz mono float. Without a handler the audio from the phone
    // is dropped and the phone gets silence.
    using AudioFromMobile = std::function<void(const NmtSubscriber& subscriber, const float* samples, int count)>;
    using AudioToMobile = std::function<void(const NmtSubscriber& subscriber, float* samples, int count)>;
    void setAudioHandlers(AudioFromMobile fromMobile, AudioToMobile toMobile)
    {
        m_audioFromMobile = std::move(fromMobile);
        m_audioToMobile = std::move(toMobile);
    }
    // Subscribers that roamed in, by country digit and number ("1123456").
    bool isRegistered(char country, const std::string& number) const;
    // The data of a registered subscriber, null if it is not registered.
    const NmtSubscriber* registeredSubscriber(char country, const std::string& number) const;
    // Calls the function for every registered subscriber (a copy).
    void forEachRegistered(const std::function<void(const NmtSubscriber&)>& function) const;
    size_t registeredCount() const { return m_registered.size(); }

    void run() { m_timers.run(); }
    size_t transactionCount() const { return m_transactions.size(); }

    // NmtHost
    modemcellular::TimerService& timers() override { return m_timers; }
    NmtTransaction* createTransaction(const NmtSubscriber& subscriber, NmtChannel& channel) override;
    void destroyTransaction(NmtTransaction* transaction) override;
    void detachChannel(NmtChannel& channel, NmtTransaction* transaction) override;
    void roamingConfirmed(const NmtSubscriber& subscriber) override;
    bool callDialled(NmtTransaction& transaction, const std::string& dialled) override;
    void callCleared(NmtTransaction& transaction) override;
    void audioFromMobile(NmtTransaction& transaction, const float* samples, int count) override;
    void audioToMobile(NmtTransaction& transaction, float* samples, int count) override;
    NmtChannel* findTrafficChannel(NmtChannel& from) override;
    NmtTransaction* startPriorityCall(const NmtSubscriber& subscriber, NmtChannel& channel) override;
    void pageAgain(NmtTransaction& transaction) override;
    NmtTransaction* calledPhoneSeized(const NmtSubscriber& subscriber, NmtChannel& channel) override;
    void pagingAnswered(NmtTransaction& transaction, NmtChannel& channel) override;
    void moveToTrafficChannel(NmtTransaction& transaction, NmtChannel& from, NmtChannel& to) override;
    void callAlerting(NmtTransaction& transaction) override;
    void callAnswered(NmtTransaction& transaction) override;
    void callFailed(NmtTransaction& transaction, modemcellular::Cause cause) override;
    void scipCompleted(NmtTransaction& transaction) override;
    void scipEnded(NmtTransaction& transaction, bool forceRelease) override;
    void mftDigit(NmtTransaction& transaction, char digit) override;
    bool recallDigit(NmtTransaction& transaction, char digit) override;
    void recallEnded(NmtTransaction& transaction, const std::string& digits) override;
    void powerChanged(NmtTransaction& transaction, int level, bool done) override;

private:
    struct MwiReport
    {
        bool valid = false;
        NmtSubscriber subscriber;
        uint8_t flags = 0;
        NmtServiceEnd result = NmtServiceEnd::Undelivered;
    };
    bool cancelMessageCall(char country, const std::string& number, uint8_t* carried);
    MwiReport mwiReport(NmtTransaction& transaction);
    void sendMwiReport(const MwiReport& report);
    NmtTransaction& newPagedTransaction(char country, const std::string& number);
    void page(NmtTransaction& transaction, int tryNo);
    void pagingTimeout(NmtTransaction& transaction);

    modemcellular::TimerService m_timers;       // before the channels, their timers use it
    std::list<NmtTransaction> m_transactions;
    std::map<std::string, NmtSubscriber> m_registered;
    RegistrationListener m_listener;
    CallHandler m_callHandler;
    ClearHandler m_clearHandler;
    AudioFromMobile m_audioFromMobile;
    AudioToMobile m_audioToMobile;
    MwiHandler m_mwiHandler;
    PowerHandler m_powerHandler;
    DigitHandler m_mftHandler;
    RecallDigitHandler m_recallDigitHandler;
    RecallEndHandler m_recallEndHandler;
    CallEventHandler m_alerting;
    CallEventHandler m_answered;
    CallFailedHandler m_failed;
    std::vector<std::unique_ptr<NmtChannel>> m_channels;
};

} // namespace modemnmt

#endif // MODEMNMT_NMTSYSTEM_H_
