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

#include "nmtsystem.h"

#include <algorithm>

namespace modemnmt
{

namespace
{

std::string subscriberKey(char country, const std::string& number)
{
    return std::string(1, country) + number;
}

} // namespace

NmtSystem::NmtSystem(const modemcellular::Clock& clock) :
    m_timers(clock)
{
}

NmtChannel& NmtSystem::addChannel(const ChannelConfig& config, double sampleRate, double deviationFactor)
{
    m_channels.push_back(std::make_unique<NmtChannel>(config, sampleRate, deviationFactor));
    m_channels.back()->attach(*this);

    return *m_channels.back();
}

bool NmtSystem::isRegistered(char country, const std::string& number) const
{
    return m_registered.count(subscriberKey(country, number)) != 0;
}

const NmtSubscriber* NmtSystem::registeredSubscriber(char country, const std::string& number) const
{
    const auto it = m_registered.find(subscriberKey(country, number));

    return it == m_registered.end() ? nullptr : &it->second;
}

void NmtSystem::forEachRegistered(const std::function<void(const NmtSubscriber&)>& function) const
{
    // over a copy: the function may roam phones in or out
    const std::map<std::string, NmtSubscriber> registered = m_registered;

    for (const auto& entry : registered) {
        function(entry.second);
    }
}

NmtTransaction* NmtSystem::createTransaction(const NmtSubscriber& subscriber, NmtChannel& channel)
{
    // one transaction per subscriber
    for (const NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country == subscriber.country
         && transaction.subscriber.number == subscriber.number) {
            return nullptr;
        }
    }

    m_transactions.emplace_back();
    m_transactions.back().subscriber = subscriber;
    m_transactions.back().channels.push_back(&channel);

    return &m_transactions.back();
}

void NmtSystem::destroyTransaction(NmtTransaction* transaction)
{
    for (auto it = m_transactions.begin(); it != m_transactions.end(); ++it)
    {
        if (&*it != transaction) {
            continue;
        }

        // the channels drop their pointer before the transaction goes away
        const std::vector<NmtChannel*> channels = it->channels;
        for (NmtChannel* channel : channels) {
            channel->transactionEnded();
        }
        m_transactions.erase(it);
        return;
    }
}

void NmtSystem::detachChannel(NmtChannel& channel, NmtTransaction* transaction)
{
    for (auto it = m_transactions.begin(); it != m_transactions.end(); ++it)
    {
        if (&*it != transaction) {
            continue;
        }

        std::vector<NmtChannel*>& channels = it->channels;
        for (auto c = channels.begin(); c != channels.end(); ++c)
        {
            if (*c == &channel) {
                channels.erase(c);
                break;
            }
        }
        if (channels.empty())
        {
            // a call to a phone that is lost with its last channel: the exchange hears it
            if (it->mobileTerminated && it->callUp)
            {
                it->callUp = false;
                callFailed(*it, modemcellular::Cause::TempFail);
            }
            m_transactions.erase(it);
        }
        return;
    }
}

bool NmtSystem::callDialled(NmtTransaction& transaction, const std::string& dialled)
{
    if (!m_callHandler) {
        return false;
    }

    // a copy: the handler may end the transaction
    const NmtSubscriber subscriber = transaction.subscriber;

    return m_callHandler(subscriber, dialled);
}

void NmtSystem::callCleared(NmtTransaction& transaction)
{
    if (!m_clearHandler) {
        return;
    }

    const NmtSubscriber subscriber = transaction.subscriber;

    m_clearHandler(subscriber);
}

