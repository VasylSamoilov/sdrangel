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

NmtSystem::~NmtSystem()
{
    // transactions that no channel is bound to would end without a word: tell the handler (the
    // handler may do anything, so the list is left alone while it runs)
    std::vector<MwiReport> reports;

    for (NmtTransaction& transaction : m_transactions)
    {
        // a call to a phone that is still on the calling channels has no channel to report it
        if (transaction.ccPhase != NmtTransaction::CcPhase::None && transaction.callUp)
        {
            transaction.callUp = false;
            callFailed(transaction, modemcellular::Cause::TempFail);
        }
        reports.push_back(mwiReport(transaction));
    }
    for (const MwiReport& report : reports) {
        sendMwiReport(report);
    }
}

// A message call that has not got to a traffic channel yet gives way to anything the phone does or
// is called for: it ends as undelivered (the flags can be sent again). Once the phone is on a
// traffic channel the call is short and runs to its end.
bool NmtSystem::cancelMessageCall(char country, const std::string& number, uint8_t* carried)
{
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country != country || transaction.subscriber.number != number) {
            continue;
        }
        // only while it is on the calling channels
        if (!transaction.messageCall || transaction.ccPhase == NmtTransaction::CcPhase::None) {
            return false;
        }
        // For a call or a seizure of the phone the indicators go on with that call, no word to the
        // handler (it would send them again while the new call is being made).
        if (carried)
        {
            *carried = transaction.mwiFlags;
            transaction.mwiReported = true;
        }
        destroyTransaction(&transaction);
        return true;
    }

    return false;
}

NmtTransaction* NmtSystem::createTransaction(const NmtSubscriber& subscriber, NmtChannel& channel)
{
    uint8_t carried = 0;

    cancelMessageCall(subscriber.country, subscriber.number, &carried);

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
    m_transactions.back().mwiFlags = carried;

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

        const MwiReport report = mwiReport(*it);

        m_transactions.erase(it);
        sendMwiReport(report);
        return;
    }
}

// What the handler is told about message indicators of a transaction that ends.
NmtSystem::MwiReport NmtSystem::mwiReport(NmtTransaction& transaction)
{
    MwiReport report;

    if (transaction.mwiFlags && !transaction.mwiReported)
    {
        transaction.mwiReported = true;
        report.valid = true;
        report.subscriber = transaction.subscriber;
        report.flags = transaction.mwiFlags;
        report.result = transaction.mwiResult;
    }

    return report;
}

void NmtSystem::sendMwiReport(const MwiReport& report)
{
    if (report.valid && m_mwiHandler) {
        m_mwiHandler(report.subscriber, report.flags, report.result);
    }
}

bool NmtSystem::sendMwi(char country, const std::string& number, uint8_t flags)
{
    if (flags == 0 || (flags & ~static_cast<uint8_t>(MwiAll))) {
        return false;
    }

    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country != country || transaction.subscriber.number != number) {
            continue;
        }
        // a phone in a call hears it when the call is cleared; one that is doing anything else is busy
        const bool switching = transaction.channels.size() == 2 && transaction.scipTarget;

        if (!transaction.callUp || (transaction.channels.size() != 1 && !switching)
         || !transaction.channels.front()->inCall()) {
            return false;
        }
        transaction.mwiFlags |= flags;
        return true;
    }

    if (!admits(nullptr)) {
        return false;
    }

    NmtTransaction& transaction = newPagedTransaction(country, number);

    transaction.messageCall = true;
    transaction.mwiFlags = flags;
    page(transaction, 1);

    return true;
}

