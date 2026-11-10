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

#ifndef MODEMNMT_NMTCHANNEL_H_
#define MODEMNMT_NMTCHANNEL_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "nmtframe.h"
#include "nmtmodem.h"
#include "nmtmwi.h"
#include "nmtnumbers.h"
#include "nmtvoice.h"
#include "timer.h"
#include "export.h"

namespace modemnmt
{

class NmtChannel;

// A mobile station as the exchange knows it: country digit, six digit number, password.
struct NmtSubscriber
{
    char country = '0';
    std::string number;         // six digits
    std::string password;       // three digits, as sent by the phone at roaming
    bool coinbox = false;       // the phone is a coin-box phone
};

// What a subscriber is doing on the channels of the base station.
struct NmtTransaction
{
    NmtSubscriber subscriber;
    std::vector<NmtChannel*> channels;      // channels the transaction is bound to
    bool callUp = false;                    // a call exists in the exchange for the transaction
    bool answered = false;                  // a call to the phone was answered

    // call to the phone
    bool mobileTerminated = false;
    // Where a call to the phone is while it is on the calling channels (the calling channel
    // carousel of NmtSystem): the phone is paged (2a), answered and gets the traffic channel
    // (2b), or answered and waits for a free one (2f / 2c, then standby).
    enum class CcPhase { None, Deferred, Page, Order, Queue };
    CcPhase ccPhase = CcPhase::None;
    std::vector<NmtChannel*> pageChannels;  // calling channels that page the phone
    std::vector<NmtChannel*> pageDue;       // ... whose 2a of this try is still to go out
    NmtChannel* ccChannel = nullptr;        // calling channel of the answer (Order, Queue)
    int queueInfoLeft = 0;                  // 2f / 2c frames still to send
    bool repaged = false;                   // paged again: a traffic channel became free for it
    int pageTry = 0;
    bool pageDeferred = false;              // waiting for the wake-up window of battery saving
    bool pageWaited = false;                // the wake-up window was waited for
    bool priority = false;                  // the phone answered as a priority phone
    // The phone seized a calling channel as a priority phone to place a call (frame 11b): it is
    // paged to check its identity, gets a traffic channel and is rung to lift the handset, and then
    // dials like any phone that places a call. There is no call in the exchange before it dials.
    bool priorityMo = false;
    std::string callerId;
    modemcellular::NumberType callerType = modemcellular::NumberType::NotAvailable;
    std::unique_ptr<modemcellular::Timer> timer;        // page tries, battery saving wait, queue

    // switching the call to another traffic channel: the channel the phone is sent to
    NmtChannel* scipTarget = nullptr;

    // power level of the phone for this call, 0..3; -1 for the one of the channel
    int power = -1;

    // A call only to tell the phone about waiting messages: no call in the exchange (callUp stays
    // false), the phone is not rung but asked to take the call without ringing (L(12)).
    bool messageCall = false;
    // Message indicators for the clearing of the call (also of an ordinary call), and how it ended.
    uint8_t mwiFlags = 0;
    NmtServiceEnd mwiResult = NmtServiceEnd::Undelivered;
    bool mwiReported = false;
};

// What a channel needs from the base station it belongs to (implemented by NmtSystem).
class NmtHost
{
public:
    virtual ~NmtHost() = default;
    virtual modemcellular::TimerService& timers() = 0;
    // Binds a new transaction of the subscriber to the channel. Null if the subscriber has one.
    virtual NmtTransaction* createTransaction(const NmtSubscriber& subscriber, NmtChannel& channel) = 0;
    // Ends the transaction; every channel bound to it goes idle.
    virtual void destroyTransaction(NmtTransaction* transaction) = 0;
    // The channel is going away while it is bound to the transaction: the transaction ends
    // when no other channel is bound to it.
    virtual void detachChannel(NmtChannel& channel, NmtTransaction* transaction) = 0;
    // The phone has identified itself on a roaming update.
    virtual void roamingConfirmed(const NmtSubscriber& subscriber) = 0;
    // The phone has dialled a number. True if the call was taken, the phone then gets the
    // address complete signal. False if it cannot be placed, the phone is released.
    virtual bool callDialled(NmtTransaction& transaction, const std::string& dialled) = 0;
    // The phone cleared a call that was taken with callDialled().
    virtual void callCleared(NmtTransaction& transaction) = 0;
    // Speech of a connected call, 8 kHz mono: from the phone, and for the phone (the host
    // fills the samples, silence if it has none). Called from the audio path of the channel.
    virtual void audioFromMobile(NmtTransaction& transaction, const float* samples, int count) = 0;
    virtual void audioToMobile(NmtTransaction& transaction, float* samples, int count) = 0;

