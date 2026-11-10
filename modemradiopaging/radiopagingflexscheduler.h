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
        std::string m_text;         //!< Ignored for tone
        bool m_priority = false;
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
        bool m_idle = false;        //!< No message in the frame
        uint32_t m_cycle = 0;
        uint32_t m_frame = 0;
        RadioPagingFlex::Mode m_mode = RadioPagingFlex::Mode1600_2;
        bool m_inverted = false;
        unsigned m_traffic = 0;     //!< Low traffic flags of the frame information word
        std::vector<uint32_t> m_sentIds;
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
    //!< Collapse 0-7, a pager listens to one frame in 2^collapse. False and error if out of range
    bool setCollapse(int collapse, std::string& error);
    int collapse() const { return m_collapse; }
    //!< One entry per frame (128), non zero = reserved. Frames 0-3 can not be reserved: the table
    //!< is not applied and false is returned. An empty table clears all reservations
    bool setReservedFrames(const std::vector<uint8_t>& table, std::string& error);
    const std::vector<uint8_t>& reservedFrames() const { return m_reserved; }

    //!< Queue a message. Rejected if the capcode or text is invalid, the message does not fit
    //!< in a frame, or every frame the capcode is eligible in is reserved
    EnqueueResult enqueue(const TxMessage& message);
    size_t pending() const { return m_queue.size(); }
    //!< Ids of the queued messages that no frame can carry with the current collapse and reservations
    std::vector<uint32_t> unschedulable() const;
    //!< Remove a queued message. False if the id is not queued
    bool remove(uint32_t id);
    //!< Queued messages a frame of the frame index would carry if it had room (their capcode
    //!< listens to the frame, the frame is not reserved)
    size_t eligible(uint64_t frameIndex) const;

    //!< The frame of a frame index. Messages placed in it are removed from the queue
    Frame buildFrame(uint64_t frameIndex);

private:
    struct Item
    {
        uint32_t m_id;
        TxMessage m_message;
        int m_number;       //!< -1 until the first placement attempt
    };

    bool schedulable(uint64_t capcode) const;

    RadioPagingFlex::Mode m_mode;
    bool m_inverted;
    int m_collapse;
    Roaming m_roaming;
    std::vector<uint8_t> m_reserved;
    std::list<Item> m_queue;
    uint32_t m_nextId;
    RadioPagingFlexScheduler::NumberCounter m_numbers;
};

#endif // MODEMRADIOPAGING_RADIOPAGINGFLEXSCHEDULER_H