void NmtSystem::detachChannel(NmtChannel& channel, NmtTransaction* transaction)
{
    for (auto it = m_transactions.begin(); it != m_transactions.end(); ++it)
    {
        if (&*it != transaction) {
            continue;
        }

        if (it->scipTarget == &channel) {
            it->scipTarget = nullptr;
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
            // A call that is lost with its last channel: the exchange hears it once. A call that
            // was not answered yet failed, one that was answered or came from the phone ended.
            const bool up = it->callUp;
            const bool failed = it->mobileTerminated && !it->answered;
            NmtTransaction report;
            const MwiReport mwi = mwiReport(*it);

            report.subscriber = it->subscriber;
            // the transaction is gone before the handler runs: it may do anything
            m_transactions.erase(it);
            sendMwiReport(mwi);
            if (up)
            {
                if (failed) {
                    callFailed(report, modemcellular::Cause::TempFail);
                } else {
                    callCleared(report);
                }
            }
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
    return admits(nullptr);
}

// ---- calling channel carousel ----
//
// A call to a phone lives on the calling channels until it gets its traffic channel: paged (2a)
// on the idle calling channels with room, answered (10a, 10d, 11b) on one of them, then the
// channel order (2b) there, or, with no free traffic channel, the queueing information (2f / 2c)
// after which the phone is standby on the channel. The phone reacts to 2b, 2d, 2f and 2a only
// for 2T after its answer (NMT Doc 450-3 5.4.3): a queued phone is paged again when a traffic
// channel becomes free, and gets the channel order after its new answer (as the priority phone
// of NMT Doc 450-1 4.4.1.7, state 4).

namespace
{

// queueing information frames after the answer (our choice; one is what the phone needs)
const int kQueueInfoFrames = 2;
// how long an answered call waits for a traffic channel; a message call holds less
const double kQueueTimeout = 60.0;
const double kMessageQueueTimeout = 10.0;
// 2T after the answer the phone no longer waits for the channel order (NMT Doc 450-3 5.4.3)
const double kOrderDeadline = 2.0 * 1.107;

bool contains(const std::vector<NmtChannel*>& channels, const NmtChannel* channel)
{
    return std::find(channels.begin(), channels.end(), channel) != channels.end();
}

bool onCallingChannels(NmtTransaction::CcPhase phase)
{
    return phase != NmtTransaction::CcPhase::None;
}

} // namespace

bool NmtSystem::idleCallingChannel(const NmtChannel& channel) const
{
    return channel.valid() && channel.isCallingChannel() && channel.state() == NmtChannel::State::Idle;
}

// Pages placed on the calling channel (also those that wait for the wake-up window of battery
// saving: they go out on it later).
int NmtSystem::pagesOn(const NmtChannel& callingChannel) const
{
    int pages = 0;

    for (const NmtTransaction& transaction : m_transactions)
    {
        if ((transaction.ccPhase == NmtTransaction::CcPhase::Page || transaction.ccPhase == NmtTransaction::CcPhase::Deferred)
         && contains(transaction.pageChannels, &callingChannel)) {
            pages++;
        }
    }

    return pages;
}

int NmtSystem::queuedCalls() const
{
    int queued = 0;

    for (const NmtTransaction& transaction : m_transactions)
    {
        if (transaction.ccPhase == NmtTransaction::CcPhase::Queue) {
            queued++;
        }
    }

    return queued;
}

int NmtSystem::idleTrafficChannels() const
{
    int idle = 0;

    for (const auto& channel : m_channels)
    {
        const ChannelType type = channel->config().type;

        if (channel->valid() && channel->state() == NmtChannel::State::Idle
         && (type == ChannelType::Tc || type == ChannelType::AcTc || type == ChannelType::CcTc)) {
            idle++;
        }
    }

    return idle;
}

// Room for one more call to a phone: a page on an idle calling channel, and, with no idle traffic
// channel, a place in the queue for it (counting the calls that are paged already).
bool NmtSystem::admits(modemcellular::Cause* cause) const
{
    bool room = false;

    for (const auto& channel : m_channels)
    {
        if (idleCallingChannel(*channel) && pagesOn(*channel) < m_maxPagesPerCc)
        {
            room = true;
            break;
        }
    }

    if (room && idleTrafficChannels() == 0)
    {
        int paged = 0;

        for (const NmtTransaction& transaction : m_transactions)
        {
            if (transaction.ccPhase == NmtTransaction::CcPhase::Page || transaction.ccPhase == NmtTransaction::CcPhase::Deferred) {
                paged++;
            }
        }
        room = queuedCalls() + paged < m_maxQueued;
    }

    if (!room && cause) {
        *cause = modemcellular::Cause::NoChannel;
    }
    return room;
}

NmtSystem::Capacity NmtSystem::capacity() const
{
    Capacity capacity;

    for (const auto& channel : m_channels)
    {
        if (idleCallingChannel(*channel)) {
            capacity.freePageSlots += std::max(0, m_maxPagesPerCc - pagesOn(*channel));
        }
    }
    capacity.freeQueueSlots = std::max(0, m_maxQueued - queuedCalls());
    capacity.idleTrafficChannels = idleTrafficChannels();

    return capacity;
}

NmtTransaction::CcPhase NmtSystem::callPhase(char country, const std::string& number) const
{
    for (const NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country == country && transaction.subscriber.number == number) {
            return transaction.ccPhase;
        }
    }

    return NmtTransaction::CcPhase::None;
}

// The call is off the calling channels (its traffic channel has it, or it ends).
void NmtSystem::leaveCallingChannels(NmtTransaction& transaction)
{
    transaction.ccPhase = NmtTransaction::CcPhase::None;
    transaction.pageChannels.clear();
    transaction.pageDue.clear();
    transaction.ccChannel = nullptr;
    transaction.queueInfoLeft = 0;
    transaction.repaged = false;
    transaction.timer->stop();
}

// Queued calls whose phone is paged again because a traffic channel became free: as many as there
// are idle traffic channels not already promised to a call (an order to send, or such a page).
// Priority phones first, then the oldest.
void NmtSystem::repageQueued()
{
    int promised = 0;

    for (const NmtTransaction& transaction : m_transactions)
    {
        if (transaction.ccPhase == NmtTransaction::CcPhase::Order
         || (transaction.repaged && (transaction.ccPhase == NmtTransaction::CcPhase::Page
                                  || transaction.ccPhase == NmtTransaction::CcPhase::Deferred))) {
            promised++;
        }
    }

    const int idle = idleTrafficChannels();

    for (int pass = 0; pass < 2 && promised < idle; pass++)
    {
        for (NmtTransaction& transaction : m_transactions)
        {
            if (promised >= idle) {
                break;
            }
            // the queueing information has to be out first
            if (transaction.ccPhase != NmtTransaction::CcPhase::Queue || transaction.queueInfoLeft > 0
             || transaction.priority != (pass == 0)) {
                continue;
            }

            transaction.repaged = true;
            transaction.pageWaited = false;
            page(transaction, 1);
            promised++;
        }
    }
}

NmtHost::CarouselItem NmtSystem::carouselItem(NmtChannel& callingChannel)
{
    CarouselItem item;

    // the calling channel of an answered call is gone (a combined channel that carries a call
    // now): the call goes on with this one
    for (NmtTransaction& transaction : m_transactions)
    {
        if ((transaction.ccPhase == NmtTransaction::CcPhase::Order || transaction.ccPhase == NmtTransaction::CcPhase::Queue)
         && transaction.ccChannel && !idleCallingChannel(*transaction.ccChannel)) {
            transaction.ccChannel = &callingChannel;
        }
    }

    repageQueued();

    // the channel order first: the phone waits for it for 2T only
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.ccPhase != NmtTransaction::CcPhase::Order || transaction.ccChannel != &callingChannel) {
            continue;
        }

        NmtChannel* const trafficChannel = findTrafficChannel(callingChannel);

        if (trafficChannel)
        {
            item.kind = CarouselItem::Kind::Order;
            item.transaction = &transaction;
            item.trafficChannel = trafficChannel;
            return item;
        }
        // the traffic channel was taken meanwhile: the call is queued
        transaction.ccPhase = NmtTransaction::CcPhase::Queue;
        transaction.queueInfoLeft = kQueueInfoFrames;
        transaction.timer->start(transaction.messageCall ? kMessageQueueTimeout : kQueueTimeout);
    }

    // then the queueing information, also within 2T
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.ccPhase == NmtTransaction::CcPhase::Queue && transaction.ccChannel == &callingChannel
         && transaction.queueInfoLeft > 0)
        {
            transaction.queueInfoLeft--;
            item.kind = CarouselItem::Kind::Queue;
            item.transaction = &transaction;
            return item;
        }
    }

    // then the pages of this try that did not go out on this channel yet
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.ccPhase != NmtTransaction::CcPhase::Page) {
            continue;
        }

        const auto due = std::find(transaction.pageDue.begin(), transaction.pageDue.end(), &callingChannel);

        if (due != transaction.pageDue.end())
        {
            transaction.pageDue.erase(due);
            item.kind = CarouselItem::Kind::Page;
            item.transaction = &transaction;
            return item;
        }
    }

    return item;
}