namespace
{

const int kPageTries = 3;
const double kPagingTimeout = 1.0;

// Battery saving (NMT Doc 450-1 para 4.3.3.12.3): does the phone whose last digit is x6 belong to
// the group that H7 addresses?
bool groupContains(int h7, int x6)
{
    switch (h7)
    {
    case 1:
        return (x6 & 1) == 1;               // 1, 3, 5, 7, 9
    case 2:
        return (x6 & 1) == 0;               // 2, 4, 6, 8, 0
    case 3:
        return x6 == 1 || x6 == 2;
    case 4:
        return x6 == 3 || x6 == 4;
    case 5:
        return x6 == 5 || x6 == 6;
    case 6:
        return x6 == 7 || x6 == 8;
    case 7:
        return x6 == 9 || x6 == 0;
    case 8:
        return true;                        // all phones
    default:
        return false;
    }
}

// Frames between the wake-ups of the phones for the H3 code (para 4.3.3.12.4).
int batteryPeriodFrames(int h3)
{
    static const int table[10] = {0, 24, 40, 56, 72, 88, 112, 168, 224, 280};

    return (h3 >= 1 && h3 <= 9) ? table[h3] : 0;
}

} // namespace

bool NmtSystem::canCall() const
{
    for (const auto& channel : m_channels)
    {
        if (channel->valid() && channel->isCallingChannel()) {
            return true;
        }
    }

    return false;
}

bool NmtSystem::startCall(char country, const std::string& number, const std::string& callerId,
                          modemcellular::NumberType callerType)
{
    NmtSubscriber subscriber;

    subscriber.country = country;
    subscriber.number = number;

    // nothing to page with
    if (!canCall()) {
        return false;
    }

    // a phone has one transaction
    for (const NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country == country && transaction.subscriber.number == number) {
            return false;
        }
    }

    m_transactions.emplace_back();
    NmtTransaction& transaction = m_transactions.back();
    NmtTransaction* const pointer = &transaction;

    transaction.subscriber = subscriber;
    transaction.mobileTerminated = true;
    transaction.paging = true;
    transaction.callUp = true;
    // the phone does not add the '+' of an international number
    transaction.callerId = (callerType == modemcellular::NumberType::International && (callerId.empty() || callerId[0] != '+'))
        ? "+" + callerId : callerId;
    transaction.callerType = callerType;
    transaction.timer = std::make_unique<modemcellular::Timer>(m_timers, [this, pointer]() { pagingTimeout(*pointer); });
    page(transaction, 1);

    return true;
}

// Pages the phone on the calling channels that are idle. With battery saving on a calling channel
// the phone may be asleep: the first page waits one period of the group the phone is in.
void NmtSystem::page(NmtTransaction& transaction, int tryNo)
{
    if (tryNo == 1 && !transaction.pageWaited)
    {
        const char last = transaction.subscriber.number.empty() ? '\0' : transaction.subscriber.number.back();
        const int x6 = (last >= '0' && last <= '9') ? last - '0' : -1;

        for (const auto& channel : m_channels)
        {
            const ChannelConfig& c = channel->config();
            if (!channel->isCallingChannel() || c.batteryPeriod <= 0 || x6 < 0 || !groupContains(c.batteryGroup, x6)) {
                continue;
            }

            // one frame is 138.33 ms; whole seconds, at least one
            const int frames = batteryPeriodFrames(c.batteryPeriod);
            const int seconds = std::max((frames * 138333 + 999999) / 1000000, 1);

            transaction.pageDeferred = true;
            transaction.pageWaited = true;
            transaction.pageTry = tryNo;
            transaction.timer->start(seconds);
            return;
        }
    }

    transaction.pageTry = tryNo;
    transaction.timer->start(kPagingTimeout);

    // all idle calling channels, and those that paged the phone before
    for (const auto& channel : m_channels)
    {
        if (!channel->valid() || !channel->isCallingChannel()) {
            continue;
        }
        const bool paged = std::find(transaction.channels.begin(), transaction.channels.end(), channel.get()) != transaction.channels.end();
        if (channel->state() != NmtChannel::State::Idle && !paged) {
            continue;
        }
        if (!paged) {
            transaction.channels.push_back(channel.get());
        }
        channel->startPaging(transaction);
    }
}

