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

#ifndef MODEMRADIOPAGING_RADIOPAGINGFLEXDECODER_H
#define MODEMRADIOPAGING_RADIOPAGINGFLEXDECODER_H

#include <cstdint>
#include <string>
#include <vector>

#include "export.h"
#include "radiopagingberwindow.h"
#include "radiopagingflex.h"

// FLEX frame decoder: codewords of a received frame to messages.
//
// The input is a whole frame: for each phase the 88 received codewords of the data part.
// Symbol timing, sync search and slicing are not part of this class. The decoder keeps the
// state that spans frames (fragments of long messages, message history for duplicates and
// retransmissions, temporary group assignments, repeated subframes, network time) for each
// of the two polarities separately: a frame received with inverted polarity belongs to
// a different logical network than a frame received with normal polarity.
//
// Codeword convention of RxPhase: the same as the words of RadioPagingFlex::buildPhase and
// RadioPagingFlex::assemble. A word is in transmission order, bit 31 is the first bit received, the
// BCH code word of RadioPagingBCH is the word itself. The 88 words of a phase are 11 blocks of
// 8 words, each block interleaved (see RadioPagingFlex::interleaveBlock): the decoder
// deinterleaves them. A frame made by RadioPagingFlexEncoder is fed back with the words
// of RadioPagingFlex::PhaseResult.
class MODEMRADIOPAGING_API RadioPagingFlexDecoder
{
public:
    enum WordStatus {
        WordNotReceived,
        WordClean,
        WordCorrected,
        WordUncorrectable   //!< Includes erased words
    };

    enum AddressType {
        AddressShort,
        AddressLong,
        AddressNetwork,
        AddressTemporary,
        AddressOperator,        //!< Operator messaging address
        AddressInfoService,
        AddressReserved,
        AddressUnknown
    };

    enum MessageType {
        TypeUnknown,
        TypeTone,               //!< Address without vector
        TypeAlphanumeric,
        TypeSecure,
        TypeNumeric,
        TypeSpecialNumeric,
        TypeNumberedNumeric,
        TypeNumberedSpecialNumeric,
        TypeBinary,             //!< Hex / binary vector
        TypeShortMessage,       //!< Message in the vector word (vector type 2)
        TypeShortInstruction
    };

    enum Check {
        CheckNone,              //!< Not applicable or not evaluated
        CheckOk,
        CheckFailed,
        CheckIncomplete         //!< Words of the message are lost
    };

    enum FragmentKind {
        FragmentUnknown,        //!< Header word lost
        FragmentComplete,       //!< One message, C = 0, F = 3
        FragmentFirst,          //!< C = 1, F = 3
        FragmentMiddle,         //!< C = 1, F != 3
        FragmentLast            //!< C = 0, F != 3
    };

    //! Codewords of one phase of the data part
    struct RxPhase
    {
        char m_name = 'A';                  //!< 'A' to 'D'
        std::vector<uint32_t> m_words;      //!< 88 interleaved codewords (see class description)
        //! 88 flags (may be empty = none): non zero if the codeword is lost (carrier lost). The
        //! index is the codeword index after deinterleaving, block * 8 + word in block
        std::vector<uint8_t> m_erased;
    };

    struct RxFrame
    {
        RadioPagingFlex::Mode m_mode = RadioPagingFlex::Mode1600_2;
        bool m_inverted = false;            //!< Frame received with inverted polarity
        bool m_hasFiw = false;              //!< m_fiw holds the received frame information word
        uint32_t m_fiw = 0;                 //!< Received FIW codeword (transmission order). Decoded and checked
        unsigned m_cycle = 0;               //!< 0-14, used if m_hasFiw is false
        unsigned m_frame = 0;               //!< 0-127, used if m_hasFiw is false
        bool m_roaming = false;             //!< FIW n flag, used if m_hasFiw is false
        bool m_repeat = false;              //!< FIW r flag (multiple transmission), used if m_hasFiw is false
        unsigned m_traffic = 0;             //!< FIW t3-t0, used if m_hasFiw is false
        std::vector<RxPhase> m_phases;      //!< One entry for each phase of the mode
        double m_quality = 1.0;             //!< Frame level signal quality 0-1 from the receiver, copied to messages
    };

