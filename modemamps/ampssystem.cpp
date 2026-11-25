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

#include "ampssystem.h"

#include <algorithm>

#include "ampsfvc.h"
#include "ampsmessages.h"
#include "ampstrace.h"

namespace modemamps
{

const char *ampsModeName(AmpsMode mode)
{
    switch (mode)
    {
    case AmpsMode::Off: return "off";
    case AmpsMode::Control: return "control";
    case AmpsMode::Audio: return "audio";
    case AmpsMode::Silence: return "silence";
    case AmpsMode::Frame: return "frame";
    }
    return "";
}

std::string AmpsSubscriber::number() const
{
    return ReccAssembler::min2Digits(min2) + ReccAssembler::min1Digits(min1);
}

const char *AmpsSystem::stateName(State state)
{
    switch (state)
    {
    case State::RegisterAck: return "register ack";
    case State::RegisterAckSend: return "register ack sending";
    case State::Reorder: return "reorder";
    case State::ReorderSend: return "reorder sending";
    case State::MoAssign: return "MO assignment";
    case State::MoAssignSend: return "MO assignment sending";
    case State::MoAssignConfirm: return "MO assignment wait confirm";
    case State::Page: return "page";
    case State::PageSend: return "page sending";
    case State::PageReply: return "page wait reply";
    case State::Reject: return "reject";
    case State::RejectSend: return "reject sending";
    case State::MtAssign: return "MT assignment";
    case State::MtAssignSend: return "MT assignment sending";
    case State::MtAssignConfirm: return "MT assignment wait confirm";
    case State::MtAlert: return "MT alert";
    case State::MtAlertSend: return "MT alert sending";
    case State::MtAlertConfirm: return "MT alert wait confirm";
    case State::MtAnswerWait: return "MT answer wait";
    case State::Call: return "call";
    case State::Release: return "release";
    case State::ReleaseSend: return "release sending";
    case State::Order: return "order";
    case State::OrderSend: return "order sending";
    }
    return "";
}

// What a timer of a transaction waits for
enum class TimerKind
{
    None,
    Sat,            // the SAT of the phone on the new voice channel (SAT_TO1)
    Fade,           // the SAT lost or wrong in a call (SAT_TO2)
    StRelease,      // the signalling tone lasting: the phone releases (ST_RELEASE_TIME)
    Release,        // the phone confirming our release (RELEASE_TIMER)
    Page,           // the page response (PAGE_TO1, PAGE_TO2)
    Alert,          // the phone's tone after the alert order (ALERT_TO)
    Answer          // the phone ringing, its answer (ANSWER_TO)
};

struct AmpsSystem::Transaction
{
    Transaction(modemcellular::TimerService& timers, AmpsSystem& system) :
        timer(timers, [this, &system]() { system.timeout(this); }),
        flashTimer(timers, [this, &system]() { system.askCalledAddress(this); })
    {
    }

