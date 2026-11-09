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

#include "nmtchannel.h"

namespace modemnmt
{

namespace
{

bool configValid(const ChannelConfig& c)
{
    const bool is900Type = c.type == ChannelType::Cca || c.type == ChannelType::Ccb || c.type == ChannelType::AcTc;

    return (c.system == System::Nmt900 || !is900Type)
        && c.channel >= 1
        && c.msPower >= 0 && c.msPower <= 3
        && c.areaNo >= 0 && c.areaNo <= 4
        && c.batteryPeriod >= 0 && c.batteryPeriod <= 9
        && c.batteryGroup >= 0 && c.batteryGroup <= 8
        && c.ccBandStart >= 0 && c.ccBandEnd >= 0 && c.tcBandStart >= 0 && c.tcBandEnd >= 0;
}

MessageType idleMessageType(ChannelType type)
{
    switch (type)
    {
    case ChannelType::Cc:
        return MessageType::Frame1a;
    case ChannelType::Cca:
        return MessageType::Frame1aA;
    case ChannelType::Ccb:
        return MessageType::Frame1aB;
    case ChannelType::Tc:
        return MessageType::Frame4;
    case ChannelType::AcTc:
        return MessageType::Frame4b;
    case ChannelType::CcTc:
        return MessageType::Frame1b;
    case ChannelType::Test:
    default:
        return MessageType::Frame30;
    }
}

// Bit 17 of the additional information: H6 bit 3, the mobile station may send overdecadic
// B-numbers (NMT Doc 450-1 para 4.3.3.12.5)
const uint64_t kOverdecadic = 0x20000;

} // namespace

NmtChannel::NmtChannel(const ChannelConfig& config, double sampleRate, double deviationFactor) :
    m_config(config),
    m_valid(configValid(config)),
    m_modem(sampleRate, deviationFactor),
    m_framesSent(0)
{
    m_modem.setFrameSource([this](FrameBits& bits) { return nextFrame(bits); });
    m_modem.setFrameSink([this](const RxFrame& rx) { receiveFrame(rx); });
}

bool NmtChannel::bandAdvertisement() const
{
    return m_config.system == System::Nmt900
        && (m_config.ccBandStart || m_config.ccBandEnd || m_config.tcBandStart || m_config.tcBandEnd);
}

uint64_t NmtChannel::additionalInfo(MessageType type)
{
    uint64_t h = 0;

    if (type == MessageType::Frame1a || type == MessageType::Frame1aA
     || type == MessageType::Frame1aB || type == MessageType::Frame1b)
    {
        // Channel band advertisement (H1=14, H2=0) and battery saving (H1=14, H2=11) are two
        // frames of additional information. If both are wanted they take turns.
        if (bandAdvertisement() && (m_config.batteryPeriod == 0 || (m_framesSent & 1) == 0))
        {
            // NMT Doc 900-1 para 4.3.3.13: H3H4/H5H6 limits of the calling channel band, H7H8/H9H10
            // of the traffic channel band. 00 means the basic band. On 1b H7 is J and the
            // traffic channel band is not sent.
            h = static_cast<uint64_t>(0xe0) << 32;
            if (m_config.ccBandStart && m_config.ccBandEnd)
            {
                h |= static_cast<uint64_t>(encodeBandLimit(m_config.ccBandStart)) << 24;
                h |= static_cast<uint64_t>(encodeBandLimit(m_config.ccBandEnd)) << 16;
            }
            if (type != MessageType::Frame1b && m_config.tcBandStart && m_config.tcBandEnd)
            {
                h |= static_cast<uint64_t>(encodeBandLimit(m_config.tcBandStart)) << 8;
                h |= static_cast<uint64_t>(encodeBandLimit(m_config.tcBandEnd));
            }
        }
        else
        {
            // NMT Doc 450-1 para 4.3.3.12 / 900-1 para 4.3.3.14: H3 period, H7 group of phones
            // that listen in this frame period. Period 0: the phones never sleep.
            h = static_cast<uint64_t>(0xeb) << 32;
            h |= static_cast<uint64_t>(m_config.batteryPeriod & 0xf) << 28;
            h |= static_cast<uint64_t>(m_config.batteryGroup & 0xf) << 12;
            h |= kOverdecadic;

            // clock in H8H9H10: flag, 5 bits hour, 6 bits minute; not on a combined channel
            if (m_config.sendClock && type != MessageType::Frame1b && m_timeOfDay)
            {
                const std::pair<int, int> now = m_timeOfDay();
                h |= static_cast<uint64_t>((1 << 11) | ((now.first & 0x1f) << 6) | (now.second & 0x3f));
            }
        }
    }

    // area number, in H8H9H10 because information elements are aligned to the right
    if (type == MessageType::Frame1b || type == MessageType::Frame4
     || type == MessageType::Frame4b || type == MessageType::Frame30) {
        h |= encodeAreaNo(m_config.system, static_cast<uint8_t>(m_config.areaNo));
    }

    return h;
}

Frame NmtChannel::idleFrame()
{
    Frame frame;

    frame.mt = idleMessageType(m_config.type);
    frame.channelNo = static_cast<uint16_t>(encodeChannel(m_config.system, m_config.channel, m_config.msPower));
    frame.trafficArea = static_cast<uint8_t>(encodeTrafficArea(m_config.system, m_config.channel, m_config.trafficArea));
    frame.additionalInfo = additionalInfo(frame.mt);
    m_framesSent++;

    return frame;
}

bool NmtChannel::nextFrame(FrameBits& bits)
{
    if (!m_valid) {
        return false;
    }

    bits = encodeFrame(idleFrame());
    return true;
}

void NmtChannel::receiveFrame(const RxFrame& rx)
{
    if (!m_valid || !m_listener) {
        return;
    }

    ReceivedFrame received;
    received.frame = decodeFrame(rx.bits, m_config.loopback ? Direction::MtxToXx : Direction::XxToMtx, false);
    received.quality = rx.quality;
    received.level = rx.level;
    received.framesElapsed = rx.framesElapsed;
    received.leadingZeros = rx.leadingZeros;
    m_listener(received);
}

} // namespace modemnmt