bool NmtSystem::awaitsAnswer(const NmtChannel& callingChannel) const
{
    for (const NmtTransaction& transaction : m_transactions)
    {
        if (transaction.ccPhase == NmtTransaction::CcPhase::Page && contains(transaction.pageChannels, &callingChannel)) {
            return true;
        }
    }

    return false;
}

bool NmtSystem::callingChannelAnswer(const NmtSubscriber& subscriber, NmtChannel& callingChannel, bool priority)
{
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country != subscriber.country || transaction.subscriber.number != subscriber.number) {
            continue;
        }
        if (transaction.ccPhase != NmtTransaction::CcPhase::Page || !contains(transaction.pageChannels, &callingChannel)) {
            return false;
        }

        if (priority) {
            transaction.priority = true;
        }
        // the other calling channels stop paging; the answer goes on here
        transaction.timer->stop();
        transaction.pageChannels.clear();
        transaction.pageDue.clear();
        transaction.ccChannel = &callingChannel;
        transaction.repaged = false;

        if (findTrafficChannel(callingChannel))
        {
            transaction.ccPhase = NmtTransaction::CcPhase::Order;
            // the phone waits for the order for 2T, then it is standby again
            transaction.timer->start(kOrderDeadline);
        }
        else
        {
            transaction.ccPhase = NmtTransaction::CcPhase::Queue;
            transaction.queueInfoLeft = kQueueInfoFrames;
            transaction.timer->start(transaction.messageCall ? kMessageQueueTimeout : kQueueTimeout);
        }
        return true;
    }

    return false;
}

