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

#ifndef MODEMAMPS_AMPSCHANNEL_H_
#define MODEMAMPS_AMPSCHANNEL_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ampsfocc.h"
#include "ampsframe.h"
#include "ampsfsk.h"
#include "ampsfvc.h"
#include "ampsrecc.h"
#include "ampsspeech.h"
#include "ampssystem.h"
#include "ampstrace.h"
#include "ampsvoice.h"
#include "biquad.h"
#include "export.h"

// The signal processing of one AMPS carrier, as osmocom-analog's amps dsp.c (sender_send,
// sender_receive): what it sends and receives follows the mode of its channel in the AmpsSystem.
// The radio side is the frequency deviation in Hz at the channel rate (the carrier radio's audio).
// - control: the FOCC from the system's messages; the RECC to the system
// - voice: speech and SAT, SAT alone, or FVC orders in place of the speech; the SAT and signalling
//   tone of the phone, its RVC orders and its speech to the system
// What is received passes the DC filter first (osmocom: 300 Hz, before the FSK and the SAT). A change
// of mode starts the receivers and the modulator afresh (osmocom's amps_set_dsp_mode).
namespace modemamps
{

class MODEMAMPS_API AmpsChannel
{
public:
    AmpsChannel(AmpsSystem& system, int index, double sampleRate);
    AmpsChannel(const AmpsChannel&) = delete;
    AmpsChannel& operator=(const AmpsChannel&) = delete;

    int index() const { return m_index; }
    // a block from the radio, then the block of the same length to the radio
    void receive(const float* samples, int length);
    void render(float* samples, int length);

    // a line for each message on the air, the SAT and the signalling tone of the phone, the keying
    // of a voice channel (ampstrace); toPhone: sent, else received
    using TraceTap = std::function<void(bool toPhone, const std::string& text)>;
    void setTraceTap(TraceTap tap) { m_traceTap = std::move(tap); }

    // the speech of the channel's call, at the levels on the air (as NMT's NmtChannel::Speech):
    // 1.0 the speech level of 2.9 kHz, the limit the 12 kHz of the voice
    struct Speech
    {
        uint64_t call = 0;                      // counts the calls of the channel; 0: none yet
        bool live = false;                      // the call is still on
        std::string number;                     // the phone's 10 digits
        modemcellular::SpeechStats rxRecent, txRecent;     // the last 2 s
        modemcellular::SpeechStats rxCall, txCall;         // the whole call
        uint64_t limited = 0;                   // blocks the transmitter's limiter scaled in the call
    };
    Speech speech() const;

private:
    bool burst(const ReverseBurst& burst);
    void trace(bool toPhone, const std::string& text);
    void meterMode(AmpsMode mode);

    AmpsSystem& m_system;
    int m_index;
    bool m_control;
    int m_scc;
    double m_sampleRate;
    TraceTap m_traceTap;
    FvcTrace m_fvcTrace;
    uint64_t m_received = 0;            // samples
    uint64_t m_toneSince = 0;
    int m_tracedSat = -1;               // the SAT last shown, -1 none or lost
    std::string m_calledAddress;        // the RVC's called address being received
    AmpsMode m_rxMode;
    AmpsMode m_txMode;
    modemcellular::BiquadHighpass m_dcFilter;
    std::vector<float> m_filtered;
    // control
    ReverseReceiver m_reverse;
    ReccAssembler m_assembler;
    FoccTransmitter m_focc;
    // voice
    SatDetector m_sat;
    SatTone m_satTone;
    AmpsRxSpeech m_rxSpeech;
    AmpsTxSpeech m_txSpeech;
    modemcellular::SpeechMeter m_rxMeter;
    modemcellular::SpeechMeter m_txMeter;
    uint64_t m_meterCall = 0;
    bool m_meterLive = false;
    std::string m_meterNumber;
    uint64_t m_meterLimitedStart = 0;
    FvcTransmitter m_fvc;
    // the FSK of either
    ManchesterModulator m_modulator;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSCHANNEL_H_