void NmtSystem::pagingTimeout(NmtTransaction& transaction)
{
    // the wake-up window of battery saving has come: now the page, this was no failed attempt
    if (transaction.pageDeferred)
    {
        transaction.pageDeferred = false;
        page(transaction, 1);
        return;
    }

    if (transaction.pageTry >= kPageTries)
    {
        callFailed(transaction, modemcellular::Cause::OutOfOrder);
        destroyTransaction(&transaction);
        return;
    }

    page(transaction, transaction.pageTry + 1);
}

// A traffic channel for a call that is on the calling channel `from`.
NmtChannel* NmtSystem::findTrafficChannel(NmtChannel& from)
{
    NmtChannel* combined = nullptr;

    for (const auto& channel : m_channels)
    {
        // the calling channel counts as free: the call is on it
        if (channel.get() != &from && (!channel->valid() || channel->state() != NmtChannel::State::Idle)) {
            continue;
        }

        const ChannelType type = channel->config().type;

        if (type == ChannelType::CcTc) {
            combined = channel.get();            // second choice
        } else if (type == ChannelType::Tc || type == ChannelType::AcTc) {
            return channel.get();
        }
    }

    return combined;
}

void NmtSystem::pagingAnswered(NmtTransaction& transaction, NmtChannel& channel)
{
    transaction.paging = false;
    transaction.timer->stop();

    // the other calling channels stop paging
    const std::vector<NmtChannel*> channels = transaction.channels;

    transaction.channels.assign(1, &channel);
    for (NmtChannel* other : channels)
    {
        if (other != &channel) {
            other->transactionEnded();
        }
    }
}

void NmtSystem::moveToTrafficChannel(NmtTransaction& transaction, NmtChannel& from, NmtChannel& to)
{
    transaction.channels.assign(1, &to);
    from.transactionEnded();
    to.beginMtIdent(transaction);
}

void NmtSystem::callAlerting(NmtTransaction& transaction)
{
    if (m_alerting)
    {
        const NmtSubscriber subscriber = transaction.subscriber;
        m_alerting(subscriber);
    }
}

void NmtSystem::callAnswered(NmtTransaction& transaction)
{
    if (m_answered)
    {
        const NmtSubscriber subscriber = transaction.subscriber;
        m_answered(subscriber);
    }
}

void NmtSystem::callFailed(NmtTransaction& transaction, modemcellular::Cause cause)
{
    if (m_failed)
    {
        const NmtSubscriber subscriber = transaction.subscriber;
        m_failed(subscriber, cause);
    }
}

void NmtSystem::audioFromMobile(NmtTransaction& transaction, const float* samples, int count)
{
    if (m_audioFromMobile)
    {
        // a copy: the handler may end the transaction
        const NmtSubscriber subscriber = transaction.subscriber;
        m_audioFromMobile(subscriber, samples, count);
    }
}

void NmtSystem::audioToMobile(NmtTransaction& transaction, float* samples, int count)
{
    if (m_audioToMobile)
    {
        const NmtSubscriber subscriber = transaction.subscriber;
        m_audioToMobile(subscriber, samples, count);
    }
}

bool NmtSystem::releaseCall(char country, const std::string& number)
{
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country != country || transaction.subscriber.number != number) {
            continue;
        }
        // a call that is still paging has no phone on a channel: it just stops
        if (transaction.paging && transaction.callUp)
        {
            transaction.callUp = false;
            destroyTransaction(&transaction);
            return true;
        }
        if (transaction.channels.empty()) {
            return false;
        }

        return transaction.channels.front()->networkRelease();
    }

    return false;
}

void NmtSystem::roamingConfirmed(const NmtSubscriber& subscriber)
{
    // a copy: the listener may end the transaction the subscriber is stored in
    const NmtSubscriber confirmed = subscriber;

    m_registered[subscriberKey(confirmed.country, confirmed.number)] = confirmed;

    if (m_listener) {
        m_listener(confirmed);
    }
}

} // namespace modemnmt
