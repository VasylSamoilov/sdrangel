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

#ifndef MODEMRADIOPAGING_RADIOPAGINGFLEXSCHEDULER_H
#define MODEMRADIOPAGING_RADIOPAGINGFLEXSCHEDULER_H

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "export.h"
#include "radiopagingflex.h"

// FLEX time grid and placement of messages in frames.
//
// Time is a frame index supplied by the caller (no clock is read). An hour is 15 cycles
// of 128 frames, so cycle = (index / 128) mod 15 and frame = index mod 128. A pager
// listens to the frame (capcode / 16) mod 128 and the phase (capcode / 4) mod 4 of its
// capcode. With a collapse value m it listens to one frame in 2^m only: the frames whose
// number is congruent to its frame modulo 2^m.
class MODEMRADIOPAGING_API RadioPagingFlexScheduler
{
public:
    static const int m_framesPerCycle = 128;
    static const int m_cyclesPerHour = 15;
    static const int m_framesPerHour = 1920;
    static const int m_protectedFrames = 4;         //!< Frames 0-3 can not be reserved
    static const size_t m_numberCounterMax = 25000; //!< Capcodes with a message number counter

    struct CapcodeSlot
    {
        uint32_t m_frame;   //!< (capcode / 16) mod 128
        uint32_t m_phase;   //!< (capcode / 4) mod 4, before the reduction to the phases of a mode
    };

    struct FrameTime
    {
        uint32_t m_cycle;       //!< 0-14
        uint32_t m_frame;       //!< 0-127
        uint32_t m_absolute;    //!< cycle * 128 + frame
    };

    //! Message number (0-63) of each capcode, advanced by one for every message. The first
    //! number of a capcode is 1. The least recently used capcode is dropped when the table is full
    class NumberCounter
    {
    public:
        explicit NumberCounter(size_t maxEntries = m_numberCounterMax) : m_maxEntries(maxEntries) {}
        uint8_t next(uint64_t capcode);
        int peek(uint64_t capcode) const;     //!< -1 if the capcode has no counter
        void set(uint64_t capcode, uint8_t value);
        size_t size() const { return m_order.size(); }

    private:
        typedef std::list<std::pair<uint64_t, uint8_t>> List;
        void touch(List::iterator it);
        void insert(uint64_t capcode, uint8_t value);

        size_t m_maxEntries;
        List m_order;                                               //!< Most recently used first
        std::unordered_map<uint64_t, List::iterator> m_index;
    };

    static CapcodeSlot capcodeSlot(uint64_t capcode);
    static FrameTime frameTime(uint64_t frameIndex);
    //!< First frame at or after current whose number is congruent to assigned modulo 2^collapse
    static uint32_t nextFrame(uint32_t current, uint32_t assigned, int collapse);
    //!< True if a pager of the capcode listens to the frame
    static bool frameEligible(uint64_t capcode, uint32_t frame, int collapse);
};

// Queue, placement and frame builder of a FLEX transmitter with one mode and polarity.
//
// Messages wait in a queue. For every frame index the encoder returns the frame: the
// messages whose capcode is eligible in the frame are placed in the phase of their
// capcode (priority addresses first, tone only addresses last, a phase that is full
// passes messages to another phase, preferring phases of the most significant bit), a
// message is sent once and removed. A frame without messages is an idle frame
// (block information word and idle words), sent continuously. A frame reserved for
// another transmitter has no output.
class MODEMRADIOPAGING_API RadioPagingFlexEncoder
{
public:
    struct TxMessage
    {
        uint64_t m_capcode = 0;
        RadioPagingFlex::MessageType m_type = RadioPagingFlex::Alphanumeric;
        std::string m_text;         //!< Ignored for tone; short message: the digits of the numeric type
        bool m_priority = false;
        RadioPagingFlex::ShortType m_shortType = RadioPagingFlex::ShortNumeric;    //!< Short message only
        int m_source = 0;           //!< Short message source and numbered types: source code 0-7
        RadioPagingFlex::BinaryParams m_binary;     //!< HEX/binary and secure messages only
    };

