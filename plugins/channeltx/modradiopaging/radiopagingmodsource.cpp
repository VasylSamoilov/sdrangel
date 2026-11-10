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

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

#include "radiopagingmodsource.h"

RadioPagingModSource::RadioPagingModSource() :
    m_channelSampleRate(48000),
    m_channelFrequencyOffset(0),
    m_channel(nullptr),
    m_clock([]() { return QDateTime::currentMSecsSinceEpoch(); }),
    m_linearGain(1.0f),
    m_streamProtocol(RadioPagingModSettings::Pocsag),
    m_streamMode(RadioPagingModSettings::OneShot),
    m_streamMixed(false),
    m_mix(nullptr),
    m_streamEnded(true),
    m_pushed(0),
    m_ersStart(0),
    m_ersEnd(0),
    m_state(Idle),
    m_pow(0.0f),
    m_powRamp(0.0f),
    m_magsq(0.0),
    m_levelCalcCount(0),
    m_rmsLevel(0.0),
    m_peakLevelOut(0.0),
    m_peakLevel(0.0f),
    m_levelSum(0.0f)
{
    applySettings(QStringList(), m_settings, true);
    applyChannelSettings(m_channelSampleRate, m_channelFrequencyOffset, true);
}

RadioPagingModSource::~RadioPagingModSource()
{
}

void RadioPagingModSource::pull(SampleVector::iterator begin, unsigned int nbSamples)
{
    std::for_each(
        begin,
        begin + nbSamples,
        [this](Sample& s) {
            pullOne(s);
        }
    );
}

void RadioPagingModSource::pullOne(Sample& sample)
{
    if (m_settings.m_channelMute)
    {
        sample.m_real = 0.0f;
        sample.m_imag = 0.0f;
        return;
    }

    modulateSample();

    Complex ci = m_modSample * m_carrierNco.nextIQ();

    double magsq = ci.real() * ci.real() + ci.imag() * ci.imag();
    m_movingAverage(magsq);
    m_magsq = m_movingAverage.asDouble();

    sample.m_real = (FixReal) (ci.real() * SDR_TX_SCALEF);
    sample.m_imag = (FixReal) (ci.imag() * SDR_TX_SCALEF);
}

// dB per sample over the ramp range; a ramp shorter than a sample is a step
Real RadioPagingModSource::rampStep(Real ms) const
{
    Real samples = ms * m_channelSampleRate / 1000.0f;
    Real range = (Real) std::max(0, m_settings.m_rampRange);
    return (samples >= 1.0f) ? range / samples : range;
}

// The stream of the protocol and mode of the settings. GSC has none: nothing is sent.
// Called with the mutex held
void RadioPagingModSource::makeStream()
{
    m_streamProtocol = m_settings.m_protocol;
    m_streamMode = effectiveMode();
    bool network = (m_streamMode == RadioPagingModSettings::Network);

    m_streamMixed = mixing();
    m_mix = nullptr;

    switch (m_settings.m_protocol)
    {
    case RadioPagingModSettings::Pocsag:
        m_stream.reset(new RadioPagingPocsagTxStream(network, m_settings.m_pocsagSpeed, m_settings.m_pocsagInverted));
        break;
    case RadioPagingModSettings::Flex:
        if (m_streamMixed)
        {
            m_mix = new RadioPagingMixTxStream(mixConfig(m_settings));
            m_stream.reset(m_mix);
        }
        else
        {
            m_stream.reset(new RadioPagingFlexTxStream(network, (RadioPagingFlex::Mode) m_settings.m_flexMode,
                m_settings.m_flexInverted, m_settings.m_flexCollapse));
        }
        break;
    case RadioPagingModSettings::Gsc:
        // GSC has no network mode: always one-shot
        m_stream.reset(new RadioPagingGscTxStream((RadioPagingGsc::Mode) m_settings.m_gscMode));
        break;
    default:
        m_stream.reset();
        break;
    }
}

// GSC has no network mode: it is one-shot whatever the setting
RadioPagingModSettings::Mode RadioPagingModSource::effectiveMode() const
{
    return (m_settings.m_protocol == RadioPagingModSettings::Gsc) ? RadioPagingModSettings::OneShot : m_settings.m_mode;
}