    AmpsSubscriber subscriber;
    State state = State::RegisterAck;
    int channel = -1;
    int voiceChannel = -1;
    uint8_t msgType = 0;
    uint8_t ordq = 0;
    uint8_t order = 0;
    modemcellular::Timer timer;
    TimerKind kind = TimerKind::None;
    double stStart = -1.0;              // the signalling tone came on, -1 not on
    bool mt = false;                    // a call to the phone, not answered yet
    int pageTry = 1;
    int alertTry = 1;
    std::string callerId;               // of a call to the phone
    std::vector<uint64_t> fvcWords;     // the words of an FVC message still to go (alert with info)
    std::vector<uint64_t> fvcOrders;    // orders of the tester to go on the FVC (audit, power, message waiting)
    AmpsMode afterOrders = AmpsMode::Off;   // the mode of the voice channel before them, Off none
    modemcellular::Timer flashTimer;    // a flash: the called address asked for after it (osmocom 1 s), then its window
    double flashEnd = -1.0;             // when the last flash ended (its tone off), -1 none
    bool calledAddressAsked = false;    // Send Called-Address sent, the phone's answer awaited
};

AmpsSystem::AmpsSystem(const modemcellular::Clock& clock, const AmpsSystemConfig& config) :
    m_clock(clock),
    m_config(config),
    m_timers(clock)
{
    for (const AmpsChannelConfig& c : config.channels)
    {
        Channel ch;
        ch.config = c;
        ch.mode = (c.type == AmpsChannelType::Voice) ? AmpsMode::Off : AmpsMode::Control;
        m_channels.push_back(ch);
    }
}

AmpsSystem::~AmpsSystem() = default;

void AmpsSystem::log(const std::string& text)
{
    if (m_log) {
        m_log(text);
    }
}

AmpsSystem::Transaction* AmpsSystem::create(const AmpsSubscriber& subscriber, State state, int channel)
{
    m_transactions.push_back(std::make_unique<Transaction>(m_timers, *this));
    Transaction* t = m_transactions.back().get();
    t->subscriber = subscriber;
    t->state = state;
    t->channel = channel;
    log(subscriber.number() + ": " + stateName(state));
    return t;
}

void AmpsSystem::destroy(Transaction* transaction)
{
    const int vc = transaction->voiceChannel;

    // a voice channel keyed for the designation that the phone never came to
    if (valid(vc) && !m_channels[vc].busy && m_channels[vc].mode == AmpsMode::Silence) {
        m_channels[vc].mode = AmpsMode::Off;
    }
    m_transactions.remove_if([transaction](const std::unique_ptr<Transaction>& t) { return t.get() == transaction; });
}

void AmpsSystem::goIdle(int channel)
{
    if (!valid(channel)) {
        return;
    }
    Channel& c = m_channels[channel];
    c.busy = false;
    c.sat = -1;
    c.mode = (c.config.type == AmpsChannelType::Voice) ? AmpsMode::Off : AmpsMode::Control;
}

void AmpsSystem::setState(Transaction* transaction, State state)
{
    transaction->state = state;
    log(transaction->subscriber.number() + ": " + stateName(state));
}

AmpsSystem::Transaction* AmpsSystem::transactionOf(uint32_t min1, uint16_t min2)
{
    for (auto& t : m_transactions)
    {
        if (t->subscriber.min1 == min1 && t->subscriber.min2 == min2) {
            return t.get();
        }
    }
    return nullptr;
}

AmpsSystem::Transaction* AmpsSystem::transactionOn(int channel)
{
    for (auto& t : m_transactions)
    {
        if (t->channel == channel) {
            return t.get();
        }
    }
    return nullptr;
}

// osmocom's search_free_vc: the first idle voice channel
int AmpsSystem::freeVoiceChannel() const
{
    for (size_t i = 0; i < m_channels.size(); i++)
    {
        const Channel& c = m_channels[i];
        if (c.config.type == AmpsChannelType::Voice && !c.busy && c.mode == AmpsMode::Off) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void AmpsSystem::reorder(const AmpsSubscriber& subscriber, int channel)
{
    create(subscriber, State::Reorder, channel);
}

void AmpsSystem::receivedRecc(int channel, const ReccMessage& m)
{
    if (!valid(channel)) {
        return;
    }
    AmpsSubscriber subscriber;
    subscriber.min1 = m.min1;
    subscriber.min2 = m.min2;
    subscriber.esn = m.esn;
    subscriber.scm = m.scm;

    const bool registration = m.order == kOrderRegistration && ((m.msgType == 0 && m.ordq <= 3) || (m.msgType == 1 && m.ordq == 3));
    const bool origination = m.t == 1 && m.order == kOrderPage && m.ordq == 0 && m.msgType == 0;
    const bool pageResponse = m.t == 0 && m.order == kOrderPage && m.ordq == 0 && m.msgType == 0;

    if (pageResponse)
    {
        Transaction* t = transactionOf(m.min1, m.min2);
        if (!t || (t->state != State::Page && t->state != State::PageSend && t->state != State::PageReply))
        {
            // osmocom: a page response nobody paged for is rejected
            log(subscriber.number() + ": page response without a page");
            if (!t) {
                create(subscriber, State::Reject, channel);
            }
            return;
        }
        t->timer.stop();
        t->kind = TimerKind::None;
        t->subscriber.esn = m.esn;
        t->subscriber.scm = m.scm;
        t->channel = channel;
        const int vc = freeVoiceChannel();
        if (vc < 0)
        {
            log(subscriber.number() + ": page response, no voice channel free");
            callFailed(t, modemcellular::Cause::NoChannel);
            setState(t, State::Reject);
            return;
        }
        t->voiceChannel = vc;
        setState(t, State::MtAssign);
        return;
    }

    if (registration)
    {
        // a transaction of the phone ends (osmocom: create_transaction destroys it, its channel idle)
        if (Transaction* old = transactionOf(m.min1, m.min2))
        {
            const int on = old->channel;
            const bool voice = valid(on) && m_channels[on].config.type == AmpsChannelType::Voice;
            destroy(old);
            if (voice) {
                goIdle(on);
            }
        }
        Transaction* t = create(subscriber, State::RegisterAck, channel);
        t->msgType = m.msgType;
        t->ordq = m.ordq;
        t->order = m.order;
        if (m.msgType == 1) {
            m_registered.erase(subscriber.number());
        } else {
            m_registered[subscriber.number()] = subscriber;
        }
        if (m_registrationHandler) {
            m_registrationHandler(subscriber, m.msgType == 1);
        }
        return;
    }

    if (origination)
    {
        if (transactionOf(m.min1, m.min2))
        {
            log(subscriber.number() + ": origination while a transaction is going on, ignored");
            return;
        }
        const int vc = freeVoiceChannel();
        if (vc < 0)
        {
            log(subscriber.number() + ": call to " + m.dialled + ", no voice channel free");
            reorder(subscriber, channel);
            return;
        }
        if (!m_callHandler || !m_callHandler(subscriber, m.dialled))
        {
            log(subscriber.number() + ": call to " + m.dialled + " refused");
            reorder(subscriber, channel);
            return;
        }
        log(subscriber.number() + ": call to " + m.dialled);
        Transaction* t = create(subscriber, State::MoAssign, channel);
        t->voiceChannel = vc;
        return;
    }

    // the order confirmation of an idle phone (osmocom: the audit's shown only)
    if (m.t == 0 && (m.order == kOrderAudit || m.order == kOrderMessageWaiting))
    {
        log(subscriber.number() + ": " + (m.order == kOrderMessageWaiting ? std::string("Message Waiting") : ampsOrderName(m.order, m.ordq, 0))
            + " confirmed");
        return;
    }

    log(subscriber.number() + ": unsupported RECC message, ORDER " + std::to_string(m.order) + " ORDQ "
        + std::to_string(m.ordq) + " MSG_TYPE " + std::to_string(m.msgType));
}

bool AmpsSystem::nextFocc(int channel, FoccMessage& message)
{
    const SystemInfo& si = m_config.si;

    for (;;)
    {
        Transaction* t = transactionOn(channel);
        if (!t) {
            return false;
        }
        const AmpsSubscriber& s = t->subscriber;

        switch (t->state)
        {
        case State::RegisterAck:
            setState(t, State::RegisterAckSend);
            message = FoccMessage::order(si.dcc, s.min1, s.min2, t->order, t->ordq, t->msgType);
            return true;
        case State::Reorder:
            setState(t, State::ReorderSend);
            message = FoccMessage::order(si.dcc, s.min1, s.min2, kOrderReorder, 0, 0);
            return true;
        case State::Reject:
            setState(t, State::RejectSend);
            message = FoccMessage::order(si.dcc, s.min1, s.min2, kOrderRelease, 0, 0);
            return true;
        case State::Order:
            // osmocom TRANS_AUDIT / TRANS_MWI: sent, the transaction is done (the phone's
            // confirmation on the RECC is only shown)
            setState(t, State::OrderSend);
            message = FoccMessage::order(si.dcc, s.min1, s.min2, t->order, t->ordq, t->msgType);
            return true;
        case State::RegisterAckSend:
        case State::ReorderSend:
        case State::RejectSend:
        case State::OrderSend:
            // sent: the next one
            destroy(t);
            continue;
        case State::Page:
            setState(t, State::PageSend);
            message = FoccMessage::order(si.dcc, s.min1, s.min2, kOrderPage, 0, 0);
            return true;
        case State::PageSend:
            // sent: the time for the response (osmocom: 8 s, 4 s for the second page), and the
            // round trip of the radio
            setState(t, State::PageReply);
            t->kind = TimerKind::Page;
            t->timer.start((t->pageTry == 1 ? kPageTimeout1 : kPageTimeout2) + m_config.roundTrip);
            return false;
        case State::MoAssign:
        case State::MtAssign:
        {
            // the voice channel is keyed (SAT, no speech) while its designation goes out
            Channel& vc = m_channels[t->voiceChannel];
            if (!vc.busy && vc.mode == AmpsMode::Off) {
                vc.mode = AmpsMode::Silence;
            }
            setState(t, t->state == State::MoAssign ? State::MoAssignSend : State::MtAssignSend);
            message = FoccMessage::voiceChannel(si.dcc, s.min1, s.min2, static_cast<uint8_t>(vc.config.scc), si.vmac,
                static_cast<uint16_t>(vc.config.channel));
            return true;
        }
        case State::MoAssignSend:
        case State::MtAssignSend:
            assignVoiceChannel(t);
            return false;
        default:
            return false;
        }
    }
}

// osmocom's assign_voice_channel: the transaction goes to the voice channel and waits for the SAT
void AmpsSystem::assignVoiceChannel(Transaction* t)
{
    const int vc = t->voiceChannel;

    if (!valid(vc) || m_channels[vc].busy)
    {
        log(t->subscriber.number() + ": the voice channel is not free any more");
        if (t->mt) {
            callFailed(t, modemcellular::Cause::NoChannel);
        } else if (m_clearHandler) {
            m_clearHandler(t->subscriber);
        }
        destroy(t);
        return;
    }
    Channel& c = m_channels[vc];
    c.busy = true;
    c.sat = -1;
    c.mode = AmpsMode::Silence;
    t->channel = vc;
    t->kind = TimerKind::Sat;
    t->timer.start(kSatTimeout1 + m_config.roundTrip);
    setState(t, t->mt ? State::MtAssignConfirm : State::MoAssignConfirm);
}

void AmpsSystem::callFailed(Transaction* t, modemcellular::Cause cause)
{
    if (t->mt)
    {
        t->mt = false;
        if (m_failed) {
            m_failed(t->subscriber, cause);
        }
    }
}

bool AmpsSystem::startCall(const std::string& number, modemcellular::Cause* cause, const std::string& callerId)
{
    const auto refuse = [cause](modemcellular::Cause c) {
        if (cause) {
            *cause = c;
        }
        return false;
    };

    if (number.size() != 10 || number.find_first_not_of("0123456789") != std::string::npos) {
        return refuse(modemcellular::Cause::InvalidNumber);
    }
    AmpsSubscriber subscriber;
    subscriber.min2 = ReccAssembler::min2Of(number.substr(0, 3));
    subscriber.min1 = ReccAssembler::min1Of(number.substr(3));
    const auto it = m_registered.find(number);
    if (it != m_registered.end()) {
        subscriber = it->second;
    }

    // osmocom's call_down_setup: busy, a voice channel, the paging channel
    if (transactionOf(subscriber.min1, subscriber.min2)) {
        return refuse(modemcellular::Cause::Busy);
    }
    if (freeVoiceChannel() < 0) {
        return refuse(modemcellular::Cause::NoChannel);
    }
    int pc = -1;
    for (size_t i = 0; i < m_channels.size() && pc < 0; i++)
    {
        if (m_channels[i].config.type != AmpsChannelType::Voice) {
            pc = static_cast<int>(i);
        }
    }
    if (pc < 0) {
        return refuse(modemcellular::Cause::OutOfOrder);
    }
    Transaction* t = create(subscriber, State::Page, pc);
    t->mt = true;
    t->pageTry = 1;
    t->callerId = callerId;
    return true;
}

// osmocom's amps_release: the release order on the FVC until the phone confirms it with its tone
void AmpsSystem::release(Transaction* t, bool tellNetwork)
{
    // a call to the phone that was not answered fails with its cause; an answered call is cleared
    if (tellNetwork && t->mt) {
        callFailed(t, modemcellular::Cause::TempFail);
    } else if (tellNetwork && m_clearHandler) {
        m_clearHandler(t->subscriber);
    }
    t->mt = false;
    t->kind = TimerKind::Release;
    t->timer.start(kReleaseTimeout + m_config.roundTrip);
    t->stStart = -1.0;
    setState(t, State::Release);
    if (valid(t->channel)) {
        m_channels[t->channel].mode = AmpsMode::Frame;
    }
}

void AmpsSystem::phoneReleased(Transaction* t)
{
    const int channel = t->channel;

    log(t->subscriber.number() + ": released by the phone");
    if (m_clearHandler) {
        m_clearHandler(t->subscriber);
    }
    destroy(t);
    goIdle(channel);
}

void AmpsSystem::timeout(Transaction* t)
{
    const TimerKind kind = t->kind;

    t->kind = TimerKind::None;
    switch (kind)
    {
    case TimerKind::Sat:
        log(t->subscriber.number() + ": no SAT from the phone on the voice channel");
        release(t, true);
        break;
    case TimerKind::Fade:
        // the SAT came back as it should: nothing
        if (valid(t->channel) && m_channels[t->channel].sat == m_channels[t->channel].config.scc) {
            break;
        }
        log(t->subscriber.number() + ": the SAT of the phone is gone");
        release(t, true);
        break;
    case TimerKind::StRelease:
        phoneReleased(t);
        break;
    case TimerKind::Release:
    {
        const int channel = t->channel;
        log(t->subscriber.number() + ": the phone did not confirm the release");
        destroy(t);
        goIdle(channel);
        break;
    }
    case TimerKind::Page:
        if (t->pageTry++ == kPageTries)
        {
            log(t->subscriber.number() + ": no page response");
            callFailed(t, modemcellular::Cause::OutOfOrder);
            destroy(t);
        }
        else {
            setState(t, State::Page);           // paged again
        }
        break;
    case TimerKind::Alert:
        if (t->alertTry++ == kAlertTries)
        {
            log(t->subscriber.number() + ": the phone does not ring");
            release(t, true);
        }
        else
        {
            setState(t, State::MtAlert);        // the alert again
            m_channels[t->channel].mode = AmpsMode::Frame;
        }
        break;
    case TimerKind::Answer:
        log(t->subscriber.number() + ": no answer");
        release(t, true);
        break;
    case TimerKind::None:
        break;
    }
}

void AmpsSystem::satChanged(int channel, int scc)
{
    if (!valid(channel)) {
        return;
    }
    Channel& c = m_channels[channel];
    c.sat = scc;

    Transaction* t = transactionOn(channel);
    if (!t || t->state == State::Release || t->state == State::ReleaseSend) {
        return;                     // the SAT does not count in a release (osmocom)
    }

    if (scc >= 0 && t->state == State::MoAssignConfirm)
    {
        t->timer.stop();
        t->kind = TimerKind::None;
        setState(t, State::Call);
        c.mode = AmpsMode::Audio;
        return;
    }
    if (scc >= 0 && t->state == State::MtAssignConfirm)
    {
        // the phone is on the voice channel: the alert order
        t->timer.stop();
        t->kind = TimerKind::None;
        t->alertTry = 1;
        setState(t, State::MtAlert);
        c.mode = AmpsMode::Frame;
        return;
    }
    if (t->state != State::Call) {
        return;
    }
    if (scc == c.config.scc)
    {
        // back: only the timer of the fade stops
        if (t->kind == TimerKind::Fade)
        {
            t->timer.stop();
            t->kind = TimerKind::None;
        }
        return;
    }
    // lost or another colour code: the fade timer, if no other timer runs
    if (t->kind == TimerKind::None)
    {
        t->kind = TimerKind::Fade;
        t->timer.start(kSatTimeout2);
    }
}

void AmpsSystem::signallingTone(int channel, bool on)
{
    if (!valid(channel)) {
        return;
    }
    Transaction* t = transactionOn(channel);
    if (!t) {
        return;
    }
    const double now = m_clock.now();

    if (on && (t->state == State::MtAlert || t->state == State::MtAlertSend || t->state == State::MtAlertConfirm))
    {
        // the phone rings: speech on, the answer awaited (osmocom)
        t->timer.stop();
        if (m_alerting) {
            m_alerting(t->subscriber);
        }
        m_channels[channel].mode = AmpsMode::Audio;
        setState(t, State::MtAnswerWait);
        t->kind = TimerKind::Answer;
        t->timer.start(kAnswerTimeout);
        sendOrders(t);              // orders given while the alert waited go on
        return;
    }
    if (t->state == State::MtAnswerWait)
    {
        // its tone stops with the SAT there: answered
        if (!on && m_channels[channel].sat >= 0)
        {
            t->timer.stop();
            t->kind = TimerKind::None;
            t->mt = false;
            setState(t, State::Call);
            if (m_answered) {
                m_answered(t->subscriber);
            }
        }
        return;
    }

    if (t->state == State::Call || t->state == State::MoAssignConfirm)
    {
        if (on)
        {
            if (m_channels[channel].sat < 0) {
                return;                         // osmocom: the tone counts with the SAT
            }
            t->stStart = now;
            t->kind = TimerKind::StRelease;
            t->timer.start(kStReleaseTime);
            return;
        }
        if (t->stStart < 0.0) {
            return;
        }
        const double duration = now - t->stStart;
        t->stStart = -1.0;
        if (t->kind == TimerKind::StRelease)
        {
            t->timer.stop();
            t->kind = TimerKind::None;
        }
        if (duration > kFlashMax)
        {
            phoneReleased(t);
            return;
        }
        if (duration >= kFlashMin)
        {
            // osmocom: the flash goes to the network; a second later the called address is asked
            // for (Send Called-Address, ORDER 8): the digits the user dialled before the flash
            log(t->subscriber.number() + ": flash");
            if (m_flashHandler) {
                m_flashHandler(t->subscriber, std::string());
            }
            if (t->state == State::Call)
            {
                t->flashEnd = now;
                t->calledAddressAsked = false;
                t->flashTimer.start(kCalledAddressDelay);
            }
        }
        sendOrders(t);              // the orders held while the tone was on
        return;
    }

    if (t->state == State::Release || t->state == State::ReleaseSend)
    {
        if (on)
        {
            t->stStart = now;
            return;
        }
        if (t->stStart >= 0.0 && now - t->stStart >= kFlashMax)
        {
            log(t->subscriber.number() + ": release confirmed by the phone");
            t->timer.stop();
            destroy(t);
            goIdle(channel);
        }
    }
}

void AmpsSystem::receivedRvcOrder(int channel, uint8_t order, uint8_t ordq)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    if (t && t->state == State::Call && order == kOrderRelease)
    {
        phoneReleased(t);
        return;
    }
    // the confirmation of an order of the tester (osmocom: shown only)
    if (t && (order == kOrderAudit || order == kOrderChangePower || order == kOrderMessageWaiting || order == kOrderFlashWithInfo
        || order == kOrderDisableDtmf)) {
        log(t->subscriber.number() + ": " + (order == kOrderMessageWaiting ? std::string("Message Waiting") : ampsOrderName(order, ordq, 0))
            + " confirmed");
    }
}

bool AmpsSystem::nextFvc(int channel, uint64_t& word)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    if (t && t->state == State::MtAlert)
    {
        const uint8_t pscc = static_cast<uint8_t>(m_channels[channel].config.scc);
        setState(t, State::MtAlertSend);
        // the first alert with the caller's number when the network sends it (osmocom: ORDER 17 on
        // the first try with a caller id)
        t->fvcWords.clear();
        if (m_config.sendCallerId && t->alertTry == 1 && !t->callerId.empty())
        {
            t->fvcWords = fvc::alertWithInfo(pscc, t->callerId);
            word = t->fvcWords.front();
            t->fvcWords.erase(t->fvcWords.begin());
            return true;
        }
        word = fvc::order(pscc, kOrderAlert, 0, 0);
        return true;
    }
    if (t && t->state == State::MtAlertSend && !t->fvcWords.empty())
    {
        word = t->fvcWords.front();
        t->fvcWords.erase(t->fvcWords.begin());
        return true;
    }
    // the tester's orders, in the call or while it rings (a release goes first); the rest waits when
    // the phone's tone comes on (a flash)
    if (t && t->state != State::Release && t->state != State::ReleaseSend && !t->fvcOrders.empty() && !toneOn(t))
    {
        word = t->fvcOrders.front();
        t->fvcOrders.erase(t->fvcOrders.begin());
        return true;
    }
    if (t && t->state == State::MtAlertSend)
    {
        // sent: the phone's tone awaited with SAT and silence (osmocom ALERT_TO, and the round trip)
        setState(t, State::MtAlertConfirm);
        t->kind = TimerKind::Alert;
        t->timer.start(kAlertTimeout + m_config.roundTrip);
        m_channels[channel].mode = AmpsMode::Silence;
        return false;
    }
    if (t && (t->state == State::Release || t->state == State::ReleaseSend))
    {
        // the release goes out again and again until the phone confirms it (osmocom)
        if (t->state == State::Release) {
            setState(t, State::ReleaseSend);
        }
        word = fvc::order(static_cast<uint8_t>(m_channels[channel].config.scc), kOrderRelease, 0, 0);
        return true;
    }
    // nothing to send: back to the speech (after the tester's orders: to the mode before them, the
    // ringing phone's included)
    if (valid(channel) && m_channels[channel].mode == AmpsMode::Frame)
    {
        if (t && t->afterOrders != AmpsMode::Off)
        {
            m_channels[channel].mode = t->afterOrders;
            t->afterOrders = AmpsMode::Off;
        }
        else
        {
            m_channels[channel].mode = (t && t->state == State::Call) ? AmpsMode::Audio : AmpsMode::Silence;
        }
    }
    return false;
}

void AmpsSystem::audioFromMobile(int channel, const float* samples, int count)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    if (t && t->state == State::Call)
    {
        if (m_tapFromMobile) {
            m_tapFromMobile(t->subscriber, samples, count);
        }
        if (m_audioFromMobile) {
            m_audioFromMobile(t->subscriber, samples, count);
        }
    }
}

