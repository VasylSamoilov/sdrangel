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

#include <algorithm>
#include <cmath>

namespace modemnmt
{

namespace
{

bool channelInRange(System system, int channel)
{
    if (system == System::Nmt450) {
        return channel >= 1 && channel <= 400;
    }

    // 1..1000 and the interleaved channels 1025..2024; 1008..1014 are fictitious channels
    // (queueing information and area number), 1001..1007 are not used
    return (channel >= 1 && channel <= 1000) || (channel >= 1025 && channel <= 2024);
}

// A channel band is both limits or none; the limits are ordinary channels (NMT Doc 900-1
// para 4.3.3.13.5).
bool bandInRange(int start, int end)
{
    if (start == 0 && end == 0) {
        return true;
    }

    return start >= 1 && end <= 999 && start < end;
}

bool isTrafficType(ChannelType type)
{
    return type == ChannelType::Tc || type == ChannelType::AcTc || type == ChannelType::CcTc;
}

bool configValid(const ChannelConfig& c, double sampleRate, double deviationFactor)
{
    const bool is900Type = c.type == ChannelType::Cca || c.type == ChannelType::Ccb || c.type == ChannelType::AcTc;

    return (c.system == System::Nmt900 || !is900Type)
        && channelInRange(c.system, c.channel)
        && c.msPower >= 0 && c.msPower <= 3
        && c.areaNo >= 0 && c.areaNo <= 4
        && c.batteryPeriod >= 0 && c.batteryPeriod <= 9
        && c.batteryGroup >= 0 && c.batteryGroup <= 8
        // the tone is for traffic channels; the tones are 4 kHz, which needs a higher rate than 8 kHz
        && c.supervisory >= 0 && c.supervisory <= 4
        && (c.supervisory == 0 || (isTrafficType(c.type) && sampleRate >= 12000.0))
        && bandInRange(c.ccBandStart, c.ccBandEnd)
        && bandInRange(c.tcBandStart, c.tcBandEnd)
        && sampleRate >= 8000.0 && sampleRate <= 10e6
        && deviationFactor > 0.0 && deviationFactor <= 100.0;
}

// Subscriber digits are 1..9 and 0. Anything else is a damaged frame: the code that protects
// the frames corrects bursts but cannot tell that it failed.
bool plainDigits(const std::string& digits)
{
    for (char c : digits)
    {
        if (c < '0' || c > '9') {
            return false;
        }
    }

    return true;
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

// How long the phone has to answer our release with the release guard.
const double kReleaseTimeout = 2.0;
// Longest time a transaction may stay in a state that advances with the frames we send. The
// frame counts are 8 + 4 frames (about 1.7 s); this only ends transactions of a channel that
// stopped sending.
const double kStateWatchdog = 5.0;
// Switching a call in progress (scheme C2'): the order 3c is sent 4 times and followed by 4 idle
// frames (T' = 553 ms). If the phone has not identified itself on the new channel T' after the
// start of the order, it is asked for its identity (4 frames), and if it does not answer
// within T' after that the switch is given up. 300 ms are added for the delay of the radio.
const int kScipOrderFrames = 4;
const int kScipIdleFrames = 4;
const int kScipRequestFrames = 4;
const double kScipIdentTime = 0.553;
const double kScipRequestTime = 0.553 + 0.3;
// In-call procedures. Frame counts (NMT Doc 450-1 4.4.1.8-4.4.1.10): 3 idle frames, then the
// acknowledgement 4 times (T'); to leave: the acknowledgement 4 times, then 4 idle frames; the
// power order is repeated for at most T = 8 frames. The standard gives no time for the phone to
// be silent in the MFT state or to dial after a register recall; these are our limits.
const int kAckIdleFrames = 3;
const int kAckFrames = 4;
const int kLeaveIdleFrames = 4;
const int kPowerOrderFrames = 8;
// Frames to wait for the idle frame of a phone before the order to answer by itself goes out.
const int kAutoanswerWait = 6;
// Idle frames of the phone (scheme B2: 15, 15, 15/16, 15/16) after which the order goes out.
const int kPhoneIdleFrames = 4;
const double kMftSilence = 10.0;
const double kRecallFirstDigit = 30.0;
const double kRecallNextDigit = 8.0;
const size_t kMaxRecallDigits = 32;
// Frames of L(3), the roaming updating confirmation (NMT Doc 450-1 scheme D, T''' = 2 frames).
const int kConfirmFrames = 2;
// The phone dials digit by digit; a pause longer than this ends the call.
const double kDiallingTimeout = 1.0;
// Dial tone frequency
const double kDialToneHz = 425.0;
// How long a call waits for a free traffic channel, and how long the phone may ring.
const double kQueueTimeout = 60.0;
const double kRingingTimeout = 60.0;
// A message call that rings a phone nobody answers, and one that waits for a traffic channel.
const double kMessageRingTimeout = 15.0;
const double kMessageQueueTimeout = 10.0;
// The received audio is muted this long after a frame sync (a little more than two frames).
const double kMuteSeconds = 0.280;
// Seconds to detect the supervisory tone after the call is connected, and to lose it before the
// call is cleared.
const double kSupervisoryDetect = 3.0;
const double kSupervisoryLost = 20.0;
// Most digits taken from the phone
const size_t kMaxDialled = 32;
// The third frame of L(3) is replaced by the dial tone (two are sent), frames of L(6) address
// complete, and the frame in which the call becomes active.
const int kProceedFrames = 3;
const int kAddressCompleteFrames = 4;
const int kActiveFrame = 9;

// Bit 17 of the additional information: H6 bit 3, the mobile station may send overdecadic
// B-numbers (NMT Doc 450-1 para 4.3.3.12.5)
const uint64_t kOverdecadic = 0x20000;

} // namespace

NmtChannel::NmtChannel(const ChannelConfig& config, double sampleRate, double deviationFactor) :
    m_config(config),
    m_valid(configValid(config, sampleRate, deviationFactor)),
    m_modem(sampleRate, deviationFactor),
    m_framesSent(0),
    m_host(nullptr),
    m_state(State::Idle),
    m_transaction(nullptr),
    m_rxFrameCount(0),
    m_txFrameCount(0),
    m_sampleRate(sampleRate),
    m_deviationFactor(deviationFactor),
    m_dspMode(DspMode::Frame),
    m_tonePhase(0.0),
    // an unusable configuration never reaches the audio path, but the objects are built anyway
    m_superDetector(m_valid ? sampleRate : 48000.0, config.supervisory),
    m_superTone(m_valid ? sampleRate : 48000.0, config.supervisory, deviationFactor),
    m_rxAudio(m_valid ? sampleRate : 48000.0, config.supervisory),
    m_txAudio(m_valid ? sampleRate : 48000.0),
    m_rxMute(0),
    m_alarmRunning(false),
    m_callerIdCount(0),
    m_scipOrdered(false),
    m_digitCount(0),
    m_newPower(0),
    m_haveLastDigit(false),
    m_lastDigitOdd(false),
    m_mftIdleSeen(false),
    m_autoanswerGo(false),
    m_autoanswerAcked(false),
    m_phoneIdle(0),
    m_mwiLead(0),
    m_mwiAcked(false)
{
    m_modem.setFrameSource([this](FrameBits& bits) { return nextFrame(bits); });
    m_modem.setFrameSink([this](const RxFrame& rx) { receiveFrame(rx); });
    // the audio is muted while a frame is received, a little longer than two frames
    m_modem.setSyncHandler([this]() {
        if (!m_config.loopback) {
            m_rxMute = static_cast<int>(m_sampleRate * kMuteSeconds);
        }
    });
    m_superDetector.setSink([this](bool detected, double) { onSupervisory(detected); });
    m_rxAudio.setSink([this](const float* samples, int count) {
        // the call may have been released by an earlier block of this buffer
        if (callAudioUp()) {
            m_host->audioFromMobile(*m_transaction, samples, count);
        }
    });
    m_txAudio.setPull([this](float* samples, int count) {
        if (callAudioUp()) {
            m_host->audioToMobile(*m_transaction, samples, count);
        }
    });
}

// The speech path is connected to the call.
bool NmtChannel::callAudioUp() const
{
    return m_host && m_transaction && m_transaction->callUp && m_state == State::Active;
}

void NmtChannel::receive(const float* samples, int length)
{
    if (!m_valid || length <= 0) {
        return;
    }

    // Speech and the supervisory tone matter during a call only. The tone is measured on the
    // audio as it comes.
    const bool call = (m_state == State::Active);

    if (call) {
        m_superDetector.process(samples, length);
    }
    m_modem.receive(samples, length);

    // The frame sync is found while the block is processed; the audio of the frame is muted
    // from the start of the block.
    const int muted = std::min(m_rxMute, length);

    m_rxMute -= muted;
    if (call && callAudioUp()) {
        m_rxAudio.process(samples, length, muted);
    }
}

// The phone's supervisory tone appeared or went away. During a call: a tone stops the alarm,
// no tone starts it.
void NmtChannel::onSupervisory(bool detected)
{
    if (m_config.loopback || m_state != State::Active || !m_superDetector.enabled()) {
        return;
    }

    m_alarmRunning = !detected;
    if (detected) {
        m_timer->stop();
    } else {
        m_timer->start(kSupervisoryLost);
    }
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

NmtChannel::~NmtChannel()
{
    // the transaction must not keep a pointer to a channel that is gone
    if (m_host && m_transaction)
    {
        // a power change or recall that is going on is over (the handlers are alive until the
        // system is gone)
        abortInCall(m_state);
        m_host->detachChannel(*this, m_transaction);
    }
}

void NmtChannel::attach(NmtHost& host)
{
    if (m_host) {
        return;
    }

    m_host = &host;
    m_timer = std::make_unique<modemcellular::Timer>(host.timers(), [this]() { onTimer(); });
}

bool NmtChannel::nextFrame(FrameBits& bits)
{
    if (!m_valid) {
        return false;
    }

    Frame frame;

    switch (m_state)
    {
    case State::RoamingIdent:
    case State::MoIdent:
    case State::MtIdent:
        frame = txIdentRequest();
        break;
    case State::MtPaging:
        frame = txMtPaging();
        break;
    case State::MtQueue:
        frame = txMtQueue();
        break;
    case State::MtChannel:
        frame = txMtChannel();
        break;
    case State::MtRinging:
        frame = txMtRinging();
        break;
    case State::MtComplete:
        frame = txMtComplete();
        break;
    case State::RoamingConfirm:
        frame = txRoamingConfirm();
        break;
    case State::MoConfirm:
        frame = txMoConfirm();
        break;
    case State::MoComplete:
        frame = txMoComplete();
        break;
    case State::MoRelease:
        frame = txMoRelease();
        break;
    case State::MtRelease:
        frame = txMtRelease();
        break;
    case State::MtAutoanswer:
        frame = txMtAutoanswer();
        break;
    case State::MwiClear:
        frame = txMwiClear();
        break;
    case State::MftIn:
        frame = txMftIn();
        break;
    case State::MftOut:
        frame = txMftOut();
        break;
    case State::PowerChange:
        frame = txPowerChange();
        break;
    case State::RecallProceed:
        frame = txRecallProceed();
        break;
    case State::RecallEnd:
        frame = txRecallEnd();
        break;
    case State::Mft:
    case State::RecallDigits:
        // nothing but audio in these states
        return false;
    case State::ScipOrder:
        frame = txScipOrder();
        break;
    case State::ScipIdent:
        // idle frame of a channel in use; not the free-channel marker, nobody may seize it
        frame.mt = MessageType::Frame6;
        break;
    case State::ScipIdentReq:
        frame = txScipIdentReq();
        break;
    case State::ScipRelease:
        frame = txScipRelease();
        break;
    case State::MoDialling:
    case State::Active:
        // nothing but audio in these states
        return false;
    case State::Idle:
    default:
        frame = idleFrame();
        break;
    }

    // The state asked for audio: the frame that was just made is not sent.
    if (m_dspMode != DspMode::Frame) {
        return false;
    }

    bits = encodeFrame(frame);
    return true;
}

int NmtChannel::render(float* samples, int length, bool add)
{
    int framed = 0;
    int pos = 0;

    while (pos < length)
    {
        switch (m_dspMode)
        {
        case DspMode::Frame:
        {
            // while the call is switched or a procedure of the call runs, the supervisory tone stays on
            const bool tone = (m_state == State::ScipOrder || m_state == State::ScipIdent || m_state == State::ScipIdentReq
                            || m_state == State::MftIn || m_state == State::MftOut || m_state == State::PowerChange
                            || m_state == State::RecallProceed || m_state == State::RecallEnd);
            const int start = pos;
            const int count = m_valid ? m_modem.renderFrames(samples + pos, length - pos, add) : 0;
            framed += count;
            pos += count;
            // no frame to send and the mode is still the same: silence
            if (m_dspMode == DspMode::Frame && pos < length)
            {
                if (!add) {
                    std::fill(samples + pos, samples + length, 0.0f);
                }
                pos = length;
            }
            if (tone) {
                m_superTone.add(samples + start, pos - start);
            }
            break;
        }
        case DspMode::DialTone:
        {
            const int start = pos;

            pos += renderTone(samples + pos, length - pos, add);
            // the dial tone of a register recall is sent during the call, with the supervisory tone
            if (m_state == State::RecallDigits) {
                m_superTone.add(samples + start, pos - start);
            }
            break;
        }
        case DspMode::Audio:
        default:
            if (callAudioUp()) {
                m_txAudio.generate(samples + pos, length - pos, add, m_deviationFactor);
            } else if (!add) {
                std::fill(samples + pos, samples + length, 0.0f);
            }
            // the supervisory tone is on air whenever the channel carries speech
            m_superTone.add(samples + pos, length - pos);
            pos = length;
            break;
        }
    }

    return framed;
}

// Dial tone: 425 Hz at the level of speech.
int NmtChannel::renderTone(float* samples, int length, bool add)
{
    const double pi = 3.14159265358979323846;
    const double step = kDialToneHz / m_sampleRate;

    for (int i = 0; i < length; i++)
    {
        const float value = static_cast<float>(std::sin(2.0 * pi * m_tonePhase) * m_deviationFactor);
        if (add) {
            samples[i] += value;
        } else {
            samples[i] = value;
        }
        m_tonePhase += step;
        m_tonePhase -= std::floor(m_tonePhase);
    }

    return length;
}

// A change to the frame mode starts the frame stream from the beginning.
void NmtChannel::setDspMode(DspMode mode)
{
    if (mode == DspMode::Frame && m_dspMode != DspMode::Frame) {
        m_modem.resetTransmitter();
    }
    if (mode == DspMode::DialTone && m_dspMode != DspMode::DialTone) {
        m_tonePhase = 0.0;
    }
    m_dspMode = mode;
}

bool NmtChannel::networkRelease()
{
    // only a call that exists in the exchange can end there
    if (!m_transaction || !m_transaction->callUp) {
        return false;
    }

    m_transaction->callUp = false;
    release();
    return true;
}

void NmtChannel::receiveFrame(const RxFrame& rx)
{
    if (!m_valid || (!m_listener && !m_host)) {
        return;
    }

    ReceivedFrame received;
    // a call acknowledgement is only told from a seizure by the state of the channel
    received.frame = decodeFrame(rx.bits, m_config.loopback ? Direction::MtxToXx : Direction::XxToMtx,
                                 m_state == State::MtPaging);
    received.quality = rx.quality;
    received.level = rx.level;
    received.framesElapsed = rx.framesElapsed;
    received.leadingZeros = rx.leadingZeros;

    if (m_listener) {
        m_listener(received);
    }

    // in loopback the frames are our own
    if (!m_host || m_config.loopback) {
        return;
    }

    m_rxFrameCount += rx.framesElapsed;
    process(received.frame);
}

// ---- frames to the phone ----

// The power level the phone has on this channel: the one ordered for its call, else the channel's.
int NmtChannel::power() const
{
    return (m_transaction && m_transaction->power >= 0) ? m_transaction->power : m_config.msPower;
}

Frame NmtChannel::baseFrame(MessageType type) const
{
    Frame frame;

    frame.mt = type;
    frame.channelNo = static_cast<uint16_t>(encodeChannel(m_config.system, m_config.channel, power()));
    frame.trafficArea = static_cast<uint8_t>(encodeTrafficArea(m_config.system, m_config.channel, m_config.trafficArea));

    return frame;
}

Frame NmtChannel::lineSignalFrame(int signal) const
{
    Frame frame = baseFrame(MessageType::Frame5a);

    frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, m_transaction->subscriber.country)));
    frame.msNumber = static_cast<uint32_t>(digitsToValue(m_transaction->subscriber.number));
    // the signal is repeated three times, the phone takes the majority
    frame.lineSignal = static_cast<uint32_t>((signal << 8) | (signal << 4) | signal);