// FLEX network with POCSAG mixed in
bool RadioPagingModSource::mixing() const
{
    return (m_settings.m_protocol == RadioPagingModSettings::Flex) && (m_settings.m_mode == RadioPagingModSettings::Network)
        && m_settings.m_flexMixPocsag;
}

RadioPagingMixTxStream::Config RadioPagingModSource::mixConfig(const RadioPagingModSettings& settings)
{
    RadioPagingMixTxStream::Config config;

    config.m_flexMode = (RadioPagingFlex::Mode) settings.m_flexMode;
    config.m_flexInverted = settings.m_flexInverted;
    config.m_collapse = settings.m_flexCollapse;
    config.m_framesPerMinute = settings.m_mixFramesPerMinute;
    config.m_shared = settings.m_mixShared;
    config.m_flexDeviation = settings.m_deviation;
    config.m_pocsagDeviation = settings.m_pocsagDeviation;
    config.m_pocsagBaud = settings.m_pocsagSpeed;
    config.m_pocsagInverted = settings.m_pocsagInverted;

    for (int i = 0; i < 3; i++) {
        config.m_switchMs[i] = settings.m_mixSwitchMs[i];
    }

    return config;
}

// The defaults of the settings in the stream: the idle signal and the time grid. Called with
// the mutex held
void RadioPagingModSource::applyStreamDefaults()
{
    if (!m_stream) {
        return;
    }

    if (m_streamProtocol == RadioPagingModSettings::Pocsag)
    {
        ((RadioPagingPocsagTxStream*) m_stream.get())->setDefaultGroup(m_settings.m_pocsagSpeed, m_settings.m_pocsagInverted);
    }
    else if (m_streamProtocol == RadioPagingModSettings::Gsc)
    {
        ((RadioPagingGscTxStream*) m_stream.get())->setMode((RadioPagingGsc::Mode) m_settings.m_gscMode);
    }
    else if (m_streamProtocol == RadioPagingModSettings::Flex)
    {
        RadioPagingFlexTxStream *flex = m_mix ? &m_mix->flex() : (RadioPagingFlexTxStream*) m_stream.get();
        std::string error;

        if (m_mix)
        {
            // Mode, collapse, plan, polarity of the next transmission, POCSAG default group,
            // deviations and switch patterns
            if (!m_mix->setConfig(mixConfig(m_settings), error)) {
                qWarning() << "RadioPagingModSource::applyStreamDefaults:" << QString::fromStdString(error);
            }
        }
        else
        {
            flex->setMode((RadioPagingFlex::Mode) m_settings.m_flexMode);
            // The re-synchronization length; network: also the schedule of the frames
            flex->setCollapse(m_settings.m_flexCollapse, error);
            flex->setInverted(m_settings.m_flexInverted);
        }

        // Another network polarity: the transmission ends, it starts again with the
        // re-synchronization in the new polarity; the queues are kept
        if (m_settings.m_flexInverted != flex->inverted())
        {

            if ((m_state == RampUp) || (m_state == Tx))
            {
                dropInFlight();
                endTransmission();
            }
        }
    }
}

// The messages of the blocks not completely sent are dropped: those in the modulator and
// those the stream prepared for the transmission. Called with the mutex held
void RadioPagingModSource::dropInFlight()
{
    if (m_stream) {
        drop(m_stream->cut());
    }

    for (const InFlight& entry : m_inFlight) {
        drop(entry.m_ids);
    }

    m_inFlight.clear();
}

// The transmission ends now: no more blocks, the power ramps down from where it is.
// Called with the mutex held
void RadioPagingModSource::endTransmission()
{
    m_streamEnded = true;

    if ((m_state == RampUp) || (m_state == Tx))
    {
        m_powRamp = rampStep(m_settings.m_rampDownMs);
        m_state = RampDown;
    }
}

