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

#ifndef MODEMRADIOPAGING_RADIOPAGINGGSCRECEIVER_H
#define MODEMRADIOPAGING_RADIOPAGINGGSCRECEIVER_H

#include <deque>

#include "export.h"
#include "radiopagingfskdemod.h"
#include "radiopaginggscdecoder.h"

// Sequential Golay receiver: FM discriminator samples to messages of both polarities.
//
// One demodulator at 600 baud, the rate of commas and data blocks; the 300 bit/s words
// are two symbols per bit and are decided by the decoders on the sum of the two soft
// symbols. The demodulator starts with acquisition loop gains and is locked (tracking
// gains) while a decoder is synchronised, that is from the confirmation of the preamble
// (three preamble words) to the end of the transmission.
class MODEMRADIOPAGING_API RadioPagingGscReceiver
{
public:
    static const int m_baud = 600;

    struct Status
    {
        bool m_locked;                              //!< Demodulator locked (a transmission is being received)
        RadioPagingGscDecoder::Status m_normal;
        RadioPagingGscDecoder::Status m_inverted;
        float m_deviation;                          //!< Measured deviation, discriminator units
    };

    RadioPagingGscReceiver();

    //!< sampleRate in Hz
    void create(double sampleRate);
    void reset();
    void setPolarities(bool normal, bool inverted) { m_normalEnabled = normal; m_invertedEnabled = inverted; }
    //!< Enable the detection of the non battery saver pattern. Off by default
    void setNonBatterySaver(bool enable);

    void process(float sample);
    //!< The signal is gone: finish the transmissions in progress
    void flush();

    bool hasMessage() const { return !m_messages.empty(); }
    RadioPagingGscDecoder::Message takeMessage();
    Status getStatus() const;

private:
    bool m_normalEnabled;
    bool m_invertedEnabled;
    RadioPagingFskDemod m_demod;
    RadioPagingGscDecoder m_normal;
    RadioPagingGscDecoder m_inverted;
    std::deque<RadioPagingGscDecoder::Message> m_messages;

    void collect();
};

#endif // MODEMRADIOPAGING_RADIOPAGINGGSCRECEIVER_H