    // Calls to the phone
    // The best free traffic channel for a call that is on the calling channel `from`: a traffic
    // channel, or a combined one (the calling channel itself counts as free). Null if none.
    virtual NmtChannel* findTrafficChannel(NmtChannel& from) = 0;
    // A priority phone seized the calling channel (frame 11b) to place a call: a transaction for
    // it is paged on the calling channels. Null if the phone has a transaction already.
    virtual NmtTransaction* startPriorityCall(const NmtSubscriber& subscriber, NmtChannel& channel) = 0;
    // A phone seized the traffic channel as the called phone (frame 10c): the transaction that
    // pages it is bound to this channel and leaves the calling channels. Null if no call to the
    // phone is waiting.
    virtual NmtTransaction* calledPhoneSeized(const NmtSubscriber& subscriber, NmtChannel& channel) = 0;

    // The calling channel carousel. An idle calling channel sends, between its idle frames, the
    // frames of the calls to phones that are on the calling channels.
    struct CarouselItem
    {
        enum class Kind { None, Page, Order, Queue };
        Kind kind = Kind::None;
        NmtTransaction* transaction = nullptr;
        NmtChannel* trafficChannel = nullptr;   // Order: the channel the phone is sent to
    };
    // The next frame for the calling channel; the item counts as sent.
    virtual CarouselItem carouselItem(NmtChannel& callingChannel) = 0;
    // A page of this calling channel waits for its answer (a frame 10a / 10d is expected).
    virtual bool awaitsAnswer(const NmtChannel& callingChannel) const = 0;
    // The phone answered a page on the calling channel (10a, 10d; 11b from a priority phone).
    // False if no page of that phone is going on there.
    virtual bool callingChannelAnswer(const NmtSubscriber& subscriber, NmtChannel& callingChannel, bool priority) = 0;
    // The channel order (2b) went out: the call is on the traffic channel now.
    virtual void orderSent(NmtTransaction& transaction, NmtChannel& trafficChannel) = 0;
    // The phone of a call that is on the calling channels clears on this calling channel (L(1)):
    // the transaction is bound to the channel, which acknowledges. Null if there is no such call.
    virtual NmtTransaction* callingChannelCleared(const NmtSubscriber& subscriber, NmtChannel& callingChannel) = 0;
    // The phone rings, and it was answered.
    virtual void callAlerting(NmtTransaction& transaction) = 0;
    virtual void callAnswered(NmtTransaction& transaction) = 0;
    // The call to the phone failed before it was answered (no answer, no channel, ...).
    virtual void callFailed(NmtTransaction& transaction, modemcellular::Cause cause) = 0;

    // Switching a call in progress to another traffic channel
    // The phone identified itself on the new channel (transaction.scipTarget): the call is
    // there now, the old channel goes idle.
    virtual void scipCompleted(NmtTransaction& transaction) = 0;
    // The switch is over without the phone on the new channel: the call stays where it is. With
    // forceRelease the new channel sends the release signal, else it just goes idle.
    virtual void scipEnded(NmtTransaction& transaction, bool forceRelease) = 0;

