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

#ifndef MODEMNMT_NMTCHANNEL_H_
#define MODEMNMT_NMTCHANNEL_H_

#include <cstdint>
#include <functional>
#include <utility>

#include "nmtframe.h"
#include "nmtmodem.h"
#include "nmtnumbers.h"
#include "export.h"

namespace modemnmt
{

enum class ChannelType
{
    Cc,         // calling channel, frame 1a
    Cca,        // calling channel, group A, frame 1a' (NMT-900)
    Ccb,        // calling channel, group B, frame 1a'' (NMT-900)
    Tc,         // traffic channel, frame 4
    AcTc,       // access and traffic channel, frame 4b (NMT-900)
    CcTc,       // combined calling and traffic channel, frame 1b
    Test        // test channel, frame 30
};

struct ChannelConfig
{
    System system = System::Nmt450;
    ChannelType type = ChannelType::Cc;
    int channel = 1;                // radio channel number of this system
    int msPower = 1;                // power level of the mobile stations, 0..3
    uint8_t trafficArea = 0;        // two digits Y1Y2, region digit in the high nibble
    int areaNo = 0;                 // area number 1..4, 0 for none
    bool sendClock = false;         // send the time with the calling channel
    int batteryPeriod = 0;          // battery saving period code 0..9, 0 for none (phones never sleep)
    int batteryGroup = 8;           // battery saving group 0..8, 8 addresses all phones
    // NMT-900 channel band advertisement; channel numbers, 0 for none
    int ccBandStart = 0;
    int ccBandEnd = 0;
    int tcBandStart = 0;
    int tcBandEnd = 0;
    bool loopback = false;          // listen to our own frames (the receiver sees the exchange direction)
};

// Hour and minute of the local time.
using TimeOfDay = std::function<std::pair<int, int>()>;

// One NMT radio channel of a base station: the FFSK modem and the signalling of the channel. This
// is the idle channel only: it sends the idle frame of the channel type and passes everything it
// receives to the listener.
class MODEMNMT_API NmtChannel
{
public:
    struct ReceivedFrame
    {
        Frame frame;
        double quality;
        double level;
        uint64_t framesElapsed;
        bool leadingZeros;
    };
    using FrameListener = std::function<void(const ReceivedFrame&)>;

    // sampleRate of the audio, deviationFactor scales the signalling level to the transmitter.
    NmtChannel(const ChannelConfig& config, double sampleRate, double deviationFactor);
    NmtChannel(const NmtChannel&) = delete;
    NmtChannel& operator=(const NmtChannel&) = delete;

    // False if the configuration cannot be used: the modem and the frames then stay silent.
    bool valid() const { return m_valid; }
    const ChannelConfig& config() const { return m_config; }

    void setTimeOfDay(TimeOfDay timeOfDay) { m_timeOfDay = std::move(timeOfDay); }
    void setFrameListener(FrameListener listener) { m_listener = std::move(listener); }

    // Next frame of the idle channel, counts the frames sent. The additional information
    // alternates between channel band advertisement and battery saving if both are configured.
    Frame idleFrame();
    uint64_t framesSent() const { return m_framesSent; }

    // Audio towards the radio: frames back to back. Audio from the radio: frames are decoded and
    // given to the listener.
    int render(float* samples, int length, bool add) { return m_modem.render(samples, length, add); }
    void receive(const float* samples, int length) { m_modem.receive(samples, length); }

    // The frame as it came from the modem, for use without audio.
    void receiveFrame(const RxFrame& rx);

private:
    bool nextFrame(FrameBits& bits);
    bool bandAdvertisement() const;
    uint64_t additionalInfo(MessageType type);

    ChannelConfig m_config;
    bool m_valid;
    TimeOfDay m_timeOfDay;
    FrameListener m_listener;
    NmtModem m_modem;
    uint64_t m_framesSent;
};

} // namespace modemnmt

#endif // MODEMNMT_NMTCHANNEL_H_