    struct EnqueueResult
    {
        bool m_ok = false;
        std::string m_error;
        uint32_t m_id = 0;
    };

    struct Frame
    {
        bool m_reserved = false;    //!< Frame reserved for another transmitter: no output
        bool m_idle = false;        //!< No message in the frame (a fragment that is not the last is a message)
        uint32_t m_cycle = 0;
        uint32_t m_frame = 0;
        RadioPagingFlex::Mode m_mode = RadioPagingFlex::Mode1600_2;
        bool m_inverted = false;
        unsigned m_traffic = 0;     //!< Low traffic flags of the frame information word
        std::vector<uint32_t> m_sentIds;                    //!< Messages sent: in one piece, or their last fragment
        std::vector<uint32_t> m_sentNumbers;                //!< Message number of each sent id
        RadioPagingFlex::FrameBytes m_bytes;
        std::vector<RadioPagingFlex::SymbolBlock> m_blocks; //!< Sync part then data part; empty if reserved
    };

    RadioPagingFlexEncoder();

    void setMode(RadioPagingFlex::Mode mode) { m_mode = mode; }
    RadioPagingFlex::Mode mode() const { return m_mode; }
    void setInverted(bool inverted) { m_inverted = inverted; }
    bool inverted() const { return m_inverted; }

    //!< Multi-area/roaming by simulcast system ID (STD-43A Chapter 6). The roaming bit of the frame
    //!< information word is set; the first system ID word is in every frame and the second in frames 0 to 3,
    //!< in the phase of 6.1.1.3. The channel has to send frames 0 to 3 in full (the callers do not reserve them).
    //!< The traffic management flags tell which of the 4 traffic groups of the system this channel serves:
    //!< they are announced, the channel does not filter the addresses
    struct Roaming
    {
        bool m_enabled = false;
        unsigned m_localId = 0;             //!< 0-511
        unsigned m_coverageZone = 0;        //!< 0-31
        unsigned m_countryCode = 0;         //!< 0-1023 (ITU-T E.212)
        unsigned m_trafficFlags = 0xf;      //!< 4 flags, T3 to T0
    };

    void setRoaming(const Roaming& roaming) { m_roaming = roaming; }
    const Roaming& roaming() const { return m_roaming; }

    //! Date and time in block information words 001 (date), 010 (time) and 101 A = 0101 (time zone, daylight
    //! saving time, extended seconds), STD-43A 3.7.2. The time is the local time: the frame grid time (UTC, frame
    //! index times 1.875 s from the Unix epoch), plus the offset, the zone and one hour of daylight saving time.
    //! The words fill the block information words 2 to 4 left free (at most 3, after the system ID words of
    //! roaming) in every phase of every frame, so frame 0 of cycle 0 has a time word in every phase. When they
    //! do not all fit they take turns: the frame index modulo 3 chooses the first. 3 is prime to 2^collapse, so
    //! a pager listening to one frame in 2^collapse receives all three within 3 of its frames
    struct TimeService
    {
        enum Reference {
            FrameZero,      //!< The time of frame 0 of the cycle (the start of its bit sync 1): STD-43A 3.7.2
            CurrentFrame    //!< The time of the frame that carries the words
        };

        bool m_enabled = false;
        Reference m_reference = FrameZero;
        int m_zoneMinutes = 0;          //!< Offset of the standard time of the zone from UTC (Table 3.7.2-3)
        bool m_sendZone = true;         //!< Send the time zone word (101)
        bool m_daylightSaving = false;  //!< The time is daylight saving time: one hour ahead, L0 = 0
        int64_t m_offsetSeconds = 0;    //!< Added to the frame grid time: sets any date and time
    };