void AmpsSystem::audioToMobile(int channel, float* samples, int count)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    std::fill(samples, samples + count, 0.0f);
    if (t && t->state == State::Call && m_channels[channel].mode == AmpsMode::Audio && m_audioToMobile)
    {
        m_audioToMobile(t->subscriber, samples, count);
        if (m_tapToMobile) {
            m_tapToMobile(t->subscriber, samples, count);
        }
    }
}

AmpsMode AmpsSystem::mode(int channel) const
{
    return valid(channel) ? m_channels[channel].mode : AmpsMode::Off;
}

bool AmpsSystem::releaseCall(const std::string& number)
{
    for (auto& p : m_transactions)
    {
        Transaction* t = p.get();
        if (t->subscriber.number() != number) {
            continue;
        }
        t->mt = false;                      // the network knows
        const bool onVoice = valid(t->channel) && m_channels[t->channel].config.type == AmpsChannelType::Voice;
        if (t->state == State::Release || t->state == State::ReleaseSend) {
            return true;
        }
        if (onVoice) {
            release(t, false);
        } else {
            destroy(t);
        }
        return true;
    }
    return false;
}

AmpsSystem::OrderResult AmpsSystem::order(const std::string& number, uint8_t order, uint8_t ordq, uint8_t msgType, OrderWhere where,
    const std::vector<uint64_t>& message)
{
    if (number.size() != 10 || number.find_first_not_of("0123456789") != std::string::npos) {
        return OrderResult::Invalid;
    }
    const uint32_t min1 = ReccAssembler::min1Of(number.substr(3));
    const uint16_t min2 = ReccAssembler::min2Of(number.substr(0, 3));
    Transaction* t = transactionOf(min1, min2);
    const std::string name = ampsOrderName(order, ordq, msgType);

    if (!t)
    {
        if (where != OrderWhere::Anywhere) {
            return OrderResult::NoCall;
        }
        int pc = -1;
        for (size_t i = 0; i < m_channels.size() && pc < 0; i++)
        {
            if (m_channels[i].config.type != AmpsChannelType::Voice) {
                pc = static_cast<int>(i);
            }
        }
        if (pc < 0) {
            return OrderResult::Busy;
        }
        AmpsSubscriber subscriber;
        subscriber.min1 = min1;
        subscriber.min2 = min2;
        Transaction* o = create(subscriber, State::Order, pc);
        o->order = order;
        o->ordq = ordq;
        o->msgType = msgType;
        log(number + ": " + name + " on the control channel");
        return OrderResult::Sent;
    }
    // on the voice channel, as the phone's tasks take them (553-A 2.6.4.3.1 Waiting for Order: the
    // alert sent, not confirmed yet; 2.6.4.3.2 Waiting for Answer: ringing; 2.6.4.4 Conversation)
    const bool inCall = t->state == State::Call;
    const bool ringing = t->state == State::MtAnswerWait;
    const bool alerting = t->state == State::MtAlert || t->state == State::MtAlertSend || t->state == State::MtAlertConfirm;
    if (alerting && where == OrderWhere::Ringing) {
        return OrderResult::Busy;       // the phone ignores it before it rings: when it rings
    }
    if (!inCall && where == OrderWhere::Conversation) {
        return OrderResult::NoCall;     // the phone ignores it before the conversation
    }
    if (!inCall && !ringing && !alerting) {
        return OrderResult::Busy;       // paging, assignment, registration, release: later
    }
    if (message.empty()) {
        t->fvcOrders.push_back(fvc::order(static_cast<uint8_t>(m_channels[t->channel].config.scc), order, ordq, msgType));
    } else {
        t->fvcOrders.insert(t->fvcOrders.end(), message.begin(), message.end());
    }
    log(number + ": " + name + " on the voice channel" + (toneOn(t) ? " when the phone's signalling tone is off" : ""));
    sendOrders(t);
    return OrderResult::Sent;
}