void NmtSystem::orderSent(NmtTransaction& transaction, NmtChannel& trafficChannel)
{
    leaveCallingChannels(transaction);
    transaction.channels.assign(1, &trafficChannel);
    trafficChannel.beginMtIdent(transaction);
}

NmtTransaction* NmtSystem::callingChannelCleared(const NmtSubscriber& subscriber, NmtChannel& callingChannel)
{
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country != subscriber.country || transaction.subscriber.number != subscriber.number) {
            continue;
        }
        if (!onCallingChannels(transaction.ccPhase)) {
            return nullptr;
        }

        leaveCallingChannels(transaction);
        transaction.channels.assign(1, &callingChannel);
        return &transaction;
    }

    return nullptr;
}

// No traffic channel became free in time: the call fails as congestion. The phone is standby on
// the calling channel, there is nothing to release.
void NmtSystem::queueTimeout(NmtTransaction& transaction)
{
    if (transaction.messageCall) {
        transaction.mwiResult = NmtServiceEnd::NoChannel;
    }
    if (transaction.callUp)
    {
        transaction.callUp = false;
        callFailed(transaction, modemcellular::Cause::NoChannel);
    }
    destroyTransaction(&transaction);
}

bool NmtSystem::startCall(char country, const std::string& number, const std::string& callerId,
                          modemcellular::NumberType callerType, modemcellular::Cause* cause)
{
    // a call waits for nothing a message call could still do; its indicators go with the call
    uint8_t carried = 0;

    cancelMessageCall(country, number, &carried);

    // a phone has one transaction
    for (const NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country == country && transaction.subscriber.number == number)
        {
            if (cause) {
                *cause = modemcellular::Cause::Busy;
            }
            return false;
        }
    }

    // no room on the calling channels: rejected at once
    if (!admits(cause))
    {
        if (carried && m_mwiHandler)
        {
            NmtSubscriber subscriber;

            subscriber.country = country;
            subscriber.number = number;
            m_mwiHandler(subscriber, carried, NmtServiceEnd::Undelivered);
        }
        return false;
    }

    NmtTransaction& transaction = newPagedTransaction(country, number);

    transaction.mwiFlags = carried;
    transaction.callUp = true;
    // the phone does not add the '+' of an international number
    transaction.callerId = (callerType == modemcellular::NumberType::International && !callerId.empty() && callerId[0] != '+')
        ? "+" + callerId : callerId;
    // no number to show: the phone is told so (4.3.3.13: type 3)
    transaction.callerType = callerId.empty() ? modemcellular::NumberType::NotAvailable : callerType;
    page(transaction, 1);

    return true;
}