    //!< False and an error if the zone is not in Table 3.7.2-3
    bool setTimeService(const TimeService& time, std::string& error);
    const TimeService& timeService() const { return m_time; }
    //!< Local date and time of a frame as sent (reference, offset, zone and daylight saving applied): seconds
    //!< from the epoch and the 1/64 minute steps of the minute
    int64_t localTimeMs(uint64_t frameIndex) const;
    //!< Collapse 0-7, a pager listens to one frame in 2^collapse. False and error if out of range
    bool setCollapse(int collapse, std::string& error);
    int collapse() const { return m_collapse; }
    //!< One entry per frame (128), non zero = reserved. Frames 0-3 can not be reserved: the table
    //!< is not applied and false is returned. An empty table clears all reservations
    bool setReservedFrames(const std::vector<uint8_t>& table, std::string& error);
    const std::vector<uint8_t>& reservedFrames() const { return m_reserved; }

    //!< Queue a message. Rejected if the capcode or text is invalid, the message does not fit
    //!< in a frame, or every frame the capcode is eligible in is reserved. A HEX/binary or secure
    //!< message longer than a frame is split into fragments (RadioPagingFlex::fragmentMessage), sent
    //!< in successive frames of the capcode under the one id
    EnqueueResult enqueue(const TxMessage& message);
    //!< Queued messages and fragments still to send
    size_t pending() const { return m_queue.size(); }
    //!< Ids of the queued messages that no frame can carry with the current collapse and reservations
    std::vector<uint32_t> unschedulable() const;
    //!< Remove a queued message. False if the id is not queued
    bool remove(uint32_t id);
    //!< Queued messages a frame of the frame index would carry if it had room (their capcode
    //!< listens to the frame, the frame is not reserved)
    size_t eligible(uint64_t frameIndex) const;
    //!< The frame of the frame index has content that must go in it: a fragment after the first of a message
    //!< (the pager waits for it), or the time words of frame 0 of cycle 0
    bool mandatory(uint64_t frameIndex) const;
    //!< Block information words 2 to 4 of a phase of the frame (system ID of roaming, date and time), encoded
    std::vector<uint32_t> extraBlockInfo(uint64_t frameIndex, int phase) const;

    //!< The frame of a frame index. Messages placed in it are removed from the queue
    Frame buildFrame(uint64_t frameIndex);

private:
    struct Item
    {
        uint32_t m_id;      //!< The same for all the fragments of a message
        TxMessage m_message;    //!< The text of the fragment
        int m_number;       //!< -1 until the first placement attempt; the fragments take the number of the first
        int m_fragment = 0;     //!< Fragment index (HEX/binary and secure), in queue order
        int m_fragments = 1;
        int m_signature = -1;   //!< HEX/binary first fragment: signature of the whole message
        int m_phase = -1;       //!< Fragments after the first: the phase of the first fragment, -1 until it is sent
    };

    bool schedulable(uint64_t capcode) const;
    //!< A fragment goes in a frame after the one of the fragment before it (no earlier fragment is queued);
    //!< the first fragment of a fragmented message waits for a fragmented message to the same address
    //!< queued before it (STD-43A 3.8.5 (2))
    bool fragmentReady(const Item& item) const;
    //!< The item can go in the frame: not reserved, and a frame of the pager (its collapse) for a first
    //!< fragment or an unfragmented message, any frame for the fragments after the first (STD-43A 4.2.1)
    bool frameFor(const Item& item, uint32_t frame) const;
    static RadioPagingFlex::PhaseMessage phaseMessage(const Item& item);

    RadioPagingFlex::Mode m_mode;
    bool m_inverted;
    int m_collapse;
    Roaming m_roaming;
    TimeService m_time;
    std::vector<uint8_t> m_reserved;
    std::list<Item> m_queue;
    uint32_t m_nextId;
    RadioPagingFlexScheduler::NumberCounter m_numbers;
};

#endif // MODEMRADIOPAGING_RADIOPAGINGFLEXSCHEDULER_H