// The phone in its conversation sends its signalling tone (a flash, or a release beginning). An
// order it takes then cuts a flash short and makes it invalid (553-A 2.6.4.4): orders wait. While it
// rings the tone is on all the time; that does not count.
bool AmpsSystem::toneOn(const Transaction* t) const
{
    return t->state == State::Call && t->stStart >= 0.0;
}

// The orders waiting on the FVC go out, unless the phone's tone is on
void AmpsSystem::sendOrders(Transaction* t)
{
    if (t->fvcOrders.empty() || toneOn(t) || !valid(t->channel)) {
        return;
    }
    if (m_channels[t->channel].mode != AmpsMode::Frame) {
        t->afterOrders = m_channels[t->channel].mode;
    }
    m_channels[t->channel].mode = AmpsMode::Frame;
}

// After a flash (osmocom flash_timer_callback): the Send Called-Address order; the phone answers
// with the digits on the RVC only within 10 s of the end of its last valid flash (553-A 2.6.4.4),
// here and back with the round trip of the radio. The timer then ends that window: no answer, no
// digits (none dialled, or the flash was cut short).
void AmpsSystem::askCalledAddress(Transaction* t)
{
    if (t->state != State::Call || !valid(t->channel)) {
        return;
    }
    if (t->calledAddressAsked)
    {
        t->calledAddressAsked = false;
        log(t->subscriber.number() + ": flash: no called address from the phone (no digits dialled, or the flash was cut short)");
        return;
    }
    t->fvcOrders.push_back(fvc::order(static_cast<uint8_t>(m_channels[t->channel].config.scc), kOrderSendCalledAddress, 0, 0));
    t->calledAddressAsked = true;
    sendOrders(t);
    const double until = t->flashEnd + kCalledAddressWindow + m_config.roundTrip;
    t->flashTimer.start(std::max(0.0, until - m_clock.now()));
}