    return frame;
}

// Identity request on the traffic channel.
Frame NmtChannel::txIdentRequest()
{
    Frame frame = baseFrame(MessageType::Frame3b);

    frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, m_transaction->subscriber.country)));
    frame.msNumber = static_cast<uint32_t>(digitsToValue(m_transaction->subscriber.number));
    frame.additionalInfo = encodeAreaNo(m_config.system, static_cast<uint8_t>(m_config.areaNo));

    // no reply after 8 frames
    if (++m_txFrameCount == 8)
    {
        if (m_state == State::MtIdent)
        {
            // the phone does not show up on the traffic channel: the call fails, no release
            failCall(modemcellular::Cause::TempFail);
            m_host->destroyTransaction(m_transaction);
        }
        else {
            release();
        }
    }

    return frame;
}

// The call to the phone ends without being answered: the exchange is told why.
void NmtChannel::failCall(modemcellular::Cause cause)
{
    NmtTransaction* const transaction = m_transaction;

    if (transaction && transaction->callUp)
    {
        transaction->callUp = false;
        m_host->callFailed(*transaction, cause);
    }
}

bool NmtChannel::inCallState(State state)
{
    switch (state)
    {
    case State::Active:
    case State::MftIn:
    case State::Mft:
    case State::MftOut:
    case State::PowerChange:
    case State::RecallProceed:
    case State::RecallDigits:
    case State::RecallEnd:
    case State::ScipOrder:
        return true;
    default:
        return false;
    }
}