// A transaction for a call to the phone that is paged; the caller checked that the phone has none.
NmtTransaction& NmtSystem::newPagedTransaction(char country, const std::string& number)
{
    m_transactions.emplace_back();
    NmtTransaction& transaction = m_transactions.back();
    NmtTransaction* const pointer = &transaction;

    transaction.subscriber.country = country;
    transaction.subscriber.number = number;
    transaction.mobileTerminated = true;
    transaction.timer = std::make_unique<modemcellular::Timer>(m_timers, [this, pointer]() { pagingTimeout(*pointer); });

    return transaction;
}

// Pages the phone: one 2a per try on each of its calling channels (the carousel sends it). The
// first try places the page on the idle calling channels that have room; a phone that is queued
// is paged on the calling channel it is standby on. With battery saving on a calling channel the
// phone may be asleep: the first page there waits one period of the group the phone is in.
void NmtSystem::page(NmtTransaction& transaction, int tryNo)
{
    const char last = transaction.subscriber.number.empty() ? '\0' : transaction.subscriber.number.back();
    const int x6 = (last >= '0' && last <= '9') ? last - '0' : -1;
    // does the phone sleep on this calling channel?
    const auto sleeps = [x6](const NmtChannel& channel) {
        const ChannelConfig& c = channel.config();
        return channel.isCallingChannel() && c.batteryPeriod > 0 && x6 >= 0 && groupContains(c.batteryGroup, x6);
    };

    // the channels of the page, unless they are chosen already (the wake-up window of battery
    // saving came for a page that waited)
    if (tryNo == 1 && transaction.pageChannels.empty())
    {
        NmtChannel* const standby = transaction.ccChannel;

        transaction.ccChannel = nullptr;
        transaction.queueInfoLeft = 0;
        if (standby && idleCallingChannel(*standby)) {
            transaction.pageChannels.push_back(standby);
        }
        else
        {
            for (const auto& channel : m_channels)
            {
                if (idleCallingChannel(*channel) && pagesOn(*channel) < m_maxPagesPerCc) {
                    transaction.pageChannels.push_back(channel.get());
                }
            }
        }
    }

    else if (tryNo > 1 && !transaction.repaged)
    {
        // a calling channel that became idle since takes part in the next try
        for (const auto& channel : m_channels)
        {
            if (idleCallingChannel(*channel) && !contains(transaction.pageChannels, channel.get())
             && pagesOn(*channel) < m_maxPagesPerCc) {
                transaction.pageChannels.push_back(channel.get());
            }
        }
    }

    transaction.pageTry = tryNo;
    transaction.pageDue.clear();

    if (tryNo == 1 && !transaction.pageWaited)
    {
        for (const NmtChannel* channel : transaction.pageChannels)
        {
            if (!sleeps(*channel)) {
                continue;
            }

            // one frame is 138.33 ms; whole seconds, at least one
            const int frames = batteryPeriodFrames(channel->config().batteryPeriod);
            const int seconds = std::max((frames * 138333 + 999999) / 1000000, 1);

            transaction.pageDeferred = true;
            transaction.pageWaited = true;
            transaction.timer->start(seconds);
            // the calling channels whose phones are awake page at once
            for (NmtChannel* c : transaction.pageChannels)
            {
                if (!sleeps(*c)) {
                    transaction.pageDue.push_back(c);
                }
            }
            transaction.ccPhase = transaction.pageDue.empty() ? NmtTransaction::CcPhase::Deferred : NmtTransaction::CcPhase::Page;
            return;
        }
    }

    for (NmtChannel* channel : transaction.pageChannels)
    {
        if (idleCallingChannel(*channel)) {
            transaction.pageDue.push_back(channel);
        }
    }
    transaction.ccPhase = NmtTransaction::CcPhase::Page;
    transaction.timer->start(kPagingTimeout);
}