    //! Extra block information word (the words after the first one)
    struct ExtraBiw
    {
        enum Kind { Lost, Ssid1, Date, Time, SystemInfo, Ssid2, Reserved };

        Kind m_kind = Lost;
        int m_index = 0;                    //!< Word index in the phase
        uint32_t m_word = 0;                //!< Data bits
        // Ssid1
        unsigned m_localId = 0;
        unsigned m_coverageZone = 0;
        // Date
        int m_year = 0;
        unsigned m_month = 0;
        unsigned m_day = 0;
        bool m_dateValid = false;
        // Time
        unsigned m_hour = 0;
        unsigned m_minute = 0;
        unsigned m_second7_5 = 0;           //!< Seconds in steps of 7.5 s
        bool m_timeValid = false;
        // SystemInfo
        unsigned m_infoType = 0;            //!< A field: 0-3 system message, 4/5 time zone, 6 channel set up
        unsigned m_info = 0;                //!< 10 bit I field
        unsigned m_zone = 0;                //!< Time zone code (A = 4, 5)
        int m_zoneMinutes = 0;              //!< Offset from UTC
        unsigned m_dst = 0;                 //!< 0 = daylight saving time, 1 = standard
        unsigned m_extendedSeconds = 0;     //!< 0-7 steps of 0.9375 s
        unsigned m_frameOffset = 0;         //!< Channel set up (A = 6)
        unsigned m_maxCarryOn = 0;
        bool m_nidMessage = false;
        bool m_systemMessage = false;
        // Ssid2
        unsigned m_country = 0;
        unsigned m_tmf = 0;
    };

    //! Block information of one phase of the last decoded frame
    struct BlockInfo
    {
        char m_phase = 'A';
        bool m_valid = false;               //!< First block information word usable and offsets valid
        bool m_idle = false;                //!< No address
        unsigned m_priorityWords = 0;
        unsigned m_addressStart = 0;        //!< Word index of the first address word
        unsigned m_vectorStart = 0;         //!< Word index of the first vector word
        unsigned m_carryOn = 0;
        unsigned m_collapse = 0;
        int m_wordsCorrected = 0;
        int m_wordsUncorrectable = 0;
        bool m_waiting = false;             //!< Repeated subframe held back until more copies arrive
        std::vector<ExtraBiw> m_extra;
    };

    struct FrameReport
    {
        bool m_fiwValid = false;
        unsigned m_cycle = 0;
        unsigned m_frame = 0;
        bool m_roaming = false;
        bool m_repeat = false;
        int m_transmissions = 1;            //!< 1-4
        int m_tdCollapse = -1;              //!< -1 = system collapse, else 5-7
        unsigned m_traffic = 0;             //!< Low traffic flags t3-t0 when m_repeat is false
        std::vector<BlockInfo> m_blocks;
    };

    //! Message found in a phase
    struct Message
    {
        // Address
        uint64_t m_capcode = 0;
        std::string m_address;              //!< Capcode as 10 digits, zero padded
        bool m_longAddress = false;
        AddressType m_addressType = AddressUnknown;
        bool m_priority = false;            //!< Address in the priority part of the address field

        // Type
        MessageType m_type = TypeUnknown;
        std::string m_typeName;             //!< alphanumeric, secure, numeric, special_numeric, numbered_numeric,
                                            //!< numbered_special_numeric, tone, binary, short_message,
                                            //!< short_instruction, unknown