bool NmtChannel::isCallingChannel() const
{
    return m_config.type == ChannelType::Cc || m_config.type == ChannelType::Cca
        || m_config.type == ChannelType::Ccb || m_config.type == ChannelType::CcTc;
}

void NmtChannel::startPaging(NmtTransaction& transaction)
{
    if (!m_host) {
        return;
    }

    m_transaction = &transaction;
    m_state = State::MtPaging;
    m_txFrameCount = 0;
    setDspMode(DspMode::Frame);
    m_timer->stop();
}

void NmtChannel::beginMtIdent(NmtTransaction& transaction)
{
    m_transaction = &transaction;
    m_state = State::MtIdent;
    m_txFrameCount = 0;
    m_rxFrameCount = 0;
    m_dialled.clear();
    setDspMode(DspMode::Frame);
    m_timer->start(kStateWatchdog);
}

// Page: frame 2a once, then the idle frames of the calling channel.
Frame NmtChannel::txMtPaging()
{
    if (++m_txFrameCount > 1) {
        return idleFrame();
    }

    Frame frame = baseFrame(MessageType::Frame2a);

    frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, m_transaction->subscriber.country)));
    frame.msNumber = static_cast<uint32_t>(digitsToValue(m_transaction->subscriber.number));
    frame.additionalInfo = encodeAreaNo(m_config.system, static_cast<uint8_t>(m_config.areaNo));
    return frame;
}

// The phone waits on the calling channel for a traffic channel: queueing information, 2c for a
// priority phone, 2f for an ordinary one.
Frame NmtChannel::txMtQueue()
{
    // a traffic channel became free
    if (m_host->findTrafficChannel(*this))
    {
        m_timer->stop();
        m_state = State::MtChannel;
        m_txFrameCount = 0;
        return txMtChannel();
    }

    // The queueing frame goes between the idle frames of the channel (scheme B: 2f "may also be
    // sent"), so the other phones keep their calling channel.
    if (m_txFrameCount++ & 1) {
        return idleFrame();
    }

    Frame frame = baseFrame(m_transaction->priority ? MessageType::Frame2c : MessageType::Frame2f);

    frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, m_transaction->subscriber.country)));
    frame.msNumber = static_cast<uint32_t>(digitsToValue(m_transaction->subscriber.number));
    // the fictitious channels 1008 (ordinary), 1009 (priority); NMT-450 codes them as 3F0, 3F1
    const uint16_t code = m_transaction->priority ? 1 : 0;
    frame.additionalInfo = (m_config.system == System::Nmt450 ? 0x3f0 : 0x9f0) + code;
    return frame;
}

// Traffic channel allocation: frame 2b with the channel on the calling channel, then the call
// continues on the traffic channel (or here, on a combined channel).
Frame NmtChannel::txMtChannel()
{
    NmtChannel* const tc = m_host->findTrafficChannel(*this);

    if (!tc)
    {
        // no free traffic channel: the call is queued
        m_state = State::MtQueue;
        m_txFrameCount = 0;
        // a message call does not hold the calling channel for long
        m_timer->start(m_transaction->messageCall ? kMessageQueueTimeout : kQueueTimeout);
        return txMtQueue();
    }

    NmtTransaction* const transaction = m_transaction;
    Frame frame = baseFrame(MessageType::Frame2b);

    frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, transaction->subscriber.country)));
    frame.msNumber = static_cast<uint32_t>(digitsToValue(transaction->subscriber.number));
    frame.tcNo = static_cast<uint16_t>(encodeTc(tc->m_config.system, tc->m_config.channel, tc->m_config.msPower));

    if (tc != this) {
        m_host->moveToTrafficChannel(*transaction, *this, *tc);       // this channel is idle after it
    } else {
        beginMtIdent(*transaction);
    }

    return frame;
}

// Ringing order L(9), then the number of the caller; repeated every 5 s.
Frame NmtChannel::txMtRinging()
{
    Frame frame = lineSignalFrame(9);

    // The ringing order is sent for 4 frames (scheme B, T'), then idle frames or the number of
    // the caller.
    ++m_txFrameCount;
    if (m_txFrameCount > 4)
    {
        if (m_callerIdCount) {
            frame = Frame();
            encodeANumber(frame, m_txFrameCount - 5, m_transaction->callerType, m_transaction->callerId,
                          m_config.system, m_config.channel, m_config.msPower, m_config.trafficArea);
        } else {
            frame = Frame();
            frame.mt = MessageType::Frame6;
        }
    }

    if (m_callerIdCount == 1)
    {
        // ringing starts after the first number of 6 frames
        if (m_txFrameCount == 10)
        {
            m_txFrameCount = 0;
            m_callerIdCount++;
        }
    }
    else if (m_txFrameCount == 36) {
        m_txFrameCount = 0;
    }

    return frame;
}

// The phone answered: idle frames for 4 frames (the compandor order would be here), then audio.
Frame NmtChannel::txMtComplete()
{
    Frame frame;

    frame.mt = MessageType::Frame6;
    if (++m_txFrameCount == 4)
    {
        // a phone that answered a ringing message call hears nothing: the indicators and the release
        if (m_transaction->messageCall) {
            beginMwiClear(0, false);
        } else {
            enterActive();
        }
    }

    return frame;
}

// L(3): proceed to send unencrypted digits, which is also the roaming updating confirmation
// (NMT Doc 450-1 para 4.3.3.3, scheme D para 4.4.1.5: for T''' = 2 frames, then the release).
Frame NmtChannel::txRoamingConfirm()
{
    Frame frame = lineSignalFrame(3);

    if (++m_txFrameCount == kConfirmFrames) {
        release();
    }

    return frame;
}

// L(3): proceed to send. After the second frame the dial tone replaces the frames.
Frame NmtChannel::txMoConfirm()
{
    Frame frame = lineSignalFrame(3);

    if (++m_txFrameCount == kProceedFrames)
    {
        m_state = State::MoDialling;
        setDspMode(DspMode::DialTone);
        m_timer->start(kDiallingTimeout);
    }

    return frame;
}

// L(6): address complete, four times. Then idle frames, and the call is connected.
Frame NmtChannel::txMoComplete()
{
    Frame frame;

    if (++m_txFrameCount <= kAddressCompleteFrames)
    {
        frame = lineSignalFrame(6);
    }
    else
    {
        frame.mt = MessageType::Frame6;
        if (m_txFrameCount == kActiveFrame) {
            enterActive();
        }
    }

    return frame;
}