void AmpsSystem::receivedRvcCalledAddress(int channel, const std::string& digits)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    if (!t || t->state != State::Call) {
        return;
    }
    // one not asked for, or after the window: shown with its digits (what real phones send is to
    // be seen on the air), not taken as a flash's
    if (!t->calledAddressAsked || m_clock.now() > t->flashEnd + kCalledAddressWindow + m_config.roundTrip)
    {
        log(t->subscriber.number() + ": called address " + (digits.empty() ? std::string("without digits") : digits)
            + " not asked for (no Send Called-Address in the 10 s after a flash), not acted on");
        return;
    }
    t->calledAddressAsked = false;
    t->flashTimer.stop();
    log(t->subscriber.number() + ": flash with " + (digits.empty() ? std::string("no digits") : digits));
    if (m_flashHandler && !digits.empty()) {
        m_flashHandler(t->subscriber, digits);
    }
}

AmpsSystem::OrderResult AmpsSystem::disableDtmf(const std::string& number)
{
    return order(number, kOrderDisableDtmf, 0, 0, OrderWhere::Conversation);
}

AmpsSystem::OrderResult AmpsSystem::audit(const std::string& number)
{
    return order(number, kOrderAudit, 0, 0, OrderWhere::Anywhere);
}

AmpsSystem::OrderResult AmpsSystem::messageWaiting(const std::string& number, int type, int count)
{
    if (type < 0 || type > 2 || count < 0 || count > 31) {
        return OrderResult::Invalid;
    }
    return order(number, kOrderMessageWaiting, static_cast<uint8_t>(type), static_cast<uint8_t>(count), OrderWhere::Anywhere);
}