    // In-call procedures
    // A digit of the MFT converter (the phone sends push-button digits to the network).
    virtual void mftDigit(NmtTransaction& transaction, char digit) = 0;
    // A digit after a register recall. True if the digits are enough: the recall ends.
    virtual bool recallDigit(NmtTransaction& transaction, char digit) = 0;
    // The register recall is over; the digits received.
    virtual void recallEnded(NmtTransaction& transaction, const std::string& digits) = 0;
    // The phone answered the order to another power level, or did not.
    virtual void powerChanged(NmtTransaction& transaction, int level, bool done) = 0;
};

// What the transmitter of a channel sends instead of frames.
enum class DspMode
{
    Frame,          // signalling frames
    DialTone,       // dial tone, 425 Hz
    Audio           // speech; silence until the voice path is connected
};

enum class ChannelType
{
    Cc,         // calling channel, frame 1a
    Cca,        // calling channel, group A, frame 1a' (NMT-900)
    Ccb,        // calling channel, group B, frame 1a'' (NMT-900)
    Tc,         // traffic channel, frame 4
    AcTc,       // access and traffic channel, frame 4b (NMT-900)
    CcTc,       // combined calling and traffic channel, frame 1b
    Test        // test channel, frame 30
};

struct ChannelConfig
{
    System system = System::Nmt450;
    ChannelType type = ChannelType::Cc;
    int channel = 1;                // radio channel number of this system
    int msPower = 1;                // power level of the mobile stations, 0..3
    uint8_t trafficArea = 0;        // two digits Y1Y2, region digit in the high nibble
    int areaNo = 0;                 // area number 1..4, 0 for none
    // Send the time with the calling channel. It is part of the battery saving frame (NMT Doc
    // 450-1 4.3.3.12.5), also with period 0, and not on a combined channel.
    bool sendClock = false;
    // Battery saving (NMT Doc 450-1 4.3.3.12, 900-1 4.3.3.14): period code 0..9 (24 .. 280 frames),
    // 0 for none (phones never sleep, the default); group 0..8, 8 addresses all phones.
    int batteryPeriod = 0;
    int batteryGroup = 8;
    int supervisory = 0;            // supervisory tone 1..4 on a traffic channel, 0 for none
    bool sendCallerId = false;      // send the number of the caller while the phone rings
    // NMT-900 channel band advertisement (900-1 4.3.3.13); channel numbers, 0 for none, sent in
    // steps of 4 channels. It and the battery saving frame are one per frame: with a battery saving
    // period they take turns, with period 0 only the advertisement is sent (no clock then).
    int ccBandStart = 0;
    int ccBandEnd = 0;
    int tcBandStart = 0;
    int tcBandEnd = 0;
    bool loopback = false;          // listen to our own frames (the receiver sees the exchange direction)
};

// Hour and minute of the local time.
using TimeOfDay = std::function<std::pair<int, int>()>;

// One NMT radio channel of a base station: the FFSK modem and the signalling of the channel.
// Without a host the channel only sends the idle frame of its type and passes everything it
// receives to the listener. With a host (attach) it also runs the call states: roaming update,
// calls from the phone with speech and supervision, and release.
//
// Not thread safe, the caller serialises all calls including the timers. In particular render()
// and receive() must not run at the same time: they share the state, the transaction, the
// timer, the supervisory detector and the modem. An audio handler runs inside one of them and
// must not block; it may call NmtSystem::releaseCall.
class MODEMNMT_API NmtChannel
{
public:
    enum class State
    {
        Idle,               // channel is not in use
        RoamingIdent,       // seizure received, waiting for the identity
        RoamingConfirm,     // identity received, sending the confirmation
        MoIdent,            // call seizure received, waiting for the identity
        MoConfirm,          // identity received, sending proceed to send
        MoDialling,         // receiving the digits, dial tone until the first one
        MoComplete,         // all digits received, sending address complete
        Active,             // call connected
        MtIdent,            // traffic channel: waiting for the identity of the phone
        MtRinging,          // traffic channel: the phone rings
        MtComplete,         // traffic channel: the phone answered, connecting
        MoRelease,          // acknowledging a release from the phone
        MtRelease,          // sending a release towards the phone
        ScipOrder,          // old traffic channel: ordering the phone to the new channel
        ScipIdent,          // new traffic channel: supervisory tone on, waiting for the phone
        ScipIdentReq,       // new traffic channel: asking the phone for its identity
        ScipRelease,        // new traffic channel: the phone did not come, forced release
        MftIn,              // call: the phone asked for the MFT converter, acknowledging
        Mft,                // call: the phone sends digits for the converter
        MftOut,             // call: leaving the MFT state (acknowledge or forced release)
        PowerChange,        // call: ordering the phone to another power level
        RecallProceed,      // call: register recall, acknowledging
        RecallDigits,       // call: register recall, dial tone, receiving digits
        RecallEnd,          // call: register recall over, back to the conversation
        MtAutoanswer,       // traffic channel: asking the phone to answer without ringing
        MwiClear            // clearing: the message indicators (5c), then the release
    };