        // Content. Raw payload: the message words (header words included, vector words excluded; the first
        // word of a long address message is the second vector word) in message order, 21 data bits d0-d20
        // each, d0 first, bits packed 8 per byte with the first bit in the most significant bit. Words
        // that were lost are zero. For a reassembled message the words of all fragments follow each other.
        // A short message and a short instruction have no message words: their payload is the vector word(s)
        std::vector<uint8_t> m_rawData;
        int m_rawBits = 0;
        //! Alphanumeric: 7-bit characters (the default charset), lost characters are '?', unprintable characters
        //! stay (or two bytes per character if the kanji mode is set). Numeric: digits 0-9 . U space - ] [,
        //! lost digits '?'. Binary, and secure messages of type t = 01, 10, 11: hex digits of the data bits (see
        //! m_hexNibbleBits). Short message: digits. Else empty
        std::string m_text;
        //! m_text is hex: the data bits as one stream in transmission order (d0 of each word first), this many bits
        //! a digit, 0 if m_text is not hex. A digit with a lost bit is '?'; for a secure message a lost fragment is '|'
        int m_hexNibbleBits = 0;
        bool m_hexLsbFirst = true;          //!< The first bit of a digit is its least significant bit
        bool m_payloadUnknown = false;      //!< The layout of the payload is not published
        std::string m_wordMap;              //!< One character for each message word: . clean, c corrected, X lost; fragments split by |

        // Errors in the address, vector and message words
        int m_wordsCorrected = 0;
        int m_bitErrors = 0;                //!< Bits corrected
        int m_wordsUncorrectable = 0;
        int m_parityCorrections = 0;        //!< Words where only the parity bit was wrong (counted in m_wordsCorrected)

        // Reception
        RadioPagingFlex::Mode m_mode = RadioPagingFlex::Mode1600_2;
        int m_baud = 1600;
        int m_levels = 2;
        int m_bitRate = 1600;
        bool m_inverted = false;
        double m_quality = 1.0;
        unsigned m_cycle = 0;               //!< Frame of the last fragment
        unsigned m_frame = 0;
        unsigned m_firstCycle = 0;          //!< Frame of the first fragment
        unsigned m_firstFrame = 0;
        char m_phase = 'A';

        // Message header fields
        int m_messageNumber = -1;           //!< N, -1 if the message has none
        bool m_retrieval = false;           //!< R flag (alphanumeric, binary, numbered numeric, short message type 2)
        bool m_mailDrop = false;            //!< M flag of an alphanumeric first fragment
        bool m_special = false;             //!< S flag of numbered numeric: special format
        int m_secureType = -1;              //!< t1 t0 of a secure message
        bool m_registrationAck = false;     //!< Secure message with operation code "="
        bool m_kanji = false;               //!< Characters are 16 bit, two bytes each
        bool m_recovered = false;           //!< Words taken from an earlier copy of the message
        bool m_duplicate = false;           //!< Retransmission of a message already output (with m_recovered: it repaired it)
        Check m_checksum = CheckNone;       //!< K checksum of the message words
        Check m_signature = CheckNone;      //!< Signature of the text of alphanumeric and binary messages

        // Fragments
        bool m_fragmented = false;          //!< Part of a message sent in fragments
        bool m_isFragment = false;          //!< This is one fragment (only if fragments are reported)
        FragmentKind m_fragment = FragmentUnknown;
        int m_fragmentNumber = 0;           //!< F field of the last fragment
        int m_fragmentCount = 1;            //!< Fragments received
        bool m_complete = true;             //!< False: fragments are missing, timed out or the header is lost
        bool m_fragmentGap = false;         //!< The fragment numbers show a lost fragment

        // Binary
        int m_blocking = 0;                 //!< B field: bits per character, 0 = 16
        bool m_displayRtl = false;          //!< D
        bool m_headerMessage = false;       //!< H
        bool m_statusInfo = false;          //!< I
        int m_dataBits = 0;                 //!< Data bits in m_text without the termination bits (binary, secure hex)