AmpsSystem::OrderResult AmpsSystem::changePower(const std::string& number, int level)
{
    if (level < 0 || level > 7) {
        return OrderResult::Invalid;
    }
    return order(number, kOrderChangePower, static_cast<uint8_t>(level), 0, OrderWhere::VoiceChannel);
}

AmpsSystem::OrderResult AmpsSystem::flashWithInfo(const std::string& number, const std::string& text)
{
    if (text.empty() || text.size() > fvc::kFlashInfoMax) {
        return OrderResult::Invalid;
    }
    for (char c : text)
    {
        if (c < 0x20 || c > 0x7e) {
            return OrderResult::Invalid;        // ASCII only (ANSI X3.4)
        }
    }
    // the words are made for the voice channel of the call; its SAT colour code is in word 1
    const uint32_t min1 = number.size() == 10 ? ReccAssembler::min1Of(number.substr(3)) : 0;
    const uint16_t min2 = number.size() == 10 ? ReccAssembler::min2Of(number.substr(0, 3)) : 0;
    const Transaction* t = number.size() == 10 ? transactionOf(min1, min2) : nullptr;
    const uint8_t pscc = (t && valid(t->channel)) ? static_cast<uint8_t>(m_channels[t->channel].config.scc) : 0;
    return order(number, kOrderFlashWithInfo, 0, 0, OrderWhere::Ringing, fvc::flashWithInfo(pscc, text));
}

std::vector<AmpsSystem::TransactionInfo> AmpsSystem::transactions() const
{
    std::vector<TransactionInfo> list;

    for (const auto& t : m_transactions) {
        list.push_back({t->subscriber, t->state, t->channel, t->voiceChannel});
    }
    return list;
}

} // namespace modemamps
