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

#include "radiopagingflexscheduler.h"

#include <algorithm>

const int RadioPagingFlexScheduler::m_framesPerCycle;
const int RadioPagingFlexScheduler::m_cyclesPerHour;
const int RadioPagingFlexScheduler::m_framesPerHour;
const int RadioPagingFlexScheduler::m_protectedFrames;
const size_t RadioPagingFlexScheduler::m_numberCounterMax;

namespace {

const int framesPerCycle = RadioPagingFlexScheduler::m_framesPerCycle;
const int maxCandidates = 256;

} // namespace

// ---------------------------------------------------------------------------
// Message number counter
// ---------------------------------------------------------------------------

void RadioPagingFlexScheduler::NumberCounter::touch(List::iterator it)
{
    m_order.splice(m_order.begin(), m_order, it);
}

void RadioPagingFlexScheduler::NumberCounter::insert(uint64_t capcode, uint8_t value)
{
    while (!m_order.empty() && (m_order.size() >= m_maxEntries))
    {
        m_index.erase(m_order.back().first);
        m_order.pop_back();
    }

    m_order.push_front(std::make_pair(capcode, value));
    m_index[capcode] = m_order.begin();
}

uint8_t RadioPagingFlexScheduler::NumberCounter::next(uint64_t capcode)
{
    auto found = m_index.find(capcode);

    if (found != m_index.end())
    {
        List::iterator it = found->second;
        touch(it);
        it->second = (it->second + 1) & 0x3f;
        return it->second;
    }

    insert(capcode, 1);
    return 1;
}

int RadioPagingFlexScheduler::NumberCounter::peek(uint64_t capcode) const
{
    auto found = m_index.find(capcode);
    return (found == m_index.end()) ? -1 : found->second->second;
}

void RadioPagingFlexScheduler::NumberCounter::set(uint64_t capcode, uint8_t value)
{
    auto found = m_index.find(capcode);

    if (found != m_index.end())
    {
        touch(found->second);
        found->second->second = value & 0x3f;
        return;
    }

    insert(capcode, value & 0x3f);
}

// ---------------------------------------------------------------------------
// Time grid
// ---------------------------------------------------------------------------

RadioPagingFlexScheduler::CapcodeSlot RadioPagingFlexScheduler::capcodeSlot(uint64_t capcode)
{
    CapcodeSlot slot;
    slot.m_frame = (uint32_t) ((capcode / 16) % framesPerCycle);
    slot.m_phase = (uint32_t) ((capcode / 4) % 4);
    return slot;
}

RadioPagingFlexScheduler::FrameTime RadioPagingFlexScheduler::frameTime(uint64_t frameIndex)
{
    FrameTime time;
    time.m_frame = (uint32_t) (frameIndex % framesPerCycle);
    time.m_cycle = (uint32_t) ((frameIndex / framesPerCycle) % m_cyclesPerHour);
    time.m_absolute = time.m_cycle * framesPerCycle + time.m_frame;
    return time;
}

uint32_t RadioPagingFlexScheduler::nextFrame(uint32_t current, uint32_t assigned, int collapse)
{
    if (collapse <= 0) {
        return current;
    }

    uint32_t period = 1u << collapse;
    uint32_t target = assigned % period;
    uint32_t remainder = current % period;

    if (remainder <= target) {
        return current + (target - remainder);
    }

    return current + (period - remainder + target);
}

