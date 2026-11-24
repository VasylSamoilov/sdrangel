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
    case State::Call: return "call";
    case State::Release: return "release";
    case State::ReleaseSend: return "release sending";
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
    Release         // the phone confirming our release (RELEASE_TIMER)
};

struct AmpsSystem::Transaction
{
    Transaction(modemcellular::TimerService& timers, AmpsSystem& system) :
        timer(timers, [this, &system]() { system.timeout(this); })
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
        case State::RegisterAckSend:
        case State::ReorderSend:
            // sent: the next one
            destroy(t);
            continue;
        case State::MoAssign:
        {
            // the voice channel is keyed (SAT, no speech) while its designation goes out
            Channel& vc = m_channels[t->voiceChannel];
            if (!vc.busy && vc.mode == AmpsMode::Off) {
                vc.mode = AmpsMode::Silence;
            }
            setState(t, State::MoAssignSend);
            message = FoccMessage::voiceChannel(si.dcc, s.min1, s.min2, static_cast<uint8_t>(vc.config.scc), si.vmac,
                static_cast<uint16_t>(vc.config.channel));
            return true;
        }
        case State::MoAssignSend:
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
        if (m_clearHandler) {
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
    setState(t, State::MoAssignConfirm);
}

// osmocom's amps_release: the release order on the FVC until the phone confirms it with its tone
void AmpsSystem::release(Transaction* t, bool tellNetwork)
{
    if (tellNetwork && m_clearHandler) {
        m_clearHandler(t->subscriber);
    }
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
        if (duration > kFlashMax) {
            phoneReleased(t);
        } else if (duration >= kFlashMin) {
            log(t->subscriber.number() + ": flash");
        }
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

void AmpsSystem::receivedRvcOrder(int channel, uint8_t order, uint8_t)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    if (t && t->state == State::Call && order == kOrderRelease) {
        phoneReleased(t);
    }
}

bool AmpsSystem::nextFvc(int channel, uint64_t& word)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    if (t && (t->state == State::Release || t->state == State::ReleaseSend))
    {
        // the release goes out again and again until the phone confirms it (osmocom)
        if (t->state == State::Release) {
            setState(t, State::ReleaseSend);
        }
        word = fvc::order(static_cast<uint8_t>(m_channels[channel].config.scc), kOrderRelease, 0, 0);
        return true;
    }
    // nothing to send: back to the speech
    if (valid(channel) && m_channels[channel].mode == AmpsMode::Frame) {
        m_channels[channel].mode = (t && t->state == State::Call) ? AmpsMode::Audio : AmpsMode::Silence;
    }
    return false;
}

void AmpsSystem::audioFromMobile(int channel, const float* samples, int count)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    if (t && t->state == State::Call && m_audioFromMobile) {
        m_audioFromMobile(t->subscriber, samples, count);
    }
}

void AmpsSystem::audioToMobile(int channel, float* samples, int count)
{
    Transaction* t = valid(channel) ? transactionOn(channel) : nullptr;

    std::fill(samples, samples + count, 0.0f);
    if (t && t->state == State::Call && m_channels[channel].mode == AmpsMode::Audio && m_audioToMobile) {
        m_audioToMobile(t->subscriber, samples, count);
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
        if (t->state == State::Call || t->state == State::MoAssignConfirm) {
            release(t, false);
        } else if (t->state != State::Release && t->state != State::ReleaseSend) {
            destroy(t);
        }
        return true;
    }
    return false;
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