    struct ReceivedFrame
    {
        Frame frame;
        double quality;
        double level;
        uint64_t framesElapsed;
        bool leadingZeros;
    };
    using FrameListener = std::function<void(const ReceivedFrame&)>;

    // sampleRate of the audio, deviationFactor scales the signalling level to the transmitter.
    NmtChannel(const ChannelConfig& config, double sampleRate, double deviationFactor);
    ~NmtChannel();
    NmtChannel(const NmtChannel&) = delete;
    NmtChannel& operator=(const NmtChannel&) = delete;

    // False if the configuration cannot be used: the modem and the frames then stay silent.
    bool valid() const { return m_valid; }
    const ChannelConfig& config() const { return m_config; }

    // Connects the channel to its base station, once. The host must outlive the channel.
    void attach(NmtHost& host);
    // Called by the host when the transaction of the channel ended.
    void transactionEnded();
    // The traffic channel takes over a call to the phone: identity request, then ringing.
    void beginMtIdent(NmtTransaction& transaction);
    // The connected call orders the phone to another power level 0..3 (NMT Doc 450-1 4.4.1.8).
    // False if the channel is not in a connected call, or the level is no change.
    bool beginPowerChange(int level);
    // Switching the call in progress: this connected channel orders the phone to the target
    // (transaction.scipTarget), which waits for it.
    void beginScipOrder();
    void beginScipIdent(NmtTransaction& transaction);
    void beginScipRelease();
    // The channel is a calling channel (also a combined one).
    bool isCallingChannel() const;
    // A call is connected here (also while a procedure of the call runs): clearing it carries the
    // message indicators.
    bool inCall() const { return inCallState(m_state); }
    static bool inCallState(State state);
    State state() const { return m_state; }

    void setTimeOfDay(TimeOfDay timeOfDay) { m_timeOfDay = std::move(timeOfDay); }
    void setFrameListener(FrameListener listener) { m_listener = std::move(listener); }

    // Next idle frame of the channel, counts the idle frames sent. The additional information
    // alternates between channel band advertisement and battery saving if both are configured.
    Frame idleFrame();
    uint64_t framesSent() const { return m_framesSent; }

    // Audio towards the radio: frames back to back, or the dial tone, or speech. Renders the
    // whole buffer; returns the number of samples that carried frames. Audio from the radio:
    // frames are decoded and given to the listener.
    int render(float* samples, int length, bool add);
    void receive(const float* samples, int length);
    DspMode dspMode() const { return m_dspMode; }
    // Whether the supervisory tone of the phone is heard (during a call).
    bool supervisoryDetected() const { return m_superDetector.detected(); }

    // The call ended on the far end: releases the phone. False if the phone is not in a state
    // that can be released.
    bool networkRelease();
    // The number the phone dialled so far.
    const std::string& dialled() const { return m_dialled; }

    // The frame as it came from the modem, for use without audio.
    void receiveFrame(const RxFrame& rx);
    // The next frame to send, as the modem asks for it. False if the channel cannot be used.
    bool nextFrame(FrameBits& bits);

private:
    bool bandAdvertisement() const;
    uint64_t additionalInfo(MessageType type);