        // Short message (vector type 2)
        int m_shortType = -1;               //!< t: 0 digits, 1 source, 2 source with number, 3 reserved
        int m_shortSource = -1;             //!< S2-S0 of types 1 and 2
        bool m_toneOnly = false;            //!< Type 0, every digit a space: the tone only payload
        // Type 0 for a network address (STD-43A Table 3.9.2-1, 6.1.2): the network ID, no digits
        bool m_networkId = false;
        int m_nidServiceArea = -1;          //!< A4-A0
        int m_nidMultiplier = -1;           //!< M2-M0
        int m_nidTrafficFlags = -1;         //!< F3-F0

        // Short instruction
        int m_instructionType = -1;         //!< 0 temporary address, 1 system event
        unsigned m_instructionData = 0;     //!< 14 bit
        int m_instructionSlot = -1;         //!< Temporary address slot
        int m_instructionFrame = -1;        //!< Frame of the group message
        unsigned m_eventFlags = 0;

        // Temporary address (group)
        int m_tempSlot = -1;                //!< Slot 0-15 if the address is a temporary address
        bool m_groupTracked = false;        //!< The group assignment was received
        std::vector<uint64_t> m_groupMembers;

        // Network address with a vector other than the short message vector: a system message of the NID
        // (FLEX-93001 3.9.2 p.44). A secure message of type t = 01 is the System NID Change Instruction
        // (FLEX-93001 3.9.7 p.48), its data layout is not published (m_payloadUnknown, as any secure t = 01)
        bool m_nidSystemMessage = false;
        bool m_nidChange = false;

        // Operator messaging address
        std::string m_operatorSubtype;
        std::string m_operatorCategory;
        int64_t m_operatorPayload = -1;     //!< First message word, -1 if lost
    };

    struct Status
    {
        uint32_t m_frames = 0;
        uint32_t m_fiwFailures = 0;
        uint32_t m_codewords = 0;
        uint32_t m_codewordsClean = 0;
        uint32_t m_codewordsCorrected = 0;
        uint32_t m_codewordsUncorrectable = 0;  //!< Includes erased
        uint32_t m_codewordsErased = 0;
        uint32_t m_messages = 0;
        uint32_t m_duplicates = 0;              //!< Retransmissions of messages already output (marked duplicate)
        uint32_t m_recovered = 0;               //!< Messages repaired with words of an earlier copy
        uint32_t m_incomplete = 0;
        uint32_t m_invalidVectors = 0;
        RadioPagingBerWindow m_ber;             //!< Of the frames received recently
    };

    //! Network time and identification, from the block information words. A value is valid
    //! when it was received several times with the same content
    struct NetworkTime
    {
        bool m_dateValid = false;
        int m_year = 0;
        unsigned m_month = 0;
        unsigned m_day = 0;
        bool m_timeValid = false;
        unsigned m_hour = 0;
        unsigned m_minute = 0;
        unsigned m_second7_5 = 0;           //!< As received, steps of 7.5 s
        bool m_zoneValid = false;
        unsigned m_zone = 0;
        int m_zoneMinutes = 0;
        unsigned m_dst = 0;                 //!< 0 = daylight saving time, 1 = standard
        unsigned m_extendedSeconds = 0;
        int m_mode = -1;                    //!< -1 unknown, 0 time of frame 0 (frame offset added), 1 time of the frame
        //! Time of the last decoded frame: hour, minute, seconds (valid if m_timeValid)
        unsigned m_currentHour = 0;
        unsigned m_currentMinute = 0;
        double m_currentSecond = 0.0;
        // Identification
        bool m_ssid1Valid = false;
        unsigned m_localId = 0;
        unsigned m_coverageZone = 0;
        bool m_ssid2Valid = false;
        unsigned m_country = 0;
        unsigned m_tmf = 0;
    };

    RadioPagingFlexDecoder();

    //! Clears the state of all polarities and the counters
    void reset();
    //! Messages of the frame, and fragments that timed out (marked incomplete)
    std::vector<Message> decodeFrame(const RxFrame& frame);
    //! Fragments in progress as incomplete messages, repeated subframes waiting for copies are
    //! decoded with the copies received. The message history is kept
    std::vector<Message> flush();