// The call is connected: speech, and the phone has to send its supervisory tone within a few
// seconds.
void NmtChannel::enterActive()
{
    m_state = State::Active;
    m_mftIdleSeen = false;
    setDspMode(DspMode::Audio);
    m_txAudio.reset();
    m_rxAudio.reset();
    m_alarmRunning = false;
    if (m_superDetector.enabled())
    {
        m_superDetector.reset();
        m_timer->start(kSupervisoryDetect);
    }
    else {
        m_timer->stop();
    }
}

// L(15): clearing, towards the exchange the release is acknowledged 4 times.
Frame NmtChannel::txMoRelease()
{
    Frame frame = lineSignalFrame(15);

    if (++m_txFrameCount == 4) {
        m_host->destroyTransaction(m_transaction);
    }

    return frame;
}

// L(15): clearing, call transfer not activated.
Frame NmtChannel::txMtRelease()
{
    return lineSignalFrame(15);
}

// ---- procedures during the conversation (NMT Doc 450-1 para 4.4.1.8 to 4.4.1.10) ----

// The rule of NMT Doc 450-1 para 4.7.2.1 for digit frames: a frame is a new digit when its position
// marker (14a: S(0), 14b: S(15)) differs from the one of the digit taken before, however many
// frames the digit is repeated; the digit is sent three times and has to agree.
NmtChannel::DigitResult NmtChannel::takeLooseDigit(const Frame& frame, char& digit)
{
    const bool odd = (frame.mt == MessageType::Frame14a);

    if (!odd && frame.mt != MessageType::Frame14b) {
        return DigitResult::Foreign;
    }
    if (!matchChannel(frame) || !matchArea(frame) || !matchSubscriber(frame)) {
        return DigitResult::Foreign;
    }
    if ((frame.digit >> 12) != (odd ? 0x00u : 0xffu)) {
        return DigitResult::Damaged;
    }
    if (((frame.digit >> 8) & 0xf) != ((frame.digit >> 4) & 0xf) || ((frame.digit >> 4) & 0xf) != (frame.digit & 0xf)) {
        return DigitResult::Damaged;
    }
    if (m_haveLastDigit && m_lastDigitOdd == odd) {
        return DigitResult::Repeat;
    }

    digit = valueToDigit(frame.digit);
    m_haveLastDigit = true;
    m_lastDigitOdd = odd;
    return DigitResult::Digit;
}

// The phone asks for the MFT converter (L(8)) or for a register recall (L(5)); a call that is
// being switched to another channel is left alone.
void NmtChannel::rxActive(const Frame& frame)
{
    // A phone in the converter state sends idle frames while it waits for our answer to L(7).
    if (frame.mt == MessageType::Frame15) {
        m_mftIdleSeen = true;
    }
    if (frame.mt != MessageType::Frame13a || !matchChannel(frame) || !matchSubscriber(frame) || m_transaction->scipTarget) {
        return;
    }

    const int signal = lineSignalMajority(frame.lineSignal, 5, 3);

    // L(7) again: the phone did not hear our answer and leaves the converter state once more.
    // The copies of an answer to a forced release that come after we left are not that; the
    // phone that waits sends idle frames in between.
    if (signal == 7)
    {
        if (m_mftIdleSeen) {
            beginMftOut();
        }
        return;
    }
    if (signal != 8 && signal != 5) {
        return;
    }

    // the speech stops; the supervisory alarm is not evaluated in these states
    m_state = (signal == 8) ? State::MftIn : State::RecallProceed;
    m_txFrameCount = 0;
    m_rxFrameCount = 0;
    m_digitCount = 0;
    m_haveLastDigit = false;
    m_recallDigits.clear();
    m_alarmRunning = false;
    setDspMode(DspMode::Frame);
    m_timer->start(kStateWatchdog);
}

// 3 idle frames, then L(4) four times; the state of the converter starts, the speech path
// stays silent (the supervisory tone goes on).
Frame NmtChannel::txMftIn()
{
    Frame frame;
    const int n = ++m_txFrameCount;

    if (n <= kAckIdleFrames) {
        frame.mt = MessageType::Frame6;
    } else if (n <= kAckIdleFrames + kAckFrames) {
        frame = lineSignalFrame(4);
    }
    else
    {
        frame.mt = MessageType::Frame6;
        m_state = State::Mft;
        setDspMode(DspMode::Audio);
        m_timer->start(kMftSilence);
    }

    return frame;
}

// Digits for the converter; L(7) ends the state.
void NmtChannel::rxMft(const Frame& frame)
{
    // the phone sends idle frames while it is in the converter state: it is there
    if (frame.mt == MessageType::Frame15)
    {
        m_timer->start(kMftSilence);
        return;
    }

    if (frame.mt == MessageType::Frame13a)
    {
        if (matchChannel(frame) && matchSubscriber(frame) && lineSignalMajority(frame.lineSignal, 5, 3) == 7) {
            beginMftOut();
        }
        return;
    }

    char digit = 0;
    const DigitResult result = takeLooseDigit(frame, digit);

    if (result == DigitResult::Foreign) {
        return;
    }

    m_timer->start(kMftSilence);
    if (result == DigitResult::Digit)
    {
        m_digitCount++;
        m_host->mftDigit(*m_transaction, digit);
    }
}

// The phone left the state (L(7)), or the base station ends it: L(10) four times answers
// both, then 4 idle frames and the conversation.
void NmtChannel::beginMftOut()
{
    m_state = State::MftOut;
    m_txFrameCount = 0;
    setDspMode(DspMode::Frame);
    m_timer->start(kStateWatchdog);
}

Frame NmtChannel::txMftOut()
{
    Frame frame;
    const int n = ++m_txFrameCount;

    if (n <= kAckFrames) {
        frame = lineSignalFrame(10);
    } else if (n <= kAckFrames + kLeaveIdleFrames) {
        frame.mt = MessageType::Frame6;
    }
    else
    {
        frame.mt = MessageType::Frame6;
        enterActive();
    }

    return frame;
}

// The phone is ordered to another power level: frame 3a for at most T. NMT-450 has the new level
// in Na and the old one in N1, NMT-900 has it in N1 (Na has no power bits there).
bool NmtChannel::beginPowerChange(int level)
{
    if (!m_host || !m_transaction || m_state != State::Active || !m_transaction->callUp || m_transaction->scipTarget
     || level < 0 || level > 3 || level == power()) {
        return false;
    }

    m_newPower = level;
    m_state = State::PowerChange;
    m_txFrameCount = 0;
    m_alarmRunning = false;
    setDspMode(DspMode::Frame);
    m_timer->start(kStateWatchdog);
    return true;
}

Frame NmtChannel::txPowerChange()
{
    Frame frame;

    if (++m_txFrameCount > kPowerOrderFrames)
    {
        // no answer within T: the call goes on at the old level, the standard has nothing else
        NmtTransaction* const transaction = m_transaction;

        frame.mt = MessageType::Frame6;
        enterActive();
        m_host->powerChanged(*transaction, m_newPower, false);
        return frame;
    }

    frame = baseFrame(MessageType::Frame3a);
    frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, m_transaction->subscriber.country)));
    frame.msNumber = static_cast<uint32_t>(digitsToValue(m_transaction->subscriber.number));
    if (m_config.system == System::Nmt450)
    {
        frame.tcNo = static_cast<uint16_t>(encodeTc(m_config.system, m_config.channel, m_newPower));
    }
    else
    {
        frame.channelNo = static_cast<uint16_t>(encodeChannel(m_config.system, m_config.channel, m_newPower));
        frame.tcNo = static_cast<uint16_t>(encodeTc(m_config.system, m_config.channel, 0));
    }

    return frame;
}

// The answer carries the new level in N1.
void NmtChannel::rxPowerChange(const Frame& frame)
{
    if (frame.mt != MessageType::Frame10b || !matchChannel(frame) || !matchSubscriber(frame)) {
        return;
    }

    int channel, level;

    if (!decodeChannel(m_config.system, frame.channelNo, channel, level) || level != m_newPower) {
        return;
    }

    NmtTransaction* const transaction = m_transaction;

    transaction->power = m_newPower;
    enterActive();
    m_host->powerChanged(*transaction, m_newPower, true);
}