NmtTransaction* NmtSystem::startPriorityCall(const NmtSubscriber& subscriber, NmtChannel&)
{
    uint8_t carried = 0;

    cancelMessageCall(subscriber.country, subscriber.number, &carried);

    for (const NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country == subscriber.country && transaction.subscriber.number == subscriber.number) {
            return nullptr;
        }
    }

    NmtTransaction& transaction = newPagedTransaction(subscriber.country, subscriber.number);

    transaction.mwiFlags = carried;
    transaction.priority = true;
    transaction.priorityMo = true;
    page(transaction, 1);

    return &transaction;
}

NmtTransaction* NmtSystem::calledPhoneSeized(const NmtSubscriber& subscriber, NmtChannel& channel)
{
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country != subscriber.country || transaction.subscriber.number != subscriber.number) {
            continue;
        }
        // only a call to the phone that is not answered yet can be taken over
        if (!transaction.mobileTerminated || transaction.answered || transaction.scipTarget
         || !onCallingChannels(transaction.ccPhase)) {
            return nullptr;
        }

        leaveCallingChannels(transaction);
        transaction.channels.assign(1, &channel);
        return &transaction;
    }

    return nullptr;
}

void NmtSystem::pagingTimeout(NmtTransaction& transaction)
{
    if (transaction.ccPhase == NmtTransaction::CcPhase::Queue)
    {
        queueTimeout(transaction);
        return;
    }
    // the order did not go out within 2T: the phone is standby again, it is paged with the next
    // try on the channel it answered on
    if (transaction.ccPhase == NmtTransaction::CcPhase::Order)
    {
        transaction.pageChannels.assign(1, transaction.ccChannel);
        transaction.ccChannel = nullptr;
        transaction.ccPhase = NmtTransaction::CcPhase::Page;
    }

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
            // second choice; the calling channel itself comes first
            if (!combined || channel.get() == &from) {
                combined = channel.get();
            }
        } else if (type == ChannelType::Tc || type == ChannelType::AcTc) {
            return channel.get();
        }
    }

    return combined;
}

void NmtSystem::callAlerting(NmtTransaction& transaction)
{
    if (m_alerting && !transaction.messageCall)
    {
        const NmtSubscriber subscriber = transaction.subscriber;
        m_alerting(subscriber);
    }
}

void NmtSystem::callAnswered(NmtTransaction& transaction)
{
    if (m_answered && !transaction.messageCall)
    {
        const NmtSubscriber subscriber = transaction.subscriber;
        m_answered(subscriber);
    }
}