    //! A temporary address (group) assigned and waiting for its group message
    struct GroupSlot
    {
        int m_slot = 0;
        std::vector<uint64_t> m_members;
        unsigned m_targetFrame = 0;     //!< Frame of the group message
        unsigned m_setupCycle = 0;      //!< Cycle and frame of the assignment
        unsigned m_setupFrame = 0;
    };

    const Status& status() const { return m_status; }
    const FrameReport& lastFrame() const { return m_report; }
    NetworkTime networkTime(bool inverted = false) const;
    //!< Groups in use on the network of the polarity
    std::vector<GroupSlot> groups(bool inverted) const;
    //!< Fragmented messages being reassembled on the network of the polarity
    int reassemblies(bool inverted) const;

    //! Report each fragment as a message too. Default off: the reassembled message only
    void setReportFragments(bool enable) { m_reportFragments = enable; }
    //! 16-bit characters (Shift-JIS) in alphanumeric messages. Default off
    void setKanji(bool enable) { m_kanji = enable; }
    //! Year used to place the 5 bit year of the date in a 32 year period (|year - reference| <= 16). Default 2026
    void setReferenceYear(int year) { m_referenceYear = year; }
    //! The year field is the year - 1994 as sent, no period selection
    void setFixedYearBase(bool enable) { m_fixedYear = enable; }

    static const char *addressTypeName(AddressType type);
    static const char *messageTypeName(MessageType type);

    static const int m_reassemblySlots = 16;
    static const int m_reassemblyMaxLength = 4096;
    static const int m_reassemblyTimeout = 64;      //!< Frames without a fragment
    static const int m_historySize = 256;
    static const int m_historyWindow = 1920;        //!< Frames
    static const int m_tempSlots = 16;
    static const int m_tempTimeout = 128;           //!< Frames from the assignment to the group message
    static const int m_voteThreshold = 3;
    static const int m_voteRing = 8;

private:
    struct Word
    {
        uint32_t m_data = 0;                //!< 21 data bits d0-d20 (d0 in bit 0)
        uint8_t m_status = WordNotReceived;
        uint8_t m_bits = 0;                 //!< Bits corrected
        bool m_parity = false;              //!< Only the parity bit was wrong
    };

    struct FrameContext
    {
        unsigned m_cycle = 0;
        unsigned m_frame = 0;
        RadioPagingFlex::Mode m_mode = RadioPagingFlex::Mode1600_2;
        int m_baud = 1600;
        int m_levels = 2;
        int m_bitRate = 1600;
        int m_pol = 0;
        double m_quality = 1.0;
        int m_transmissions = 1;
        int m_tdCollapse = -1;
    };

    struct AddressContext
    {
        uint64_t m_capcode = 0;
        bool m_long = false;
        AddressType m_type = AddressUnknown;
        uint32_t m_word = 0;
        int m_index = 0;                    //!< Word index of the first address word
        bool m_priority = false;
        int m_tempSlot = -1;
    };

    //! What the reassembly needs of one fragment besides the message fields
    struct FragmentInfo
    {
        uint32_t m_signature = 0;           //!< Signature field of the first fragment
        uint32_t m_signatureSum = 0;        //!< Sum of the characters of this fragment
        bool m_wordsOk = true;              //!< No lost words in the fragment
        bool m_kanji = false;
        std::vector<uint32_t> m_raw;        //!< Message words of the fragment
        std::vector<int8_t> m_bits;         //!< Data bits of a secure hex fragment: 0, 1, -1 lost
    };

    static const int m_maxSubframeWords = 44;   //!< Subframe of a 2x multiple transmission

    struct SubframeCopy
    {
        bool m_received = false;
        int m_uncorrectable = 0;
        Word m_words[m_maxSubframeWords];
    };