// Register recall: 3 idle frames, L(3) four times, then the dial tone until the first digit.
Frame NmtChannel::txRecallProceed()
{
    Frame frame;
    const int n = ++m_txFrameCount;

    if (n <= kAckIdleFrames) {
        frame.mt = MessageType::Frame6;
    } else if (n <= kAckIdleFrames + kAckFrames) {
        frame = lineSignalFrame(3);
    }
    else
    {
        frame.mt = MessageType::Frame6;
        m_state = State::RecallDigits;
        setDspMode(DspMode::DialTone);
        m_timer->start(kRecallFirstDigit);
    }

    return frame;
}

void NmtChannel::rxRecall(const Frame& frame)
{
    // The R button again (after a wrong digit): the digits so far are over, a new recall starts.
    if (frame.mt == MessageType::Frame13a)
    {
        if (matchChannel(frame) && matchSubscriber(frame) && lineSignalMajority(frame.lineSignal, 5, 3) == 5)
        {
            NmtTransaction* const transaction = m_transaction;
            const std::string digits = m_recallDigits;

            m_state = State::RecallProceed;
            m_txFrameCount = 0;
            m_digitCount = 0;
            m_haveLastDigit = false;
            m_recallDigits.clear();
            setDspMode(DspMode::Frame);
            m_timer->start(kStateWatchdog);
            m_host->recallEnded(*transaction, digits);
        }
        return;
    }

    char digit = 0;
    const DigitResult result = takeLooseDigit(frame, digit);

    switch (result)
    {
    case DigitResult::Foreign:
    case DigitResult::Repeat:
    case DigitResult::Damaged:
    case DigitResult::Lost:
        return;
    case DigitResult::TooLong:
        beginRecallEnd();
        return;
    case DigitResult::Digit:
        break;
    }

    // the dial tone ends with the first digit
    if (m_recallDigits.empty()) {
        setDspMode(DspMode::Audio);
    }
    m_digitCount++;
    m_recallDigits.push_back(digit);
    m_timer->start(kRecallNextDigit);

    const bool enough = m_host->recallDigit(*m_transaction, digit);

    if (m_state == State::RecallDigits && (enough || m_recallDigits.size() >= kMaxRecallDigits)) {
        beginRecallEnd();
    }
}

// Back to the conversation: L(6) four times and 4 idle frames. The exchange gets the digits.
void NmtChannel::beginRecallEnd()
{
    NmtTransaction* const transaction = m_transaction;
    const std::string digits = m_recallDigits;

    m_state = State::RecallEnd;
    m_txFrameCount = 0;
    setDspMode(DspMode::Frame);
    m_timer->start(kStateWatchdog);
    m_host->recallEnded(*transaction, digits);
}

Frame NmtChannel::txRecallEnd()
{
    Frame frame;
    const int n = ++m_txFrameCount;

    if (n <= kAckFrames) {
        frame = lineSignalFrame(6);
    } else if (n <= kAckFrames + kLeaveIdleFrames) {
        frame.mt = MessageType::Frame6;
    }
    else
    {
        frame.mt = MessageType::Frame6;
        enterActive();
    }

    return frame;
}

// ---- switching a call in progress (NMT Doc 450-1 para 4.4.1.4, scheme C2') ----

bool NmtChannel::scipTargetState() const
{
    return m_state == State::ScipIdent || m_state == State::ScipIdentReq || m_state == State::ScipRelease;
}

// The new channel: idle frames and the supervisory tone before the phone is ordered here.
void NmtChannel::beginScipIdent(NmtTransaction& transaction)
{
    if (!m_host) {
        return;
    }

    m_transaction = &transaction;
    m_scipOrdered = false;
    m_state = State::ScipIdent;
    m_txFrameCount = 0;
    setDspMode(DspMode::Frame);
    // T' is counted from the first 3c on the old channel (txScipOrder); this only covers an old
    // channel that never sends it
    m_timer->start(kStateWatchdog);
}

// The old channel: the order 3c four times, then idle frames for T'. Then the call goes on
// here, until the phone shows up on the new channel.
void NmtChannel::beginScipOrder()
{
    if (!m_host) {
        return;
    }

    // the level of the phone is the one of this channel; the new channel keeps it
    if (m_transaction && m_transaction->power < 0) {
        m_transaction->power = m_config.msPower;
    }

    m_state = State::ScipOrder;
    m_txFrameCount = 0;
    m_alarmRunning = false;
    setDspMode(DspMode::Frame);
    m_timer->start(kStateWatchdog);
}

Frame NmtChannel::txScipOrder()
{
    const NmtChannel* const target = m_transaction->scipTarget;
    Frame frame;

    ++m_txFrameCount;
    if (m_txFrameCount <= kScipOrderFrames && target)
    {
        const ChannelConfig& t = target->config();

        // T' for the identity starts with the order
        if (m_txFrameCount == 1)
        {
            m_transaction->scipTarget->m_timer->start(kScipIdentTime);
            m_transaction->scipTarget->m_scipOrdered = true;
        }

        // the channel of the frame is the new one: N1'N2'N3' equals NaNbNc
        frame = baseFrame(MessageType::Frame3c);
        // the phone keeps the power level of its call
        const int level = power();

        frame.channelNo = static_cast<uint16_t>(encodeChannel(t.system, t.channel, level));
        frame.trafficArea = static_cast<uint8_t>(encodeTrafficArea(t.system, t.channel, m_config.trafficArea));
        frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, m_transaction->subscriber.country)));
        frame.msNumber = static_cast<uint32_t>(digitsToValue(m_transaction->subscriber.number));
        frame.tcNo = static_cast<uint16_t>(encodeTc(t.system, t.channel, level));
    }
    else
    {
        frame.mt = MessageType::Frame6;
        if (m_txFrameCount > kScipOrderFrames + kScipIdleFrames)
        {
            // the speech goes on here; if the phone does not come to the new channel it stays
            enterActive();
        }
    }

    return frame;
}

// Identity request on the new channel, at most T' long.
Frame NmtChannel::txScipIdentReq()
{
    Frame frame;

    if (++m_txFrameCount > kScipRequestFrames)
    {
        frame.mt = MessageType::Frame6;
        return frame;
    }

    frame = baseFrame(MessageType::Frame3b);
    frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, m_transaction->subscriber.country)));
    frame.msNumber = static_cast<uint32_t>(digitsToValue(m_transaction->subscriber.number));
    frame.additionalInfo = encodeAreaNo(m_config.system, static_cast<uint8_t>(m_config.areaNo));

    return frame;
}

// The phone did not come: the release signal on the new channel, then it is free.
void NmtChannel::beginScipRelease()
{
    if (!m_host) {
        return;
    }

    m_state = State::ScipRelease;
    m_txFrameCount = 0;
    setDspMode(DspMode::Frame);
    m_timer->start(kStateWatchdog);
}

Frame NmtChannel::txScipRelease()
{
    Frame frame = lineSignalFrame(15);

    if (++m_txFrameCount == 4)
    {
        NmtTransaction* const transaction = m_transaction;

        goIdle();
        m_host->detachChannel(*this, transaction);
    }

    return frame;
}

// The phone is on the new channel and says who it is.
void NmtChannel::rxScipIdent(const Frame& frame)
{
    if (frame.mt != MessageType::Frame10b || m_transaction->scipTarget != this) {
        return;
    }
    // no identity before the order; before our request the phone has only seen the area of the
    // old channel, so the area is checked after the request only
    if (!m_scipOrdered || !matchChannel(frame) || !matchSubscriber(frame)
     || (m_state == State::ScipIdentReq && !matchArea(frame))) {
        return;
    }

    // the call is here now
    enterActive();
    m_host->scipCompleted(*m_transaction);
}

// ---- frames from the phone ----

NmtSubscriber NmtChannel::subscriberOf(const Frame& frame)
{
    NmtSubscriber subscriber;

    subscriber.country = valueToDigits(frame.msCountry, 1)[0];
    subscriber.number = valueToDigits(frame.msNumber, 6);

    return subscriber;
}

bool NmtChannel::isTrafficChannel() const
{
    return m_config.type == ChannelType::Tc
        || m_config.type == ChannelType::AcTc
        || m_config.type == ChannelType::CcTc;
}