void NmtSystem::callFailed(NmtTransaction& transaction, modemcellular::Cause cause)
{
    // a message call is no call of the exchange: the outcome goes to the message indicator report
    if (transaction.messageCall)
    {
        transaction.mwiResult = (cause == modemcellular::Cause::NoChannel) ? NmtServiceEnd::NoChannel : NmtServiceEnd::Unreachable;
        return;
    }

    // a priority phone that places a call has no call in the exchange before it dials
    if (m_failed && !transaction.priorityMo)
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
        if (transaction.messageCall) {
            return cancelMessageCall(country, number, nullptr);
        }
        // a call that is still on the calling channels has no phone on a channel: it just stops
        if (onCallingChannels(transaction.ccPhase) && transaction.callUp)
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

bool NmtSystem::moveCall(char country, const std::string& number, int channel)
{
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country != country || transaction.subscriber.number != number) {
            continue;
        }
        // a connected call on one channel, and no switch going on
        if (!transaction.callUp || transaction.scipTarget || transaction.channels.size() != 1) {
            return false;
        }

        NmtChannel* const from = transaction.channels.front();

        if (from->state() != NmtChannel::State::Active) {
            return false;
        }

        // the first free traffic channel, or the one asked for
        NmtChannel* target = nullptr;

        for (const auto& candidate : m_channels)
        {
            const ChannelType type = candidate->config().type;

            if (candidate.get() == from || !candidate->valid() || candidate->state() != NmtChannel::State::Idle
             || (type != ChannelType::Tc && type != ChannelType::AcTc)) {
                continue;
            }
            if (channel != 0 && candidate->config().channel != channel) {
                continue;
            }
            // the phone keeps the supervisory tone of the call: the new channel has to listen for it
            if (candidate->config().supervisory != from->config().supervisory) {
                continue;
            }
            target = candidate.get();
            break;
        }
        if (!target) {
            return false;
        }

        // the new channel sends the supervisory tone and waits before the phone is ordered
        transaction.channels.push_back(target);
        transaction.scipTarget = target;
        target->beginScipIdent(transaction);
        from->beginScipOrder();
        return true;
    }

    return false;
}

void NmtSystem::scipCompleted(NmtTransaction& transaction)
{
    NmtChannel* const target = transaction.scipTarget;
    const std::vector<NmtChannel*> channels = transaction.channels;

    transaction.scipTarget = nullptr;
    transaction.channels.assign(1, target);
    for (NmtChannel* channel : channels)
    {
        // the old channel lets go of the call
        if (channel != target) {
            channel->transactionEnded();
        }
    }
}

void NmtSystem::scipEnded(NmtTransaction& transaction, bool forceRelease)
{
    NmtChannel* const target = transaction.scipTarget;

    if (!target) {
        return;
    }
    transaction.scipTarget = nullptr;

    if (forceRelease)
    {
        target->beginScipRelease();
        return;
    }

    // the channel drops its pointer before it goes idle
    for (auto c = transaction.channels.begin(); c != transaction.channels.end(); ++c)
    {
        if (*c == target)
        {
            transaction.channels.erase(c);
            break;
        }
    }
    target->transactionEnded();
}

bool NmtSystem::changePower(char country, const std::string& number, int level)
{
    for (NmtTransaction& transaction : m_transactions)
    {
        if (transaction.subscriber.country != country || transaction.subscriber.number != number) {
            continue;
        }
        if (transaction.channels.size() != 1) {
            return false;
        }

        return transaction.channels.front()->beginPowerChange(level);
    }

    return false;
}

void NmtSystem::mftDigit(NmtTransaction& transaction, char digit)
{
    if (m_mftHandler)
    {
        // a copy: the handler may end the transaction
        const NmtSubscriber subscriber = transaction.subscriber;
        m_mftHandler(subscriber, digit);
    }
}

bool NmtSystem::recallDigit(NmtTransaction& transaction, char digit)
{
    if (!m_recallDigitHandler) {
        return false;
    }

    const NmtSubscriber subscriber = transaction.subscriber;

    return m_recallDigitHandler(subscriber, digit);
}

void NmtSystem::recallEnded(NmtTransaction& transaction, const std::string& digits)
{
    if (m_recallEndHandler)
    {
        const NmtSubscriber subscriber = transaction.subscriber;
        m_recallEndHandler(subscriber, digits);
    }
}

void NmtSystem::powerChanged(NmtTransaction& transaction, int level, bool done)
{
    if (m_powerHandler)
    {
        const NmtSubscriber subscriber = transaction.subscriber;
        m_powerHandler(subscriber, level, done);
    }
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