    // frames to the phone
    Frame baseFrame(MessageType type) const;
    Frame lineSignalFrame(int signal) const;
    Frame txIdentRequest();
    Frame txRoamingConfirm();
    Frame txMoConfirm();
    Frame txMoComplete();
    Frame txCalling();
    Frame txPage(const NmtTransaction& transaction) const;
    Frame txQueueInfo(const NmtTransaction& transaction) const;
    Frame txOrder(NmtTransaction& transaction, NmtChannel& trafficChannel);
    Frame txMtRinging();
    Frame txMtComplete();
    void enterActive();
    void failCall(modemcellular::Cause cause);
    Frame txMoRelease();
    Frame txMtRelease();
    Frame txMftIn();
    Frame txMftOut();
    Frame txPowerChange();
    Frame txRecallProceed();
    Frame txRecallEnd();
    Frame txMtAutoanswer();
    Frame txMwiClear();
    void rxMtAutoanswer(const Frame& frame);
    void rxMwiClear(const Frame& frame);
    void beginMwiClear(int leadIdle, bool phoneCleared);
    void rxActive(const Frame& frame);
    void rxMft(const Frame& frame);
    void rxPowerChange(const Frame& frame);
    void rxRecall(const Frame& frame);
    void beginMftOut();
    void beginRecallEnd();
    void abortInCall(State previous);
    int power() const;
    enum class DigitResult { Foreign, Repeat, Damaged, Digit, Lost };
    DigitResult takeDigit(const Frame& frame, size_t count, size_t limit, char& digit);
    // The same for the MFT and register recall states, by the rule of the standard: a frame of
    // the other kind than the last digit is a new digit, whenever it comes.
    DigitResult takeLooseDigit(const Frame& frame, char& digit);
    Frame txScipOrder();
    Frame txScipIdentReq();
    Frame txScipRelease();
    void rxScipIdent(const Frame& frame);
    // The channel waits for a phone that is switched to it (or sends it the release).
    bool scipTargetState() const;

    // frames from the phone
    void process(const Frame& frame);
    void rxIdle(const Frame& frame);
    void rxIdent(const Frame& frame);
    void rxMoDialling(const Frame& frame);
    void rxMtIdent(const Frame& frame);
    void rxMtRinging(const Frame& frame);
    void completeDialling();
    void rxMtRelease(const Frame& frame);
    void setDspMode(DspMode mode);
    int renderTone(float* samples, int length, bool add);
    void onSupervisory(bool detected);
    bool callAudioUp() const;
    bool matchChannel(const Frame& frame) const;
    bool matchArea(const Frame& frame) const;
    bool matchSubscriber(const Frame& frame) const;
    static NmtSubscriber subscriberOf(const Frame& frame);
    bool isTrafficChannel() const;

    void release();
    void goIdle();
    void onTimer();

    ChannelConfig m_config;
    bool m_valid;
    TimeOfDay m_timeOfDay;
    FrameListener m_listener;
    NmtModem m_modem;
    uint64_t m_framesSent;

    NmtHost* m_host;
    std::unique_ptr<modemcellular::Timer> m_timer;
    State m_state;
    NmtTransaction* m_transaction;
    uint64_t m_rxFrameCount;        // frame periods since the seizure
    int m_txFrameCount;             // frames sent in the current state
    uint64_t m_callingSlot;         // idle calling channel: slots sent (even: idle frame, odd: carousel)

    double m_sampleRate;
    double m_deviationFactor;
    DspMode m_dspMode;
    double m_tonePhase;             // cycles of the dial tone
    std::string m_dialled;

    // speech path
    SupervisoryDetector m_superDetector;
    SupervisoryTone m_superTone;
    RxAudioChain m_rxAudio;
    TxAudioSource m_txAudio;
    int m_rxMute;                   // samples of received audio still to be muted
    bool m_alarmRunning;            // the supervisory tone was lost and the alarm timer runs
    int m_callerIdCount;            // 0: no number while ringing, 1: before the first ring, 2: after
    bool m_scipOrdered;             // new channel: the phone has been ordered here (first 3c sent)
    size_t m_digitCount;            // digits taken in the MFT or register recall state
    std::string m_recallDigits;     // digits of the register recall
    int m_newPower;                 // power level that is ordered
    bool m_haveLastDigit;           // MFT / register recall: a digit was taken
    bool m_lastDigitOdd;            // ... in a frame 14a
    bool m_mftIdleSeen;             // conversation: an idle frame of the phone came since the last MFT state
    bool m_autoanswerGo;            // message call: the idle frames after the identity are out, the order goes out
    bool m_autoanswerAcked;         // message call: the phone answered while the order went out
    int m_mwiLead;                  // idle frames before the indicators
    bool m_mwiAcked;                // the phone cleared / acknowledged already: nothing to wait for after the sequence
};

} // namespace modemnmt

#endif // MODEMNMT_NMTCHANNEL_H_