// The channel number of the frame is ours. On an interleaved channel the upper bit is ignored.
bool NmtChannel::matchChannel(const Frame& frame) const
{
    int channel, power;

    if (!decodeChannel(m_config.system, frame.channelNo, channel, power)) {
        return false;
    }

    return (channel % 1024) == (m_config.channel % 1024);
}

// Area number and traffic area of the frame are ours. Old phones do not send them.
bool NmtChannel::matchArea(const Frame& frame) const
{
    if (frame.areaInfo + frame.trafficArea == 0) {
        return true;
    }

    int areaNo = frame.areaInfo >> 2;
    if (areaNo == 0 && m_config.areaNo != 0) {
        areaNo = 4;
    }
    if (areaNo != m_config.areaNo) {
        return false;
    }

    const int trafficArea = ((frame.areaInfo & 0x3) << 4) | frame.trafficArea;
    if (m_config.trafficArea != 0 && (m_config.trafficArea & 0x3f) != trafficArea) {
        return false;
    }

    return true;
}

bool NmtChannel::matchSubscriber(const Frame& frame) const
{
    if (!m_transaction) {
        return false;
    }

    return digitsToValue(std::string(1, m_transaction->subscriber.country)) == frame.msCountry
        && digitsToValue(m_transaction->subscriber.number) == frame.msNumber;
}

void NmtChannel::process(const Frame& frame)
{
    // A frame of the phone of this call shows that it is there: while the alarm for a lost
    // tone is running, it is started again. (The 3 s for the first tone are not extended.)
    if (m_state == State::Active && m_alarmRunning && matchSubscriber(frame)) {
        m_timer->start(kSupervisoryLost);
    }

    // The phone clears: acknowledge with the release signal, unless that is already going on
    // (a clearing from the phone that answers our release is handled in MtRelease).
    if (frame.mt == MessageType::Frame13a && m_state != State::Idle
     && m_state != State::MoRelease && m_state != State::MtRelease && m_state != State::MwiClear && !scipTargetState())
    {
        // three of five characters have to agree on L(1); other line signals (the answer, for
        // one) are for the state
        if (matchChannel(frame) && matchSubscriber(frame) && lineSignalMajority(frame.lineSignal, 5, 3) == 1)
        {
            // the state changes before the exchange is told, so what the handler does lands
            // on a channel that is already releasing
            const bool wasUp = m_transaction->callUp;
            NmtTransaction* const transaction = m_transaction;

            transaction->callUp = false;
            // the page stops: no more tries, and the other calling channels let go
            if (transaction->paging) {
                m_host->pagingAnswered(*transaction, *this);
            }
            // a switch to another channel is off, the phone is here
            if (transaction->scipTarget) {
                m_host->scipEnded(*transaction, false);
            }
            const State previous = m_state;

            setDspMode(DspMode::Frame);
            m_state = State::MoRelease;
            m_txFrameCount = 0;
            m_timer->start(kStateWatchdog);
            // message indicators for the phone go out before the release (4.4.1.16)
            if (transaction->mwiFlags && inCallState(previous) && transaction->mwiResult == NmtServiceEnd::Undelivered) {
                beginMwiClear(0, true);
            }
            abortInCall(previous);
            if (wasUp && m_transaction == transaction) {
                m_host->callCleared(*transaction);
            }
            return;
        }
    }

    switch (m_state)
    {
    case State::Idle:
        rxIdle(frame);
        break;
    case State::RoamingIdent:
    case State::MoIdent:
        rxIdent(frame);
        break;
    case State::MoDialling:
        rxMoDialling(frame);
        break;
    case State::MtPaging:
        rxMtPaging(frame);
        break;
    case State::MtIdent:
        rxMtIdent(frame);
        break;
    case State::MtRinging:
        rxMtRinging(frame);
        break;
    case State::MtRelease:
        rxMtRelease(frame);
        break;
    case State::ScipIdent:
    case State::ScipIdentReq:
        rxScipIdent(frame);
        break;
    case State::MtAutoanswer:
        rxMtAutoanswer(frame);
        break;
    case State::MwiClear:
        rxMwiClear(frame);
        break;
    case State::Active:
        rxActive(frame);
        break;
    case State::Mft:
        rxMft(frame);
        break;
    case State::PowerChange:
        rxPowerChange(frame);
        break;
    case State::RecallDigits:
        rxRecall(frame);
        break;
    case State::MftIn:
    case State::MftOut:
    case State::RecallProceed:
    case State::RecallEnd:
    case State::ScipOrder:
    case State::ScipRelease:
        break;
    case State::MtQueue:
    case State::MtChannel:
    case State::MtComplete:
    case State::MoConfirm:          // the phone dials after the dial tone, not before
    case State::RoamingConfirm:
    case State::MoComplete:
    case State::MoRelease:
    default:
        break;
    }
}

void NmtChannel::rxIdle(const Frame& frame)
{
    // roaming update (11a), call from an ordinary phone (10b) or from a coin-box phone (12)
    if (frame.mt != MessageType::Frame11a && frame.mt != MessageType::Frame10b && frame.mt != MessageType::Frame12) {
        return;
    }
    if (!matchChannel(frame) || !isTrafficChannel() || !matchArea(frame)) {
        return;
    }

    NmtSubscriber subscriber = subscriberOf(frame);
    if (!plainDigits(subscriber.number) || !plainDigits(std::string(1, subscriber.country))) {
        return;
    }
    subscriber.coinbox = (frame.mt == MessageType::Frame12);

    m_transaction = m_host->createTransaction(subscriber, *this);
    if (!m_transaction) {
        return;
    }

    m_state = (frame.mt == MessageType::Frame11a) ? State::RoamingIdent : State::MoIdent;
    m_rxFrameCount = 0;
    m_txFrameCount = 0;
    m_dialled.clear();
    m_timer->start(kStateWatchdog);
}

// The answer to the identity request: the seizure frame again, at least two frame periods after
// the seizure, now with the password.
void NmtChannel::rxIdent(const Frame& frame)
{
    const bool roaming = (m_state == State::RoamingIdent);

    if (roaming ? frame.mt != MessageType::Frame11a : (frame.mt != MessageType::Frame10b && frame.mt != MessageType::Frame12)) {
        return;
    }
    if (!matchChannel(frame) || !matchArea(frame) || !matchSubscriber(frame)) {
        return;
    }
    // The seizure is sent repeatedly. The reply to our identity request comes at least two
    // frame periods after the seizure.
    if (m_rxFrameCount < 2) {
        return;
    }

    m_transaction->subscriber.password = valueToDigits(frame.msPassword, 3);
    m_state = roaming ? State::RoamingConfirm : State::MoConfirm;
    m_txFrameCount = 0;
    m_timer->start(kStateWatchdog);
    if (roaming) {
        m_host->roamingConfirmed(m_transaction->subscriber);
    }
}

// Digits come as pairs of frames: 14a carries the 1st, 3rd, 5th digit and 14b the 2nd, 4th,
// 6th. A digit is repeated until the next one follows; the frame count since the last new digit
// tells a repeat from a digit that was lost. Frame 15 (idle) ends the number.
void NmtChannel::rxMoDialling(const Frame& frame)
{
    if (frame.mt == MessageType::Frame15)
    {
        completeDialling();
        return;
    }

    const size_t len = m_dialled.size();
    char digit = 0;

    switch (takeDigit(frame, len, kMaxDialled, digit))
    {
    case DigitResult::Foreign:
        return;
    case DigitResult::Repeat:
    case DigitResult::Damaged:
        m_timer->start(kDiallingTimeout);
        return;
    case DigitResult::TooLong:
        // A longer number would be a wrong number: the 33rd digit ends the call.
        m_timer->start(kDiallingTimeout);
        release();
        return;
    case DigitResult::Lost:
        m_timer->start(kDiallingTimeout);
        release();
        return;
    case DigitResult::Digit:
        break;
    }

    m_timer->start(kDiallingTimeout);
    m_dialled.push_back(digit);
    // the dial tone ends with the first digit
    if (len == 0) {
        setDspMode(DspMode::Audio);
    }
}