    //! The transmissions of one subframe content of a phase (multiple transmission): the k-th
    //! copy is subframe k of the frame (k - 1) repeat intervals after the first transmission
    struct SubframeStore
    {
        uint32_t m_origin = 0;              //!< Frame of the first transmission, 0-1919 in the hour
        uint32_t m_interval = 1;            //!< Repeat interval, frames
        bool m_decoded = false;             //!< Decoded: later copies are only collected
        int m_expected = 0;                 //!< Number of transmissions
        int m_received = 0;
        int m_words = 0;
        char m_phase = 'A';
        FrameContext m_context;             //!< Of the first transmission frame
        SubframeCopy m_copies[4];
    };

    struct TempSlot
    {
        bool m_active = false;
        std::vector<uint64_t> m_members;
        unsigned m_targetFrame = 0;
        unsigned m_setupFrame = 0;
        unsigned m_setupCycle = 0;
    };

    struct Reassembly
    {
        bool m_active = false;
        uint64_t m_capcode = 0;
        int m_number = 0;
        int m_expected = 0;                 //!< Next F
        int m_vector = 0;                   //!< Vector type of the stream
        uint32_t m_signature = 0;
        uint32_t m_signatureSum = 0;
        bool m_signatureValid = true;
        uint32_t m_lastFrame = 0;
        uint32_t m_lastCycle = 0;
        int m_fragments = 0;
        bool m_gap = false;
        bool m_kanji = false;
        bool m_checksumFailed = false;
        std::string m_text;
        std::vector<uint32_t> m_raw;
        std::vector<int8_t> m_bits;         //!< Secure hex: data bits of the fragments, 2 where fragments are lost
        Message m_message;                  //!< Fields of the first fragment, counters of all
    };

    struct HistoryEntry
    {
        bool m_active = false;
        uint64_t m_capcode = 0;
        int m_number = 0;
        int m_vector = 0;
        int m_phase = 0;
        int m_wordCount = 0;
        int m_fragCount = 0;
        bool m_long = false;
        int m_blocking = 0;
        bool m_hasUncorrectable = false;
        uint32_t m_frame = 0;
        Word m_words[RadioPagingFlex::m_wordsPerFrame];
    };

    struct BiwState
    {
        bool m_ssid1 = false;
        unsigned m_localId = 0;
        unsigned m_coverage = 0;
        bool m_ssid2 = false;
        unsigned m_country = 0;
        unsigned m_tmf = 0;
        bool m_seenDate = false;
        int m_year = 0;
        unsigned m_month = 0;
        unsigned m_day = 0;
        bool m_seenTime = false;
        unsigned m_hour = 0;
        unsigned m_minute = 0;
        unsigned m_second = 0;
        bool m_seenZone = false;
        unsigned m_zone = 0;
        int m_zoneMinutes = 0;
        unsigned m_dst = 0;
        unsigned m_extSec = 0;
        int m_sysMsgType = -1;              //!< A field of a system message word of this frame, else -1
        int m_voteTimeCount = 0;
        unsigned m_voteHour = 0;
        unsigned m_voteMinute = 0;
        int m_dateRingCount = 0;
        int m_dateRingIndex = 0;
        int m_dateYear[8];
        unsigned m_dateMonth[8];
        unsigned m_dateDay[8];
        int m_zoneRingCount = 0;
        int m_zoneRingIndex = 0;
        unsigned m_zoneZone[8];
        unsigned m_zoneDst[8];
        unsigned m_zoneExt[8];
        int m_modeDetected = -1;
        int m_modeVotesStd = 0;
        int m_modeVotesDir = 0;
        unsigned m_modeMinute = 0;
        unsigned m_modeSecond = 0;
        unsigned m_modeFrame = 0;
        unsigned m_modeCycle = 0;
        bool m_modeValid = false;
    };

    struct PolarityState
    {
        TempSlot m_temp[16];
        std::vector<Reassembly> m_reassembly;
        std::vector<HistoryEntry> m_history;
        std::vector<SubframeStore> m_stores[4];     //!< Per phase, contents waiting for copies
        BiwState m_biw;
        int m_lastCollapse = 0;             //!< System collapse of the last BIW 1 that decoded (clean or corrected)
    };

