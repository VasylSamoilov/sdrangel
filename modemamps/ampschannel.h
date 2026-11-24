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

#include <memory>
#include <vector>

#include "ampsfocc.h"
#include "ampsframe.h"
#include "ampsfsk.h"
#include "ampsfvc.h"
#include "ampsrecc.h"
#include "ampsspeech.h"
#include "ampssystem.h"
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

private:
    bool burst(const ReverseBurst& burst);

    AmpsSystem& m_system;
    int m_index;
    bool m_control;
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
    FvcTransmitter m_fvc;
    // the FSK of either
    ManchesterModulator m_modulator;
};

} // namespace modemamps

#endif // MODEMAMPS_AMPSCHANNEL_H_