// A digit frame of the phone. `count` digits were taken so far, up to `limit`; the frame count
// since the last new digit tells a repeat from a digit that was lost. A digit that is taken
// resets the frame count.
NmtChannel::DigitResult NmtChannel::takeDigit(const Frame& frame, size_t count, size_t limit, char& digit)
{
    const bool odd = (frame.mt == MessageType::Frame14a);      // 1st, 3rd, ... digit
    if (!odd && frame.mt != MessageType::Frame14b) {
        return DigitResult::Foreign;
    }
    if (!matchChannel(frame) || !matchArea(frame) || !matchSubscriber(frame)) {
        return DigitResult::Foreign;
    }

    // An even frame at the limit is the repeat of the last digit.
    if (count >= limit) {
        return odd ? DigitResult::TooLong : DigitResult::Repeat;
    }

    bool lost;
    if (odd) {
        if (count & 1) {
            // one more digit than frames of that kind: a repeat, unless the even one got lost
            lost = m_rxFrameCount > 1;
            if (!lost) {
                return DigitResult::Repeat;
            }
        } else {
            lost = count && m_rxFrameCount > 3;
        }
    } else {
        if (!count) {
            lost = true;                        // an even digit before the first one
        } else if (!(count & 1)) {
            lost = m_rxFrameCount > 1;
            if (!lost) {
                return DigitResult::Repeat;
            }
        } else {
            lost = m_rxFrameCount > 3;
        }
    }
    if (lost) {
        return DigitResult::Lost;
    }

    // Position marker in the upper digits (00 for odd, FF for even digits), the digit itself is
    // sent three times; anything else is a damaged frame.
    if ((frame.digit >> 12) != (odd ? 0x00u : 0xffu)) {
        return DigitResult::Damaged;
    }
    if (((frame.digit >> 8) & 0xf) != ((frame.digit >> 4) & 0xf) || ((frame.digit >> 4) & 0xf) != (frame.digit & 0xf)) {
        return DigitResult::Damaged;
    }

    digit = valueToDigit(frame.digit);
    m_rxFrameCount = 0;
    return DigitResult::Digit;
}

// The number is complete: '+' replaces the A (international) and B (service number) prefixes.
void NmtChannel::completeDialling()
{
    if (m_dialled.empty()) {
        return;
    }

    if (m_dialled[0] == 'A' || m_dialled[0] == 'B') {
        m_dialled[0] = '+';
    }

    // The call exists while the host decides, so that a release from inside the handler
    // is a release of this call. The handler gets a copy: it may end the transaction.
    const std::string dialled = m_dialled;
    NmtTransaction* const transaction = m_transaction;

    transaction->callUp = true;
    const bool accepted = m_host->callDialled(*transaction, dialled);

    // the handler released the call or ended the transaction: that stands
    if (!m_transaction || m_state != State::MoDialling) {
        return;
    }

    if (!accepted)
    {
        m_transaction->callUp = false;
        release();
        return;
    }

    m_timer->start(kStateWatchdog);
    m_state = State::MoComplete;
    setDspMode(DspMode::Frame);
    m_txFrameCount = 0;
}

// The phone answers the page: call acknowledgement (10a), on a combined channel (10d), or from
// a priority phone (11b).
void NmtChannel::rxMtPaging(const Frame& frame)
{
    if (frame.mt != MessageType::Frame10a && frame.mt != MessageType::Frame10d && frame.mt != MessageType::Frame11b) {
        return;
    }
    if (!matchChannel(frame) || !matchSubscriber(frame)) {
        return;
    }

    if (frame.mt == MessageType::Frame11b) {
        m_transaction->priority = true;
    }

    m_state = State::MtChannel;
    m_txFrameCount = 0;
    m_timer->start(kStateWatchdog);
    m_host->pagingAnswered(*m_transaction, *this);
}

// The phone is on the traffic channel and has identified itself: it rings.
void NmtChannel::rxMtIdent(const Frame& frame)
{
    if (frame.mt != MessageType::Frame10b && frame.mt != MessageType::Frame10c) {
        return;
    }
    if (!matchSubscriber(frame)) {
        return;
    }
    // On a combined channel the phone repeats its acknowledgement of the page for a while; the
    // identity is the answer to our request, at least two frame periods after the allocation.
    // A separate traffic channel has no such repeat.
    if (m_config.type == ChannelType::CcTc && m_rxFrameCount < 2) {
        return;
    }

    m_transaction->subscriber.password = valueToDigits(frame.msPassword, 3);

    // A call that only carries message indicators is not rung: the phone is asked to answer by itself.
    if (m_transaction->messageCall)
    {
        m_state = State::MtAutoanswer;
        m_txFrameCount = 0;
        m_autoanswerGo = false;
        m_autoanswerAcked = false;
        m_phoneIdle = 0;
        m_timer->start(kStateWatchdog);
        return;
    }

    m_state = State::MtRinging;
    // the number of the caller comes before the first ring
    m_txFrameCount = m_config.sendCallerId ? 4 : 0;
    m_callerIdCount = m_config.sendCallerId ? 1 : 0;
    m_timer->start(kRingingTimeout);
    m_host->callAlerting(*m_transaction);
}

// Automatic answer (scheme B2, NMT Doc 450-1 para 4.4.1.12): the order to receive a short message,
// L(12) four times once the phone is quiet, then idle frames while it acknowledges with L(12).
// A phone that does not know the order is rung after all (the call goes on as an ordinary one).
Frame NmtChannel::txMtAutoanswer()
{
    Frame frame;

    frame.mt = MessageType::Frame6;

    if (!m_autoanswerGo)
    {
        // the phone sends idle frames after its identity (15, 15, 15, 15); do not start before,
        // but not forever
        if (++m_txFrameCount >= kAutoanswerWait) {
            m_autoanswerGo = true;
            m_txFrameCount = 0;
        }
        return frame;
    }

    const int n = ++m_txFrameCount;

    if (n <= kAckFrames) {
        return lineSignalFrame(12);
    }
    // the phone answered while the order was going out: the order is complete, then 3 idle frames
    if (n == kAckFrames + 1 && m_autoanswerAcked)
    {
        beginMwiClear(3, false);
        return txMwiClear();
    }
    if (n <= kAckFrames + kLeaveIdleFrames) {
        return frame;
    }

    // no acknowledgement: ring the phone
    m_state = State::MtRinging;
    m_txFrameCount = m_config.sendCallerId ? 4 : 0;
    m_callerIdCount = m_config.sendCallerId ? 1 : 0;
    m_timer->start(kMessageRingTimeout);
    return frame;
}

void NmtChannel::rxMtAutoanswer(const Frame& frame)
{
    if (frame.mt == MessageType::Frame15 && !m_autoanswerGo)
    {
        // identity 4 frames, then idle frames 4 frames: the order follows the last of them
        if (++m_phoneIdle >= kPhoneIdleFrames)
        {
            m_autoanswerGo = true;
            m_txFrameCount = 0;
        }
        return;
    }
    if (frame.mt != MessageType::Frame13a || !m_autoanswerGo) {
        return;
    }
    if (!matchChannel(frame) || !matchSubscriber(frame) || lineSignalMajority(frame.lineSignal, 5, 3) != 12) {
        return;
    }

    // The phone answers (scheme B2: while the order goes out, it needs one valid copy). The order
    // is sent to its end, then 3 idle frames count, then the indicators and the release.
    if (m_txFrameCount < kAckFrames)
    {
        m_autoanswerAcked = true;
        return;
    }
    const int sent = std::max(0, m_txFrameCount - kAckFrames);

    beginMwiClear(std::max(0, kAckIdleFrames - sent), false);
}

// The indicators go out right before the clearing signal (4.4.1.16): 5c twice, L(15) four times.
// Then the phone acknowledges (L(1)); if it cleared first that was the acknowledgement.
void NmtChannel::beginMwiClear(int leadIdle, bool phoneCleared)
{
    m_state = State::MwiClear;
    m_txFrameCount = 0;
    m_mwiLead = leadIdle;
    m_mwiAcked = phoneCleared;
    setDspMode(DspMode::Frame);
    m_timer->start(kStateWatchdog);
}