// Called with the mutex held
void RadioPagingModSource::startTransmission()
{
    m_mod.reset();
    // Mixed blocks set their own deviation: back to the channel's for the other streams
    m_mod.setDeviation(m_settings.m_deviation);
    m_pushed = 0;
    m_ersStart = 0;
    m_ersEnd = 0;
    m_inFlight.clear();
    m_streamEnded = false;
    m_stream->begin(m_clock());
    feed();

    if (m_streamEnded && !m_mod.busy()) {
        return;     // Nothing to send after all
    }

    // A ramp shorter than a sample is no ramp: full power from the first sample
    if ((m_settings.m_rampUpMs * m_channelSampleRate / 1000.0f < 1.0f) || (m_settings.m_rampRange <= 0))
    {
        m_pow = 0.0f;
        m_state = Tx;
    }
    else
    {
        m_pow = -(Real) m_settings.m_rampRange;
        m_powRamp = rampStep(m_settings.m_rampUpMs);
        m_state = RampUp;
    }
}

// The next block once the modulator has sent its last symbol (it is pushed before the next
// symbol is due, the channel rate being at least the internal rate): the stream never runs dry
// and its blocks are taken as late as possible (messages queued meanwhile get in)
void RadioPagingModSource::feed()
{
    if (m_streamEnded || !m_stream || (m_mod.queued() > 0)) {
        return;
    }

    RadioPagingTxBlock block;

    if (!m_stream->next(block))
    {
        m_streamEnded = true;
        return;
    }

    if (!m_mod.push(block.m_baud, block.m_levels, block.m_shaping, block.m_deviation))
    {
        qWarning() << "RadioPagingModSource::feed: baud rate not supported:" << block.m_baud;
        fail(block.m_ids);
        m_streamEnded = true;
        return;
    }

    if (block.m_ers)
    {
        m_ersStart = m_pushed;
        m_ersEnd = m_pushed + block.m_levels.size();
        m_status.m_ersMs = (int) ((block.m_levels.size() * 1000) / block.m_baud);
        m_status.m_ersCount++;
    }

    m_pushed += block.m_levels.size();

    if (!block.m_ids.empty()) {
        m_inFlight.push_back(InFlight{m_pushed, block.m_ids});
    }

    // The messages are on air: with this block, or the voice pages with their first one
    if (m_notifyTransmitted)
    {
        for (uint64_t id : block.m_startIds) {
            m_notifyTransmitted(id);
        }
        for (uint64_t id : block.m_ids) {
            m_notifyTransmitted(id);
        }
    }
}

// Messages whose last symbol has been sent are sent
void RadioPagingModSource::complete()
{
    quint64 finished = m_pushed - std::min<quint64>(m_pushed, m_mod.queued());

    while (!m_inFlight.empty() && (m_inFlight.front().m_endSymbol <= finished))
    {
        m_status.m_sent += m_inFlight.front().m_ids.size();
        m_status.m_lastSentId = m_inFlight.front().m_ids.back();

        if (m_notify)
        {
            for (uint64_t id : m_inFlight.front().m_ids) {
                m_notify(id, true);
            }
        }

        m_inFlight.pop_front();
    }
}

void RadioPagingModSource::modulateSample()
{
    QMutexLocker mutexLocker(&m_mutex);

    m_status.m_sampleCounter++;

    if ((m_state == Idle) && m_stream && m_stream->hasWork()) {
        startTransmission();
    }

    if (m_state == Idle)
    {
        m_modSample = Complex(0.0f, 0.0f);
        calculateLevel(0.0f);
        return;
    }

    feed();

    Complex c = m_mod.next();
    Real linearRampGain = std::pow(10.0f, m_pow / 20.0f);
    m_modSample = c * (m_linearGain * linearRampGain);

    complete();

    switch (m_state)
    {
    case RampUp:
        m_pow += m_powRamp;

        if (m_pow >= 0.0f)
        {
            m_pow = 0.0f;
            m_state = Tx;
        }
        break;
    case Tx:
        // The last symbol has left the shaping filter: ramp down on the carrier it ended on
        if (m_streamEnded && !m_mod.busy())
        {
            m_powRamp = rampStep(m_settings.m_rampDownMs);
            m_state = RampDown;
        }
        break;
    case RampDown:
        m_pow -= m_powRamp;

        if (m_pow <= -(Real) std::max(0, m_settings.m_rampRange))
        {
            // Blocks still in the modulator were cut (abort, change of mode)
            dropInFlight();
            m_mod.reset();
            m_state = Idle;
        }
        break;
    default:
        break;
    }

    calculateLevel(std::abs(m_modSample));
}