bool RadioPagingFlexScheduler::frameEligible(uint64_t capcode, uint32_t frame, int collapse)
{
    return nextFrame(frame, capcodeSlot(capcode).m_frame, collapse) == frame;
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

RadioPagingFlexEncoder::RadioPagingFlexEncoder() :
    m_mode(RadioPagingFlex::Mode1600_2),
    m_inverted(false),
    m_collapse(0),
    m_reserved(RadioPagingFlexScheduler::m_framesPerCycle, 0),
    m_nextId(1)
{
}

bool RadioPagingFlexEncoder::setCollapse(int collapse, std::string& error)
{
    if ((collapse < 0) || (collapse > RadioPagingFlex::m_maxCollapse))
    {
        error = "Collapse must be 0 to 7";
        return false;
    }

    m_collapse = collapse;
    return true;
}

bool RadioPagingFlexEncoder::setReservedFrames(const std::vector<uint8_t>& table, std::string& error)
{
    if (table.empty())
    {
        m_reserved.assign(RadioPagingFlexScheduler::m_framesPerCycle, 0);
        return true;
    }
    if (table.size() != (size_t) RadioPagingFlexScheduler::m_framesPerCycle)
    {
        error = "Reserved frame table must have 128 entries";
        return false;
    }

    for (int f = 0; f < RadioPagingFlexScheduler::m_protectedFrames; f++)
    {
        if (table[f])
        {
            error = "Frames 0 to 3 can not be reserved";
            return false;
        }
    }

    m_reserved = table;
    for (uint8_t& r : m_reserved) {
        r = r ? 1 : 0;
    }
    return true;
}

bool RadioPagingFlexEncoder::schedulable(uint64_t capcode) const
{
    for (int f = 0; f < RadioPagingFlexScheduler::m_framesPerCycle; f++)
    {
        if (!m_reserved[f] && RadioPagingFlexScheduler::frameEligible(capcode, f, m_collapse)) {
            return true;
        }
    }

    return false;
}

RadioPagingFlex::PhaseMessage RadioPagingFlexEncoder::phaseMessage(const Item& item)
{
    RadioPagingFlex::PhaseMessage pm;
    const TxMessage& m = item.m_message;

    pm.m_capcode = m.m_capcode;
    pm.m_type = m.m_type;
    pm.m_text = m.m_text;
    pm.m_priority = m.m_priority;
    pm.m_number = item.m_number;
    pm.m_shortType = m.m_shortType;
    pm.m_source = m.m_source;
    pm.m_binary = m.m_binary;
    pm.m_fragment = item.m_fragment;
    pm.m_fragments = item.m_fragments;
    pm.m_signature = item.m_signature;
    return pm;
}

bool RadioPagingFlexEncoder::fragmentReady(const Item& item) const
{
    if (item.m_fragment == 0) {
        return true;
    }

    for (const Item& other : m_queue)
    {
        if ((other.m_id == item.m_id) && (other.m_fragment < item.m_fragment)) {
            return false;
        }
    }

    return true;
}

RadioPagingFlexEncoder::EnqueueResult RadioPagingFlexEncoder::enqueue(const TxMessage& message)
{
    EnqueueResult result;
    RadioPagingFlex::PhaseMessage pm;

    pm.m_capcode = message.m_capcode;
    pm.m_type = message.m_type;
    pm.m_text = (message.m_type == RadioPagingFlex::Tone) ? std::string() : message.m_text;
    pm.m_priority = message.m_priority;
    pm.m_shortType = message.m_shortType;
    pm.m_source = message.m_source;
    pm.m_binary = message.m_binary;

    // HEX/binary and secure messages are split into fragments of a frame each; the others are one piece
    std::vector<RadioPagingFlex::PhaseMessage> parts;

    if ((pm.m_type == RadioPagingFlex::Binary) || (pm.m_type == RadioPagingFlex::Secure))
    {
        if (!RadioPagingFlex::fragmentMessage(pm, parts, result.m_error)) {
            return result;
        }
    }
    else
    {
        parts.push_back(pm);
    }

    for (const RadioPagingFlex::PhaseMessage& part : parts)
    {
        int cost = RadioPagingFlex::messageCost(part, &result.m_error);

        if (cost < 0) {
            return result;
        }
        if (cost > RadioPagingFlex::m_wordsPerFrame - 1)
        {
            result.m_error = "Message does not fit in a frame";
            return result;
        }
    }
    if (!schedulable(message.m_capcode))
    {
        result.m_error = "All frames of the capcode are reserved";
        return result;
    }

    const uint32_t id = m_nextId++;

    for (const RadioPagingFlex::PhaseMessage& part : parts)
    {
        Item item;
        item.m_id = id;
        item.m_message = message;
        item.m_message.m_text = part.m_text;
        item.m_number = -1;
        item.m_fragment = part.m_fragment;
        item.m_fragments = part.m_fragments;
        item.m_signature = part.m_signature;
        m_queue.push_back(item);
    }

    result.m_ok = true;
    result.m_id = id;
    return result;
}

std::vector<uint32_t> RadioPagingFlexEncoder::unschedulable() const
{
    std::vector<uint32_t> ids;

    for (const Item& item : m_queue)
    {
        // Once per message (its first queued fragment)
        if (!schedulable(item.m_message.m_capcode) && (std::find(ids.begin(), ids.end(), item.m_id) == ids.end())) {
            ids.push_back(item.m_id);
        }
    }

    return ids;
}

bool RadioPagingFlexEncoder::remove(uint32_t id)
{
    // All the fragments still queued of the message
    bool found = false;

    for (auto it = m_queue.begin(); it != m_queue.end();)
    {
        if (it->m_id == id)
        {
            it = m_queue.erase(it);
            found = true;
        }
        else
        {
            ++it;
        }
    }

    return found;
}

size_t RadioPagingFlexEncoder::eligible(uint64_t frameIndex) const
{
    uint32_t frame = RadioPagingFlexScheduler::frameTime(frameIndex).m_frame;
    size_t n = 0;

    if (m_reserved[frame]) {
        return 0;
    }

    for (const Item& item : m_queue)
    {
        if (RadioPagingFlexScheduler::frameEligible(item.m_message.m_capcode, frame, m_collapse) && fragmentReady(item)) {
            n++;
        }
    }

    return n;
}

bool RadioPagingFlexEncoder::continuationDue(uint64_t frameIndex) const
{
    uint32_t frame = RadioPagingFlexScheduler::frameTime(frameIndex).m_frame;

    if (m_reserved[frame]) {
        return false;
    }

    for (const Item& item : m_queue)
    {
        if ((item.m_fragment > 0) && RadioPagingFlexScheduler::frameEligible(item.m_message.m_capcode, frame, m_collapse)
            && fragmentReady(item)) {
            return true;
        }
    }

    return false;
}

RadioPagingFlexEncoder::Frame RadioPagingFlexEncoder::buildFrame(uint64_t frameIndex)
{
    Frame frame;
    RadioPagingFlexScheduler::FrameTime time = RadioPagingFlexScheduler::frameTime(frameIndex);

    frame.m_cycle = time.m_cycle;
    frame.m_frame = time.m_frame;
    frame.m_mode = m_mode;
    frame.m_inverted = m_inverted;

    if (m_reserved[time.m_frame])
    {
        frame.m_reserved = true;
        return frame;
    }

    const int phases = RadioPagingFlex::phaseCount(m_mode);
    const int capacity = RadioPagingFlex::m_wordsPerFrame - 1;
    const bool twoLevels = (RadioPagingFlex::levels(m_mode) == 2);

    struct Candidate
    {
        std::list<Item>::iterator m_item;
        int m_cost;
        int m_addressWords;
    };

    std::vector<Candidate> candidates[RadioPagingFlex::m_maxPhases];
    // The system ID words of roaming (block information words 2 to 4) take words of the phase that carries them
    const int ssidPhase = m_roaming.m_enabled ? RadioPagingFlex::ssidPhase(m_mode, time.m_frame) : -1;
    const int ssidWords = (time.m_frame <= 3) ? 2 : 1;
    int used[RadioPagingFlex::m_maxPhases] = {0, 0, 0, 0};
    if ((ssidPhase >= 0) && (ssidPhase < RadioPagingFlex::m_maxPhases)) {
        used[ssidPhase] = ssidWords;
    }
    int priorityWords[RadioPagingFlex::m_maxPhases] = {0, 0, 0, 0};
    std::vector<std::pair<uint64_t, int>> usage;

    // Phases of the most significant bit of 4-level symbols are preferred when a phase is full
    auto isMsb = [&](int p) {
        if (twoLevels) {
            return true;
        }
        return (phases == 2) ? (p == 0) : (phases == 4) ? ((p == 0) || (p == 2)) : true;
    };

    // Passes: priority addresses, other addresses, tone only addresses. Messages keep their queue order.
    for (int pass = 0; pass < 3; pass++)
    {
        for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
        {
            const TxMessage& m = it->m_message;
            bool isTone = (m.m_type == RadioPagingFlex::Tone);

            if (!RadioPagingFlexScheduler::frameEligible(m.m_capcode, time.m_frame, m_collapse)) {
                continue;
            }
            // A fragment waits for a frame after the one of the fragment before it (that one is erased from the
            // queue once its frame is built)
            if (!fragmentReady(*it)) {
                continue;
            }
            if ((pass == 0) && !(m.m_priority && !isTone)) {
                continue;
            }
            if ((pass == 1) && (m.m_priority || isTone)) {
                continue;
            }
            if ((pass == 2) && !isTone) {
                continue;
            }

            int cost = RadioPagingFlex::messageCost(phaseMessage(*it));
            if (cost < 0) {
                continue;
            }

            // The same address is used at most twice in a frame
            int usageCount = 0;
            for (const auto& u : usage)
            {
                if (u.first == m.m_capcode) {
                    usageCount = u.second;
                    break;
                }
            }
            if (usageCount >= 2) {
                continue;
            }

            int addressWords = RadioPagingFlex::isLongAddress(m.m_capcode) ? 2 : 1;
            int phase = (int) (RadioPagingFlexScheduler::capcodeSlot(m.m_capcode).m_phase % (uint32_t) phases);

            if (used[phase] + cost > capacity)
            {
                if (phases <= 1) {
                    continue;
                }

                int found = -1;
                for (int p = 0; (p < phases) && (found < 0); p++)
                {
                    if ((p != phase) && isMsb(p) && (used[p] + cost <= capacity)) {
                        found = p;
                    }
                }
                for (int p = 0; (p < phases) && (found < 0); p++)
                {
                    if ((p != phase) && !isMsb(p) && (used[p] + cost <= capacity)) {
                        found = p;
                    }
                }
                if (found < 0) {
                    continue;
                }
                phase = found;
            }

            if (m.m_priority && (priorityWords[phase] + addressWords > RadioPagingFlex::m_maxPriorityWords))
            {
                if (phases <= 1) {
                    continue;
                }

                int found = -1;
                for (int p = 0; p < phases; p++)
                {
                    if ((p == phase) || (priorityWords[p] + addressWords > RadioPagingFlex::m_maxPriorityWords)
                        || (used[p] + cost > capacity)) {
                        continue;
                    }
                    if ((found < 0) || isMsb(p))
                    {
                        found = p;
                        if (isMsb(p)) {
                            break;
                        }
                    }
                }
                if (found < 0) {
                    continue;
                }
                phase = found;
            }

            if ((int) candidates[phase].size() >= maxCandidates) {
                continue;
            }

            Candidate c;
            c.m_item = it;
            c.m_cost = cost;
            c.m_addressWords = addressWords;
            candidates[phase].push_back(c);
            used[phase] += cost;
            if (m.m_priority) {
                priorityWords[phase] += addressWords;
            }

            bool counted = false;
            for (auto& u : usage)
            {
                if (u.first == m.m_capcode)
                {
                    u.second++;
                    counted = true;
                    break;
                }
            }
            if (!counted) {
                usage.push_back(std::make_pair(m.m_capcode, 1));
            }
        }
    }

    // Low traffic flags from the address field length of each phase
    std::vector<int> addressWords(phases, 0);
    for (int p = 0; p < phases; p++)
    {
        for (const Candidate& c : candidates[p]) {
            addressWords[p] += c.m_addressWords;
        }
        // The flag tells that the address field ends in block 0 (STD-43A 3.6): the system ID words come first
        if (p == ssidPhase) {
            addressWords[p] += ssidWords;
        }
    }
    frame.m_traffic = RadioPagingFlex::lowTrafficFlags(addressWords, phases);

    // Phases
    std::vector<std::vector<uint32_t>> phaseWords(phases);
    std::vector<std::list<Item>::iterator> sent;

    for (int p = 0; p < phases; p++)
    {
        RadioPagingFlex::PhaseParams params;
        params.m_mode = m_mode;
        params.m_phase = p;
        params.m_collapse = m_collapse;
        params.m_carryOn = 0;

        // The simulcast system ID words are in one phase of the frame (STD-43A 6.1.1.3)
        if (m_roaming.m_enabled && (p == RadioPagingFlex::ssidPhase(m_mode, time.m_frame)))
        {
            params.m_extraBiw.push_back(RadioPagingFlex::ssid1Word(m_roaming.m_localId, m_roaming.m_coverageZone));

            if (time.m_frame <= 3) {
                params.m_extraBiw.push_back(RadioPagingFlex::ssid2Word(m_roaming.m_countryCode, m_roaming.m_trafficFlags));
            }
        }

        if (!candidates[p].empty())
        {
            std::vector<RadioPagingFlex::PhaseMessage> messages;

            for (const Candidate& c : candidates[p])
            {
                Item& item = *c.m_item;

                if (item.m_number < 0)
                {
                    item.m_number = m_numbers.next(item.m_message.m_capcode);

                    // The fragments of a message have the number of its first fragment (N, 3.10.1.2 p.60)
                    for (Item& other : m_queue)
                    {
                        if ((other.m_id == item.m_id) && (other.m_number < 0)) {
                            other.m_number = item.m_number;
                        }
                    }
                }

                messages.push_back(phaseMessage(item));
            }

            RadioPagingFlex::PhaseResult result = RadioPagingFlex::buildPhase(params, messages);

            if (result.m_ok)
            {
                phaseWords[p] = result.m_words;
                for (size_t j = 0; j < messages.size(); j++)
                {
                    if (result.m_packed[j]) {
                        sent.push_back(candidates[p][j].m_item);
                    }
                }
            }
        }

        if (phaseWords[p].empty()) {
            phaseWords[p] = RadioPagingFlex::buildPhase(params, std::vector<RadioPagingFlex::PhaseMessage>()).m_words;
        }
    }

    for (auto it : sent)
    {
        // A message is sent with its last fragment
        if (it->m_fragment == it->m_fragments - 1)
        {
            frame.m_sentIds.push_back(it->m_id);
            frame.m_sentNumbers.push_back((uint32_t) it->m_number);
        }
        m_queue.erase(it);
    }
    frame.m_idle = sent.empty();

    RadioPagingFlex::FrameParams fp;
    fp.m_mode = m_mode;
    fp.m_cycle = time.m_cycle;
    fp.m_frame = time.m_frame;
    fp.m_traffic = frame.m_traffic;
    fp.m_roaming = m_roaming.m_enabled;
    frame.m_bytes = RadioPagingFlex::assemble(fp, phaseWords);
    frame.m_blocks = RadioPagingFlex::toSymbols(frame.m_bytes, m_mode, m_inverted);
    return frame;
}