Frame NmtChannel::txMwiClear()
{
    Frame frame;
    const int n = ++m_txFrameCount;

    if (n <= m_mwiLead)
    {
        frame.mt = MessageType::Frame6;
        return frame;
    }

    const int k = n - m_mwiLead;

    if (k <= 2)
    {
        frame = baseFrame(MessageType::Frame5c);
        frame.msCountry = static_cast<uint8_t>(digitsToValue(std::string(1, m_transaction->subscriber.country)));
        frame.msNumber = static_cast<uint32_t>(digitsToValue(m_transaction->subscriber.number));
        frame.waitingInfo = mwiWireInfo(m_transaction->mwiFlags);
        m_transaction->mwiResult = NmtServiceEnd::NoAck;
        return frame;
    }

    frame = lineSignalFrame(15);
    if (k == 2 + kAckFrames)
    {
        if (m_mwiAcked)
        {
            // the phone cleared first (or answered during the signal): this is the end
            NmtTransaction* const transaction = m_transaction;

            transaction->mwiResult = NmtServiceEnd::Ok;
            m_host->destroyTransaction(transaction);
        }
        else
        {
            m_state = State::MtRelease;
            m_txFrameCount = 0;
            m_timer->start(kReleaseTimeout);
        }
    }

    return frame;
}

void NmtChannel::rxMwiClear(const Frame& frame)
{
    if (frame.mt != MessageType::Frame13a || !matchChannel(frame) || !matchSubscriber(frame)
     || lineSignalMajority(frame.lineSignal, 5, 3) != 1) {
        return;
    }

    // The phone clears before the indicators went out (the user hung up the short call): it is
    // released like for any clearing, and nothing was delivered.
    if (m_txFrameCount <= m_mwiLead && !m_mwiAcked)
    {
        m_state = State::MoRelease;
        m_txFrameCount = 0;
        m_timer->start(kStateWatchdog);
        return;
    }

    // the phone acknowledges the clearing while the signal is on
    m_mwiAcked = true;
}

// The phone answers with L(14).
void NmtChannel::rxMtRinging(const Frame& frame)
{
    if (frame.mt != MessageType::Frame13a) {
        return;
    }
    if (!matchChannel(frame) || !matchSubscriber(frame)) {
        return;
    }
    if (lineSignalMajority(frame.lineSignal, 5, 3) != 14) {
        return;
    }

    m_timer->start(kStateWatchdog);
    m_transaction->answered = true;
    m_state = State::MtComplete;
    m_txFrameCount = 0;
    m_host->callAnswered(*m_transaction);
}

// The phone acknowledges our release with L(1), the release guard.
void NmtChannel::rxMtRelease(const Frame& frame)
{
    if (frame.mt != MessageType::Frame13a) {
        return;
    }
    if (!matchChannel(frame) || !matchSubscriber(frame)) {
        return;
    }
    if (lineSignalMajority(frame.lineSignal, 5, 3) != 1) {
        return;
    }

    // the clearing that carried the indicators is acknowledged
    if (m_transaction->mwiResult == NmtServiceEnd::NoAck) {
        m_transaction->mwiResult = NmtServiceEnd::Ok;
    }
    m_host->destroyTransaction(m_transaction);
}

// ---- states ----

// Releases the connection towards the phone: L(15) until it answers with the release guard or
// the guard time is over.
void NmtChannel::release()
{
    // a phone that was sent to another channel is released there too
    if (m_transaction && m_transaction->scipTarget) {
        m_host->scipEnded(*m_transaction, true);
    }

    const State previous = m_state;

    m_state = State::MtRelease;
    m_txFrameCount = 0;
    setDspMode(DspMode::Frame);
    m_timer->start(kReleaseTimeout);
    // message indicators for the phone go out before the release (4.4.1.16)
    if (m_transaction && m_transaction->mwiFlags && inCallState(previous)
     && m_transaction->mwiResult == NmtServiceEnd::Undelivered) {
        beginMwiClear(0, false);
    }
    abortInCall(previous);
}

// A power change or a register recall that does not finish because the call ends: the host is told.
void NmtChannel::abortInCall(State previous)
{
    if (!m_transaction) {
        return;
    }

    NmtTransaction* const transaction = m_transaction;

    if (previous == State::PowerChange) {
        m_host->powerChanged(*transaction, m_newPower, false);
    } else if (previous == State::RecallProceed || previous == State::RecallDigits) {
        m_host->recallEnded(*transaction, m_recallDigits);
    }
}

void NmtChannel::goIdle()
{
    if (m_timer) {
        m_timer->stop();
    }
    m_transaction = nullptr;
    m_state = State::Idle;
    m_txFrameCount = 0;
    m_dialled.clear();
    m_recallDigits.clear();
    m_digitCount = 0;
    m_alarmRunning = false;
    m_rxMute = 0;
    m_superDetector.reset();
    setDspMode(DspMode::Frame);
}

void NmtChannel::transactionEnded()
{
    goIdle();
}

// No release guard from the phone in MtRelease, or the channel stopped sending in another state.
void NmtChannel::onTimer()
{
    if (!m_transaction) {
        return;
    }

    // switching the call to another channel
    if ((m_state == State::ScipIdent || m_state == State::ScipIdentReq) && m_transaction->scipTarget != this)
    {
        // the switch was ended without this channel being told
        NmtTransaction* const transaction = m_transaction;

        goIdle();
        m_host->detachChannel(*this, transaction);
        return;
    }

    switch (m_state)
    {
    case State::MftIn:
    case State::MftOut:
    case State::RecallEnd:
        // this channel stopped sending: the conversation goes on
        enterActive();
        return;
    case State::RecallProceed:
    {
        NmtTransaction* const transaction = m_transaction;
        const std::string digits = m_recallDigits;

        enterActive();
        m_host->recallEnded(*transaction, digits);
        return;
    }
    case State::PowerChange:
    {
        NmtTransaction* const transaction = m_transaction;
        const int level = m_newPower;

        enterActive();
        m_host->powerChanged(*transaction, level, false);
        return;
    }
    case State::Mft:
        // the phone is silent: the converter state is ended like a forced release
        beginMftOut();
        return;
    case State::RecallDigits:
        beginRecallEnd();
        return;
    case State::ScipIdent:
        // no identity yet: ask for it
        m_state = State::ScipIdentReq;
        m_txFrameCount = 0;
        m_timer->start(kScipRequestTime);
        return;
    case State::ScipIdentReq:
        // the phone is not here: the call stays on the old channel, this one sends the release
        m_host->scipEnded(*m_transaction, true);
        return;
    case State::ScipRelease:
        {
            NmtTransaction* const transaction = m_transaction;

            goIdle();
            m_host->detachChannel(*this, transaction);
        }
        return;
    case State::ScipOrder:
        // this channel stopped sending: the order is off, the call goes on
        m_host->scipEnded(*m_transaction, true);
        enterActive();
        return;
    default:
        break;
    }

    // a pause in the dialling: the phone is released like for any other failure
    if (m_state == State::MoDialling)
    {
        release();
        return;
    }

    // the phone does not answer, or no traffic channel became free: the call fails, the phone
    // is released
    if (m_state == State::MtRinging || m_state == State::MtQueue)
    {
        if (m_state == State::MtQueue && m_transaction->messageCall) {
            m_transaction->mwiResult = NmtServiceEnd::NoChannel;
        }
        // no answer is a plain release for the caller, no free channel is congestion
        failCall(m_state == State::MtRinging ? modemcellular::Cause::Normal : modemcellular::Cause::NoChannel);
        if (m_transaction) {
            release();
        }
        return;
    }

    // no supervisory tone from the phone: the call is over, the phone is released
    if (m_state == State::Active)
    {
        NmtTransaction* const transaction = m_transaction;

        // the phone is gone: the indicators would go nowhere, the host can send them again
        if (transaction->mwiResult == NmtServiceEnd::Undelivered) {
            transaction->mwiResult = NmtServiceEnd::Unreachable;
        }
        if (transaction->callUp)
        {
            transaction->callUp = false;
            m_host->callCleared(*transaction);
        }
        if (m_transaction == transaction && m_state == State::Active) {
            release();
        }
        return;
    }

    // the channel stopped sending while a call was set up: the exchange has to be told. A call
    // to the phone that never got to the answer fails, an answered one is cleared.
    if (m_transaction->callUp)
    {
        NmtTransaction* const transaction = m_transaction;
        const bool notAnswered = (m_state == State::MtIdent || m_state == State::MtChannel);

        if (notAnswered)
        {
            failCall(modemcellular::Cause::TempFail);
        }
        else
        {
            transaction->callUp = false;
            m_host->callCleared(*transaction);
        }
        if (m_transaction != transaction) {
            return;
        }
    }

    m_host->destroyTransaction(m_transaction);
}

} // namespace modemnmt