void RadioPagingModSource::calculateLevel(Real sample)
{
    if (m_levelCalcCount < m_levelNbSamples)
    {
        m_peakLevel = std::max(std::fabs(m_peakLevel), sample);
        m_levelSum += sample * sample;
        m_levelCalcCount++;
    }
    else
    {
        m_rmsLevel = std::sqrt(m_levelSum / m_levelNbSamples);
        m_peakLevelOut = m_peakLevel;
        m_peakLevel = 0.0f;
        m_levelSum = 0.0f;
        m_levelCalcCount = 0;
    }
}

void RadioPagingModSource::addRequest(const Request& request)
{
    QMutexLocker mutexLocker(&m_mutex);
    std::string error;
    bool ok = false;

    // A request for a protocol the channel no longer sends is dropped. A mixed FLEX channel sends POCSAG too
    if (m_mix)
    {
        if (request.m_protocol == RadioPagingModSettings::Pocsag) {
            ok = m_mix->addPocsag(request.m_pocsag, request.m_pocsagBaud, request.m_pocsagInverted, request.m_id, error);
        } else if (request.m_protocol == RadioPagingModSettings::Flex) {
            ok = m_mix->addFlex(request.m_flex, request.m_flexInverted, request.m_id, error);
        }
    }
    else if (m_stream && (request.m_protocol == m_streamProtocol))
    {
        if (m_streamProtocol == RadioPagingModSettings::Gsc) {
            RadioPagingGscTxStream *gsc = (RadioPagingGscTxStream*) m_stream.get();

            if (request.m_live) {
                ok = gsc->addLive(request.m_gsc, request.m_gscInverted, request.m_id, error, m_settings.m_gscVoiceSeconds);
            } else {
                ok = gsc->add(request.m_gsc, request.m_gscInverted, request.m_id, error, request.m_voice);
            }
        } else if (m_streamProtocol == RadioPagingModSettings::Pocsag) {
            ok = ((RadioPagingPocsagTxStream*) m_stream.get())->add(request.m_pocsag, request.m_pocsagBaud,
                request.m_pocsagInverted, request.m_id, error);
        } else if (m_streamProtocol == RadioPagingModSettings::Flex) {
            ok = ((RadioPagingFlexTxStream*) m_stream.get())->add(request.m_flex, request.m_flexInverted, request.m_id, error);
        }
    }

    if (!ok)
    {
        if (error.empty()) {
            error = "the channel does not send this protocol";
        }

        qWarning() << "RadioPagingModSource::addRequest: message" << request.m_id << "dropped:" << QString::fromStdString(error);
        fail({request.m_id});
    }
}

// Messages that can not be transmitted: counted, and reported as failed
void RadioPagingModSource::fail(const std::vector<uint64_t>& ids)
{
    m_status.m_dropped += ids.size();

    for (uint64_t id : ids)
    {
        if (m_notifyFailed) {
            m_notifyFailed(id);
        } else if (m_notify) {
            m_notify(id, false);
        }
    }
}

// Messages that will not be sent: counted, and reported
void RadioPagingModSource::drop(const std::vector<uint64_t>& ids)
{
    m_status.m_dropped += ids.size();

    if (m_notify)
    {
        for (uint64_t id : ids) {
            m_notify(id, false);
        }
    }
}

void RadioPagingModSource::clear()
{
    QMutexLocker mutexLocker(&m_mutex);

    if (m_stream) {
        drop(m_stream->clear());
    }
}

// Voice of a live voice page: resampled to the rate of the modulator
void RadioPagingModSource::pushLive(const std::vector<float>& samples)
{
    QMutexLocker mutexLocker(&m_mutex);

    if (m_stream && (m_streamProtocol == RadioPagingModSettings::Gsc)) {
        ((RadioPagingGscTxStream*) m_stream.get())->pushLive(samples.data(), samples.size());
    }
}