    Status m_status;
    FrameReport m_report;
    FrameContext m_ctx;
    PolarityState m_pol[2];
    bool m_reportFragments;
    bool m_kanji;
    bool m_fixedYear;
    int m_referenceYear;
    unsigned m_lastFrameNumber[2];

    // Current phase
    Word m_w[RadioPagingFlex::m_wordsPerFrame];
    char m_name;
    std::vector<Message> *m_out;
    BlockInfo *m_block;

    PolarityState& pol() { return m_pol[m_ctx.m_pol]; }
    uint32_t absFrame() const { return m_ctx.m_cycle * 128 + m_ctx.m_frame; }

    static Word decodeWord(uint32_t raw, bool erased);
    bool decodeFiw(const RxFrame& frame);
    void decodePhase(const RxPhase& phase);
    void decodeSubframes(int phaseIndex);
    void combineSubframes(SubframeStore& store, Word *out, int& remaining) const;
    void parsePhase();
    void parseExtraBiw(int index);
    void walkAddresses(unsigned addressStart, unsigned vectorStart, unsigned priorityWords);
    void processVector(const AddressContext& a, int j);
    void decodeShortInstruction(Message& proto, const AddressContext& a, int j);
    void decodeAlpha(Message& proto, const AddressContext& a, int j, int vecType, int mw1, int mw2);
    void decodeNumeric(Message& proto, const AddressContext& a, int j, int vecType, int mw1, int mw2);
    void decodeShortMessage(Message& proto, const AddressContext& a, int j);
    void decodeBinary(Message& proto, const AddressContext& a, int j, int mw1, int mw2);
    void handleTemporaryAddress(const AddressContext& a, int j, int vecType, int mw1, Message& proto);
    void handleFragment(Message& msg, const AddressContext& a, int vector, const FragmentInfo& info);
    static Check combineChecks(Check a, Check b);

    // Messages
    Message newMessage(const AddressContext& a) const;
    void countWord(Message& msg, const Word& word) const;
    void countWords(Message& msg, int first, int last) const;
    static void setRaw(Message& msg, const std::vector<uint32_t>& words);
    void emitMessage(Message& msg);
    bool wordOk(int index) const;

    // Fragment reassembly
    int reassemblyFind(uint64_t capcode, int number);
    int reassemblyAlloc(uint64_t capcode, int number, int vector);
    void reassemblyInvalidate(uint64_t capcode, int excludeNumber);
    void reassemblyExpire();
    void emitIncomplete(Reassembly& slot);

    // History
    HistoryEntry *historyFind(uint64_t capcode, int number, int vector, int phase);
    HistoryEntry *historyStore(uint64_t capcode, int number, int vector, int phase, const Word *words, int wordCount,
        int fragCount, bool isLong, int blocking, uint32_t frame);
    void historyExpire();
    int checkRetransmission(uint64_t capcode, bool retrieval, int number, int vector, int phase, const Word *words,
        int wordCount, int fragCount, bool isLong, int blocking);

    // Network time
    void voteDate(int year, unsigned month, unsigned day);
    void voteTime(unsigned hour, unsigned minute, unsigned second);
    void voteZone(unsigned zone, unsigned dst, unsigned ext);
    static bool dateValid(int year, unsigned month, unsigned day);
    static void nextDay(int y, unsigned m, unsigned d, int& ny, unsigned& nm, unsigned& nd);
    static bool datesAgree(int y1, unsigned m1, unsigned d1, int y2, unsigned m2, unsigned d2);
    static bool timeMatchesCycle(unsigned minute, unsigned cycle);
    static int zoneMinutes(unsigned zone);

    void expireTempGroups();
    //!< Decode the combined copies of a subframe content in the context of its first transmission
    void decodeStore(SubframeStore& store);
};

#endif // MODEMRADIOPAGING_RADIOPAGINGFLEXDECODER_H