void RadioPagingModSource::endLive()
{
    QMutexLocker mutexLocker(&m_mutex);

    if (m_stream && (m_streamProtocol == RadioPagingModSettings::Gsc)) {
        ((RadioPagingGscTxStream*) m_stream.get())->endLive();
    }
}

// One-shot: the transmission being sent is cut and its messages dropped, unless it is ramping
// down (all its symbols are out: its messages are sent). Network: the transmission goes on,
// the blocks already in the modulator are sent
void RadioPagingModSource::abort()
{
    QMutexLocker mutexLocker(&m_mutex);

    if (!m_stream) {
        return;
    }

    drop(m_stream->clear());

    if (!m_stream->continuous() && ((m_state == RampUp) || (m_state == Tx)))
    {
        dropInFlight();
        endTransmission();
    }
}

RadioPagingModSource::Status RadioPagingModSource::getStatus() const
{
    QMutexLocker mutexLocker(&m_mutex);
    Status status = m_status;
    status.m_state = m_state;
    status.m_queued = m_stream ? (int) m_stream->pending() : 0;
    status.m_inFlight = 0;

    for (const InFlight& entry : m_inFlight) {
        status.m_inFlight += (int) entry.m_ids.size();
    }

    status.m_currentId = m_inFlight.empty() ? 0 : m_inFlight.front().m_ids.front();

    if (m_stream && (m_streamProtocol == RadioPagingModSettings::Gsc))
    {
        RadioPagingGscTxStream *gsc = (RadioPagingGscTxStream*) m_stream.get();
        status.m_liveActive = gsc->liveActive();
        status.m_liveOpen = gsc->liveOpen();
        status.m_liveId = gsc->liveId();
    }

    // The re-synchronization is on air while one of its symbols is being sent
    quint64 finished = m_pushed - std::min<quint64>(m_pushed, m_mod.queued());
    status.m_ers = (m_state != Idle) && (m_ersEnd > m_ersStart) && (finished >= m_ersStart) && (finished < m_ersEnd);
    return status;
}

void RadioPagingModSource::applySettings(const QStringList& settingsKeys, const RadioPagingModSettings& settings, bool force)
{
    QMutexLocker mutexLocker(&m_mutex);
    bool wasMuted = m_settings.m_channelMute;

    if (settingsKeys.contains("deviation") || force) {
        m_mod.setDeviation(settings.m_deviation);
    }

    if (settingsKeys.contains("gain") || force) {
        m_linearGain = std::pow(10.0f, settings.m_gain / 20.0f);
    }

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    // Another protocol or mode: the transmission ends and what is queued is dropped
    if (!m_stream || (m_settings.m_protocol != m_streamProtocol) || (effectiveMode() != m_streamMode)
        || (mixing() != m_streamMixed))
    {
        if (m_stream)
        {
            drop(m_stream->clear());
            dropInFlight();
            endTransmission();
        }

        makeStream();
    }
    else
    {
        applyStreamDefaults();
    }

    // FLEX frames keep their time: a muted FLEX transmission ends, it starts again (with the
    // re-synchronization) when unmuted
    if (m_settings.m_channelMute && !wasMuted && (m_streamProtocol == RadioPagingModSettings::Flex) && (m_state != Idle))
    {
        dropInFlight();
        m_mod.reset();
        m_streamEnded = true;
        m_state = Idle;
    }
}

void RadioPagingModSource::applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force)
{
    qDebug() << "RadioPagingModSource::applyChannelSettings:"
            << " channelSampleRate: " << channelSampleRate
            << " channelFrequencyOffset: " << channelFrequencyOffset;

    QMutexLocker mutexLocker(&m_mutex);

    if ((channelFrequencyOffset != m_channelFrequencyOffset)
     || (channelSampleRate != m_channelSampleRate) || force)
    {
        m_carrierNco.setFreq(channelFrequencyOffset, channelSampleRate);
    }

    if ((channelSampleRate != m_channelSampleRate) || force) {
        m_mod.setChannelSampleRate(channelSampleRate);
    }

    m_channelSampleRate = channelSampleRate;
    m_channelFrequencyOffset = channelFrequencyOffset;
}
