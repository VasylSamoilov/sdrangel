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
#include <QDir>
#include <QMutexLocker>
#include <QStorageInfo>
#include <QUuid>

#include <algorithm>
#include <limits>

#include "dsp/dspengine.h"
#include "channel/channelapi.h"
#include "util/messagequeue.h"
#include "maincore.h"

#include "radiopagingpocsag.h"
#include "radiopagingflex.h"
#include "radiopaginggsc.h"

#include "radiopagingvoicestore.h"
#include "radiopagingdemodsink.h"

namespace {

const int legFilterTaps = 65;
const int statusPeriodSamples = RadioPagingDemodSettings::m_channelSampleRate / 10;   // 100 ms
const double voiceCutoff = 3000.0;      // Hz, upper edge of the voice band
const int voiceWriteSamples = 4096;     // Voice samples written to the file at a time
const qint64 voiceMinFreeBytes = 500LL * 1024 * 1024;  // No recording on a disk with less free space
const Real dcBlockPole = 0.995f;        // About 30 Hz at 38400 Hz
const int audioBufferSize = 1 << 12;
const double huntCentreDecay = 0.967216;    // exp(-1 s / 30 s), per second
const double narrowResidual = 300.0;        // Hz: a leg is narrowed once a measurement is this close

enum Protocol {
    ProtocolPocsag,
    ProtocolFlex,
    ProtocolGsc
};

} // namespace

void RadioPagingDemodSink::Leg::create(double huntBandwidth, double trackBandwidth, double channelBandwidth, double deviation)
{
    m_huntBandwidth = std::min(huntBandwidth, channelBandwidth);
    m_trackBandwidth = std::min(trackBandwidth, channelBandwidth);
    m_deviation = deviation;
    m_maxShift = std::max(0.0, (channelBandwidth - trackBandwidth) / 2.0);
    m_filter.create(legFilterTaps, RadioPagingDemodSettings::m_channelSampleRate,
        (m_tracking ? m_trackBandwidth : m_huntBandwidth) / 2.0);

    // Outer deviation gives +/-1
    m_discri.setFMScaling(RadioPagingDemodSettings::m_channelSampleRate / (2.0 * deviation));
    m_discri.reset();
}

void RadioPagingDemodSink::Leg::setTracking(bool tracking)
{
    if (tracking != m_tracking)
    {
        m_tracking = tracking;
        m_filter.retune(legFilterTaps, RadioPagingDemodSettings::m_channelSampleRate,
            (m_tracking ? m_trackBandwidth : m_huntBandwidth) / 2.0);
    }
}

void RadioPagingDemodSink::Leg::setShift(double shift)
{
    shift = std::max(-m_maxShift, std::min(m_maxShift, shift));

    if (shift == m_shift) {
        return;
    }

    // The discriminator output moves by the change once the samples shifted before the
    // filter come out of it (its delay)
    m_pendingDelta += (float) (-(shift - m_shift) / m_deviation);
    m_pendingSamples = legFilterTaps / 2;
    m_shift = shift;
    m_nco.setFreq((Real) -m_shift, (Real) RadioPagingDemodSettings::m_channelSampleRate);
}

bool RadioPagingDemodSink::Leg::takeHandover(float& delta)
{
    if ((m_pendingSamples > 0) || (m_pendingDelta == 0.0f)) {
        return false;
    }

    delta = m_pendingDelta;
    m_pendingDelta = 0.0f;
    return true;
}

Real RadioPagingDemodSink::Leg::demod(const Complex& sample)
{
    double magsq;
    Real fmDev;
    Complex c = (m_shift != 0.0) ? sample * m_nco.nextIQ() : sample;

    if (m_pendingSamples > 0) {
        m_pendingSamples--;
    }

    return m_discri.phaseDiscriminatorDelta(m_filter.filter(c), magsq, fmDev);
}

// The residual offset measured by a receiver moves the leg onto the carrier and narrows it
void RadioPagingDemodSink::legMeasured(Leg& leg, int protocol, float dc)
{
    if (!RadioPagingDemodSettings::m_legAfc) {
        return;
    }

    if (leg.m_fits == 0)
    {
        leg.m_messagesAtStart = m_status.m_messages[protocol];
        leg.m_framesAtStart = (protocol == ProtocolFlex) ? m_flex.getStatus().m_frames : 0;
    }

    // Near the noise threshold a signal cut by the filter on one side is measured too close
    // to the filter centre: the leg is narrowed only once the carrier is on its centre (a
    // small residual), before that it is only moved. GSC has too few known patterns for one
    // measurement to place the carrier closely at a low signal to noise ratio: its receiving
    // leg keeps the offset room and is narrowed at the first measurement
    double residual = dc * leg.m_deviation;

    leg.m_fits++;
    leg.setShift(leg.m_shift + residual);

    if ((protocol == ProtocolGsc) || (std::fabs(residual) < narrowResidual)) {
        leg.setTracking(true);
    }
}

// The correction of a transmission that was really received (messages, or measured more than
// once, or a FLEX frame decoded) becomes the hunting centre; otherwise the previous one stays
void RadioPagingDemodSink::legReleased(Leg& leg, int protocol, bool confirmed)
{
    if (leg.m_fits == 0) {
        return;
    }

    confirmed = confirmed || (leg.m_fits >= 2) || (m_status.m_messages[protocol] > leg.m_messagesAtStart);

    if (confirmed) {
        leg.m_huntCentre = leg.m_shift;
    }
    leg.m_fits = 0;
    leg.setTracking(false);
    leg.setShift(leg.m_huntCentre);
}

// Every second: the hunting centres decay to 0 (time constant 30 s), so a wrong or old
// correction does not stay
void RadioPagingDemodSink::decayHuntCentres()
{
    for (Leg *leg : {&m_pocsagLeg, &m_flexLeg, &m_gscLeg})
    {
        // Only while hunting: a transmission being received (measured at least once) keeps
        // its correction
        if ((leg->m_fits > 0) || (leg->m_huntCentre == 0.0)) {
            continue;
        }

        leg->m_huntCentre *= huntCentreDecay;
        if (std::fabs(leg->m_huntCentre) < 10.0) {
            leg->m_huntCentre = 0.0;
        }
        leg->setShift(leg->m_huntCentre);
    }
}

RadioPagingDemodSink::RadioPagingDemodSink() :
    m_channel(nullptr),
    m_messageQueueToChannel(nullptr),
    m_channelSampleRate(RadioPagingDemodSettings::m_channelSampleRate),
    m_channelFrequencyOffset(0),
    m_interpolatorDistance(1.0f),
    m_interpolatorDistanceRemain(1.0f),
    m_magsq(0.0),
    m_magsqSum(0.0),
    m_magsqPeak(0.0),
    m_magsqCount(0),
    m_audioBufferFill(0),
    m_audioSampleRate(48000),
    m_audioDistance(1.0f),
    m_audioDistanceRemain(1.0f),
    m_audioOpen(false),
    m_dcIn(0.0f),
    m_dcOut(0.0f),
    m_statusCount(0)
{
    m_audioBuffer.resize(audioBufferSize);
    m_audioInterpolator.create(16, RadioPagingDemodSettings::m_channelSampleRate, voiceCutoff);

    for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++) {
        m_pocsag[i].create(RadioPagingDemodSettings::m_channelSampleRate, RadioPagingDemodSettings::m_pocsagSpeeds[i]);
    }
    m_flex.create(RadioPagingDemodSettings::m_channelSampleRate);
    m_gsc.create(RadioPagingDemodSettings::m_channelSampleRate);
    m_gsc.setNonBatterySaver(true);     // Every GSC transmission is decoded
    m_flex.setReportFragments(true);    // Each fragment is output, then the reassembled message

    m_status = Status();
    applySettings(QStringList(), m_settings, true);
    applyChannelSettings(m_channelSampleRate, m_channelFrequencyOffset, true);
}

RadioPagingDemodSink::~RadioPagingDemodSink()
{
}

void RadioPagingDemodSink::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
{
    Complex ci;

    for (SampleVector::const_iterator it = begin; it != end; ++it)
    {
        Complex c(it->real(), it->imag());
        c *= m_nco.nextIQ();

        if (m_interpolatorDistance < 1.0f) // interpolate
        {
            while (!m_interpolator.interpolate(&m_interpolatorDistanceRemain, c, &ci))
            {
                processOneSample(ci);
                m_interpolatorDistanceRemain += m_interpolatorDistance;
            }
        }
        else // decimate
        {
            if (m_interpolator.decimate(&m_interpolatorDistanceRemain, c, &ci))
            {
                processOneSample(ci);
                m_interpolatorDistanceRemain += m_interpolatorDistance;
            }
        }
    }
}

void RadioPagingDemodSink::processOneSample(Complex& ci)
{
    Real magsq = (ci.real() * ci.real() + ci.imag() * ci.imag()) / (SDR_RX_SCALED * SDR_RX_SCALED);

    m_movingAverage(magsq);
    m_magsq = m_movingAverage.asDouble();
    m_magsqSum += magsq;
    if (magsq > m_magsqPeak) {
        m_magsqPeak = magsq;
    }
    m_magsqCount++;

    // A protocol that is on is decoded at every speed, mode and polarity. After each sample
    // the legs follow the carrier offsets the receivers measure (see Leg)
    float dc;
    float delta;

    if (m_settings.m_pocsagEnabled)
    {
        Real fm = m_pocsagLeg.demod(ci);
        bool synced = false;

        for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++)
        {
            m_pocsag[i].process(fm);

            while (m_pocsag[i].hasMessage()) {
                sendPocsag(m_pocsag[i].takeMessage(), RadioPagingDemodSettings::m_pocsagSpeeds[i]);
            }
            if (m_pocsag[i].takeLevelsMeasured(dc)) {
                legMeasured(m_pocsagLeg, ProtocolPocsag, dc);
            }
            synced = synced || m_pocsag[i].isSynced();
        }

        if (!synced) {
            legReleased(m_pocsagLeg, ProtocolPocsag, false);
        }
        if (m_pocsagLeg.takeHandover(delta))
        {
            for (auto& pocsag : m_pocsag) {
                pocsag.offsetLevels(delta);
            }
        }
    }

    if (m_settings.m_flexEnabled)
    {
        m_flex.process(m_flexLeg.demod(ci), magsq);

        while (m_flex.hasMessage()) {
            sendFlex(m_flex.takeMessage());
        }
        if (m_flex.takeLevelsMeasured(dc)) {
            legMeasured(m_flexLeg, ProtocolFlex, dc);
        }
        if (m_flex.isHunting() && m_flexLeg.m_tracking) {
            legReleased(m_flexLeg, ProtocolFlex, m_flex.getStatus().m_frames > m_flexLeg.m_framesAtStart);
        }
        if (m_flexLeg.takeHandover(delta)) {
            m_flex.offsetLevels(delta);
        }
    }

    if (m_settings.m_gscEnabled)
    {
        Real fm = m_gscLeg.demod(ci);
        m_gsc.process(fm);

        while (m_gsc.hasMessage()) {
            sendGsc(m_gsc.takeMessage());
        }
        if (m_gsc.takeLevelsMeasured(dc)) {
            legMeasured(m_gscLeg, ProtocolGsc, dc);
        }
        if (!m_gsc.locked()) {
            legReleased(m_gscLeg, ProtocolGsc, false);
        }
        if (m_gscLeg.takeHandover(delta)) {
            m_gsc.offsetLevels(delta);
        }

        if (m_settings.m_gscAudio || m_settings.m_gscVoiceRecord) {
            processAudio(fm);
        }
    }

    if (++m_statusCount >= statusPeriodSamples)
    {
        m_statusCount = 0;
        updateStatus();

        if (++m_afcCount >= 10)
        {
            m_afcCount = 0;
            decayHuntCentres();
        }
    }
}

void RadioPagingDemodSink::applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force)
{
    qDebug() << "RadioPagingDemodSink::applyChannelSettings:"
            << " channelSampleRate: " << channelSampleRate
            << " channelFrequencyOffset: " << channelFrequencyOffset;

    if ((m_channelFrequencyOffset != channelFrequencyOffset) ||
        (m_channelSampleRate != channelSampleRate) || force)
    {
        m_nco.setFreq(-channelFrequencyOffset, channelSampleRate);
    }

    if ((m_channelSampleRate != channelSampleRate) || force)
    {
        m_interpolator.create(16, channelSampleRate, m_settings.m_rfBandwidth / 2.2);
        m_interpolatorDistance = (Real) channelSampleRate / (Real) RadioPagingDemodSettings::m_channelSampleRate;
        m_interpolatorDistanceRemain = m_interpolatorDistance;
    }

    m_channelSampleRate = channelSampleRate;
    m_channelFrequencyOffset = channelFrequencyOffset;
}

void RadioPagingDemodSink::applySettings(const QStringList& settingsKeys, const RadioPagingDemodSettings& settings, bool force)
{
    qDebug() << "RadioPagingDemodSink::applySettings:" << settings.getDebugString(settingsKeys, force);

    if ((settingsKeys.contains("rfBandwidth") && (settings.m_rfBandwidth != m_settings.m_rfBandwidth)) || force)
    {
        m_interpolator.create(16, m_channelSampleRate, settings.m_rfBandwidth / 2.2);
        m_interpolatorDistance = (Real) m_channelSampleRate / (Real) RadioPagingDemodSettings::m_channelSampleRate;
        m_interpolatorDistanceRemain = m_interpolatorDistance;
    }

    bool legs = settingsKeys.contains("rfBandwidth")
        || settingsKeys.contains("pocsagDeviation") || settingsKeys.contains("flexDeviation")
        || settingsKeys.contains("gscDeviation") || force;

    if (force) {
        m_settings = settings;
    } else {
        m_settings.applySettings(settingsKeys, settings);
    }

    if (legs) {
        applyLegs();
    }

    // A protocol switched off starts from scratch when it is switched on again
    if (settingsKeys.contains("pocsagEnabled") && !m_settings.m_pocsagEnabled)
    {
        for (auto& pocsag : m_pocsag) {
            pocsag.reset();
        }
    }
    if (settingsKeys.contains("flexEnabled") && !m_settings.m_flexEnabled) {
        m_flex.reset();
    }
    if ((settingsKeys.contains("gscAudio") && !m_settings.m_gscAudio && !m_settings.m_gscVoiceRecord)
        || (settingsKeys.contains("gscVoiceRecord") && !m_settings.m_gscVoiceRecord && !m_settings.m_gscAudio)
        || (settingsKeys.contains("gscEnabled") && !m_settings.m_gscEnabled)) {
        closeAudio();
    }
    if (settingsKeys.contains("gscVoiceRecord") && !m_settings.m_gscVoiceRecord) {
        finishVoiceRecording();
    }
    if (settingsKeys.contains("gscVoiceRecord") && m_settings.m_gscVoiceRecord && m_audioOpen && !m_voiceRecorder) {
        startVoiceRecording();
    }
    if (settingsKeys.contains("gscEnabled") && !m_settings.m_gscEnabled)
    {
        m_gsc.reset();
        m_gscVoiceId[0].clear();
        m_gscVoiceId[1].clear();
    }
    if (settingsKeys.contains("gscVoiceTimeout") || force) {
        m_gsc.setVoiceTimeout(m_settings.m_gscVoiceTimeout);
    }
}

void RadioPagingDemodSink::applyAudioSampleRate(int sampleRate)
{
    qDebug("RadioPagingDemodSink::applyAudioSampleRate: %d", sampleRate);

    if (sampleRate <= 0) {
        return;
    }

    closeAudio();
    m_audioFifo.setSize(sampleRate);
    m_audioDistance = (Real) RadioPagingDemodSettings::m_channelSampleRate / (Real) sampleRate;
    m_audioDistanceRemain = m_audioDistance;
    m_audioSampleRate = sampleRate;
}

// The audio opens when the alert time of a voice page is over, as a pager opens its audio
// after the alert, and closes when the page ends: turn-off word, new transmission, signal
// lost or the voice timeout (the longest voice message)
void RadioPagingDemodSink::processAudio(Real fm)
{
    if (!m_gsc.inVoiceMessage())
    {
        closeAudio();
        return;
    }

    if (!m_audioOpen)
    {
        m_audioOpen = true;
        m_audioDistanceRemain = m_audioDistance;
        m_dcIn = fm;
        m_dcOut = 0.0f;

        if (m_settings.m_gscVoiceRecord) {
            startVoiceRecording();
        }
    }

    Real y = fm - m_dcIn + dcBlockPole * m_dcOut;
    m_dcIn = fm;
    m_dcOut = y;

    Complex c(y, 0.0f);
    Complex out;

    if (m_audioDistance < 1.0f)
    {
        while (!m_audioInterpolator.interpolate(&m_audioDistanceRemain, c, &out))
        {
            pushAudio(out.real());
            recordAudio(out.real());
            m_audioDistanceRemain += m_audioDistance;
        }
    }
    else if (m_audioInterpolator.decimate(&m_audioDistanceRemain, c, &out))
    {
        pushAudio(out.real());
        recordAudio(out.real());
        m_audioDistanceRemain += m_audioDistance;
    }
}

// A file per voice message, named by the time it started. The voice is not scaled: the
// discriminator gives +/-1 at the GSC deviation, as the transmitter takes the samples of a
// sound file. Without a directory to write to nothing is recorded
void RadioPagingDemodSink::startVoiceRecording()
{
    QString directory = m_settings.voiceDirectory();

    m_voiceRecorder.reset();
    m_voiceFile.clear();
    m_voiceFileDone.clear();
    m_voiceBuffer.clear();

    if (!QDir().mkpath(directory))
    {
        qWarning() << "RadioPagingDemodSink::startVoiceRecording: cannot create" << directory;
        return;
    }

    // A disk that fills up is not written to
    QStorageInfo storage(directory);

    if (storage.isValid() && (storage.bytesAvailable() < voiceMinFreeBytes))
    {
        qWarning() << "RadioPagingDemodSink::startVoiceRecording: not enough free space in" << directory;
        return;
    }

    std::unique_ptr<WavFileRecord> recorder(new WavFileRecord((quint32) m_audioSampleRate));
    recorder->setMono(true);
    // Named after the alias of the demodulator, by default its frequency
    recorder->setFileName(RadioPagingVoiceStore::filePrefix(directory,
        m_settings.effectiveChannelAlias(m_centerFrequency + m_channelFrequencyOffset), "rx"));

    if (!recorder->startRecording())
    {
        qWarning() << "RadioPagingDemodSink::startVoiceRecording: cannot write in" << directory;
        return;
    }

    m_voiceFile = recorder->getCurrentFileName();
    m_voiceBuffer.reserve(voiceWriteSamples);
    m_voiceRecorder = std::move(recorder);
}

void RadioPagingDemodSink::recordAudio(Real sample)
{
    if (!m_voiceRecorder) {
        return;
    }

    Real v = sample * std::numeric_limits<int16_t>::max();
    m_voiceBuffer.push_back((qint16) std::max<Real>(std::numeric_limits<int16_t>::min(),
        std::min<Real>(std::numeric_limits<int16_t>::max(), v)));

    // Written in blocks, not sample by sample
    if (m_voiceBuffer.size() >= voiceWriteSamples)
    {
        m_voiceRecorder->writeMono(m_voiceBuffer.data(), (int) m_voiceBuffer.size());
        m_voiceBuffer.clear();
    }
}

// The file of the voice message is kept for the record of its end, which can come after the
// audio closed (the record is sent by the receiver, the audio follows its state)
QString RadioPagingDemodSink::finishVoiceRecording()
{
    if (!m_voiceRecorder) {
        return QString();
    }

    if (!m_voiceBuffer.empty())
    {
        m_voiceRecorder->writeMono(m_voiceBuffer.data(), (int) m_voiceBuffer.size());
        m_voiceBuffer.clear();
    }

    m_voiceRecorder->stopRecording();
    m_voiceRecorder.reset();
    m_voiceFileDone = m_voiceFile;
    m_voiceFile.clear();

    // The old files go a few at a time, when a new one is complete
    RadioPagingVoiceStore::cleanup(m_settings.voiceDirectory(), m_settings.m_gscVoiceKeepDays, m_settings.m_gscVoiceKeepMin,
        RadioPagingVoiceStore::m_autoCleanupBatch, m_voiceFileDone);

    return m_voiceFileDone;
}

void RadioPagingDemodSink::pushAudio(Real sample)
{
    if (!m_settings.m_gscAudio) {
        return;
    }

    // The discriminator gives +/-1 at the GSC deviation
    Real v = sample * m_settings.m_volume * std::numeric_limits<int16_t>::max();
    qint16 s = (qint16) std::max<Real>(std::numeric_limits<int16_t>::min(), std::min<Real>(std::numeric_limits<int16_t>::max(), v));

    m_audioBuffer[m_audioBufferFill].l = s;
    m_audioBuffer[m_audioBufferFill].r = s;

    if (++m_audioBufferFill >= m_audioBuffer.size())
    {
        m_audioFifo.write((const quint8*) &m_audioBuffer[0], m_audioBufferFill);
        m_audioBufferFill = 0;
    }
}

// The rest of the voice is output, then nothing until the next voice page
void RadioPagingDemodSink::closeAudio()
{
    if (!m_audioOpen) {
        return;
    }

    if (m_audioBufferFill > 0)
    {
        m_audioFifo.write((const quint8*) &m_audioBuffer[0], m_audioBufferFill);
        m_audioBufferFill = 0;
    }

    m_audioOpen = false;
    finishVoiceRecording();
}

// The filters are rebuilt when the bandwidth or a deviation changes. All POCSAG speeds
// share one leg sized to the fastest
void RadioPagingDemodSink::applyLegs()
{
    double channel = m_settings.m_rfBandwidth;

    // Following the carrier, the legs hunt at the channel width; otherwise at Carson and the
    // offset room
    bool afc = RadioPagingDemodSettings::m_legAfc;

    m_pocsagLeg.create(afc ? channel : m_settings.pocsagBandwidth(), m_settings.pocsagCarson(), channel, m_settings.m_pocsagDeviation);
    m_flexLeg.create(afc ? channel : m_settings.flexBandwidth(), m_settings.flexCarson(), channel, m_settings.m_flexDeviation);
    // GSC receives with the offset room: one measurement may place its carrier that far off
    m_gscLeg.create(afc ? channel : m_settings.gscBandwidth(), afc ? m_settings.gscBandwidth() : m_settings.gscCarson(),
        channel, m_settings.m_gscDeviation);

    qDebug() << "RadioPagingDemodSink::applyLegs: hunting / receiving"
        << "POCSAG" << m_pocsagLeg.m_huntBandwidth << m_pocsagLeg.m_trackBandwidth
        << "FLEX" << m_flexLeg.m_huntBandwidth << m_flexLeg.m_trackBandwidth
        << "GSC" << m_gscLeg.m_huntBandwidth << m_gscLeg.m_trackBandwidth;
}

RadioPagingDemodSink::Status RadioPagingDemodSink::getStatus() const
{
    QMutexLocker mutexLocker(&m_statusMutex);
    return m_status;
}

void RadioPagingDemodSink::updateStatus()
{
    Status status;

    for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++)
    {
        RadioPagingPocsagReceiver::Status s = m_pocsag[i].getStatus();
        bool normal = s.m_normal.m_state != RadioPagingPocsagDecoder::Hunting;
        bool inverted = s.m_inverted.m_state != RadioPagingPocsagDecoder::Hunting;

        status.m_pocsagSync[i] = normal || inverted;
        status.m_pocsagInverted[i] = inverted && !normal;

        // The BER of a synchronised decoder is the live one; the last one is kept after
        if (normal || inverted)
        {
            const RadioPagingBerWindow& ber = inverted && !normal ? s.m_inverted.m_ber : s.m_normal.m_ber;
            if (ber.valid()) {
                m_lastBer[0] = ber;
            }
            // The carrier offset: the correction of the leg and what is left at the
            // discriminator (+/-1 at the deviation)
            m_lastOffset[0] = m_pocsagLeg.m_shift + s.m_dc * m_settings.m_pocsagDeviation;
            m_lastOffsetValid[0] = true;
            m_lastPocsagBatches = inverted && !normal ? s.m_inverted.m_batches : s.m_normal.m_batches;
        }
    }

    bool pocsagSync = status.m_pocsagSync[0] || status.m_pocsagSync[1] || status.m_pocsagSync[2];

    RadioPagingFlexReceiver::Status flex = m_flex.getStatus();
    status.m_flexSync = flex.m_state != RadioPagingFlexReceiver::Hunting;
    status.m_flexMode = RadioPagingFlex::modeName(flex.m_mode);
    status.m_flexInverted = flex.m_inverted;
    status.m_flexQuality = flex.m_quality;
    status.m_flexErs = flex.m_ers;
    status.m_flexErsSeconds = flex.m_ersSeconds;
    status.m_flexErsCount = flex.m_ersDetections;
    if (status.m_flexSync && flex.m_ber.valid()) {
        m_lastBer[1] = flex.m_ber;
    }
    if (status.m_flexSync)
    {
        m_lastOffset[1] = m_flexLeg.m_shift + flex.m_dc * m_settings.m_flexDeviation;
        m_lastOffsetValid[1] = true;
    }
    status.m_flexFrame = flex.m_frameReport;
    status.m_flexTime = flex.m_networkTime;
    status.m_flexGroups = flex.m_groups;
    status.m_flexReassemblies = flex.m_reassemblies;
    status.m_pocsagBatches = m_lastPocsagBatches;
    status.m_gscPreamble = m_lastGscPreamble;
    status.m_gscBatch = m_lastGscBatch;
    status.m_gscNbs = m_lastGscNbs;
    status.m_gscSlot = m_lastGscSlot;

    RadioPagingGscReceiver::Status gsc = m_gsc.getStatus();
    bool gscNormal = gsc.m_normal.m_state != RadioPagingGscDecoder::Hunting;
    bool gscInverted = gsc.m_inverted.m_state != RadioPagingGscDecoder::Hunting;
    status.m_gscSync = gscNormal || gscInverted;
    if (status.m_gscSync)
    {
        const RadioPagingBerWindow& ber = gscInverted && !gscNormal ? gsc.m_inverted.m_ber : gsc.m_normal.m_ber;
        if (ber.valid()) {
            m_lastBer[2] = ber;
        }
        m_lastOffset[2] = m_gscLeg.m_shift + gsc.m_dc * m_settings.m_gscDeviation;
        m_lastOffsetValid[2] = true;
    }

    bool sync[3] = { pocsagSync, status.m_flexSync, status.m_gscSync };

    for (int p = 0; p < 3; p++)
    {
        status.m_ber[p] = m_lastBer[p];
        status.m_berLive[p] = sync[p] && m_lastBer[p].valid();
        status.m_offset[p] = m_lastOffset[p];
        status.m_offsetValid[p] = m_lastOffsetValid[p];
    }
    status.m_gscVoice = (gsc.m_normal.m_state == RadioPagingGscDecoder::Voice) || (gsc.m_inverted.m_state == RadioPagingGscDecoder::Voice);
    status.m_gscInverted = gscInverted && !gscNormal;
    status.m_gscAudio = m_audioOpen;

    status.m_enabled[0] = m_settings.m_pocsagEnabled;
    status.m_enabled[1] = m_settings.m_flexEnabled;
    status.m_enabled[2] = m_settings.m_gscEnabled;

    QMutexLocker mutexLocker(&m_statusMutex);
    std::copy(m_status.m_messages, m_status.m_messages + 3, status.m_messages);
    m_status = status;
}

void RadioPagingDemodSink::sendRecord(MessageRecord& record, int protocol)
{
    record.m_direction = MessageRecord::DirectionRX;
    record.m_kind = MessageRecord::KindMessage;
    if (record.m_id.isNull()) {
        record.m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    record.m_timestamp = QDateTime::currentMSecsSinceEpoch();
    record.m_protocolData.insert("interpretedBy", "decoder");

    {
        QMutexLocker mutexLocker(&m_statusMutex);
        m_status.m_messages[protocol]++;
    }

    if (m_messageQueueToChannel) {
        m_messageQueueToChannel->push(MainCore::MsgMessageRecord::create(m_channel, record));
    }
}

void RadioPagingDemodSink::sendPocsag(const RadioPagingPocsagDecoder::Message& message, int baud)
{
    MessageRecord record;
    RadioPagingPocsag::Interpretation interpretation = RadioPagingPocsag::interpret(message.m_rawData, message.m_rawBits);

    record.m_protocol = "pocsag";
    record.m_mode = QString::number(baud);
    record.m_speed = baud;
    // RIC and function (sub-address A-D): the address of the pager
    record.m_address = QString("%1%2").arg(message.m_address, 7, 10, QChar('0')).arg(QChar('A' + (message.m_function & 3)));
    record.m_polarity = polarity(message.m_polarity == RadioPagingPocsagDecoder::Inverted);
    record.m_msgType = QString::fromStdString(interpretation.m_type);
    if (message.m_rawBits > 0) {
        record.m_text = escapeText(interpretation.m_text);
    }
    record.m_rawData = toByteArray(message.m_rawData);
    record.m_rawBits = message.m_rawBits;
    record.m_errorsCorrected = message.m_errorsCorrected;
    record.m_errorsUncorrectable = message.m_errorsUncorrectable;
    record.m_protocolData.insert("function", message.m_function);
    record.m_protocolData.insert("batch", message.m_batch);
    record.m_protocolData.insert("truncated", message.m_truncated);
    record.m_protocolData.insert("score", interpretation.m_score);
    record.m_protocolData.insert("alt_type", QString::fromStdString(interpretation.m_altType));
    record.m_protocolData.insert("alt_text", escapeText(interpretation.m_altText));
    record.m_protocolData.insert("alt_score", interpretation.m_altScore);
    record.m_duplicate = message.m_duplicate ? MessageRecord::DuplicateDup : MessageRecord::DuplicateNone;

    sendRecord(record, ProtocolPocsag);
}

void RadioPagingDemodSink::sendFlex(const RadioPagingFlexDecoder::Message& message)
{
    MessageRecord record;

    record.m_protocol = "flex";
    record.m_mode = RadioPagingFlex::modeName(message.m_mode);
    record.m_speed = message.m_bitRate;
    record.m_address = QString::fromStdString(message.m_address);
    record.m_polarity = polarity(message.m_inverted);
    record.m_msgType = QString::fromStdString(message.m_typeName);
    if (!message.m_text.empty()) {
        record.m_text = escapeText(message.m_text);
    }
    record.m_rawData = toByteArray(message.m_rawData);
    record.m_rawBits = message.m_rawBits;
    record.m_errorsCorrected = message.m_bitErrors;
    record.m_errorsUncorrectable = message.m_wordsUncorrectable;
    record.m_protocolData.insert("capcode", (qulonglong) message.m_capcode);
    record.m_protocolData.insert("long_address", message.m_longAddress);
    record.m_protocolData.insert("priority", message.m_priority);
    record.m_protocolData.insert("cycle", message.m_cycle);
    record.m_protocolData.insert("frame", message.m_frame);
    record.m_protocolData.insert("phase", QString(QChar(message.m_phase)));
    record.m_protocolData.insert("message_number", message.m_messageNumber);
    record.m_protocolData.insert("fragmented", message.m_fragmented);
    record.m_protocolData.insert("fragment_count", message.m_fragmentCount);
    record.m_protocolData.insert("complete", message.m_complete);
    record.m_protocolData.insert("kanji", message.m_kanji);
    record.m_protocolData.insert("word_map", QString::fromStdString(message.m_wordMap));
    record.m_protocolData.insert("words_corrected", message.m_wordsCorrected);
    record.m_protocolData.insert("quality", message.m_quality);
    record.m_protocolData.insert("addr_type", RadioPagingFlexDecoder::addressTypeName(message.m_addressType));
    record.m_protocolData.insert("maildrop", message.m_mailDrop);
    record.m_protocolData.insert("retrieval", message.m_retrieval);
    record.m_protocolData.insert("checksum", checkName(message.m_checksum));
    record.m_protocolData.insert("signature", checkName(message.m_signature));
    record.m_protocolData.insert("is_fragment", message.m_isFragment);
    record.m_protocolData.insert("fragment_gap", message.m_fragmentGap);

    if (message.m_tempSlot >= 0)
    {
        QVariantList members;

        for (uint64_t capcode : message.m_groupMembers) {
            members.append(QString("%1").arg((qulonglong) capcode, 10, 10, QChar('0')));
        }

        record.m_protocolData.insert("group_slot", message.m_tempSlot);
        record.m_protocolData.insert("group_tracked", message.m_groupTracked);
        record.m_protocolData.insert("group_capcodes", members);
    }

    if (message.m_type == RadioPagingFlexDecoder::TypeShortInstruction)
    {
        record.m_protocolData.insert("instruction_type", message.m_instructionType);
        record.m_protocolData.insert("instruction_slot", message.m_instructionSlot);
        record.m_protocolData.insert("instruction_frame", message.m_instructionFrame);
    }

    record.m_duplicate = !message.m_duplicate ? MessageRecord::DuplicateNone
        : message.m_recovered ? MessageRecord::DuplicateDupPlus : MessageRecord::DuplicateDup;

    sendRecord(record, ProtocolFlex);
}

void RadioPagingDemodSink::sendGsc(const RadioPagingGscDecoder::Message& message)
{
    MessageRecord record;
    int pol = (message.m_polarity == RadioPagingGscDecoder::Inverted) ? 1 : 0;

    // Capcode and function (address number 1-4); the page type is in the message type
    record.m_protocol = "gsc";
    record.m_mode = "600";
    record.m_speed = 600;
    record.m_address = QString::fromStdString(message.m_address) + QChar('0' + message.m_addressNumber);
    record.m_polarity = polarity(pol == 1);
    record.m_rawData = toByteArray(message.m_rawData);
    record.m_rawBits = message.m_rawBits;
    record.m_errorsCorrected = message.m_errorsCorrected;
    record.m_errorsUncorrectable = message.m_errorsUncorrectable;
    record.m_protocolData.insert("address_number", message.m_addressNumber);
    record.m_protocolData.insert("preamble", message.m_preamble);
    record.m_protocolData.insert("batch", message.m_batch);
    record.m_protocolData.insert("slot", message.m_slot);
    record.m_protocolData.insert("non_battery_saver", message.m_nonBatterySaver);
    record.m_protocolData.insert("truncated", message.m_truncated);
    record.m_protocolData.insert("function", message.m_addressNumber);

    m_lastGscPreamble = message.m_preamble;
    m_lastGscBatch = message.m_batch;
    m_lastGscNbs = message.m_nonBatterySaver;
    m_lastGscSlot = message.m_slot;
    record.m_duplicate = message.m_duplicate ? MessageRecord::DuplicateDup : MessageRecord::DuplicateNone;

    if (message.m_type == RadioPagingGsc::Data)
    {
        RadioPagingGsc::Interpretation interpretation = RadioPagingGsc::interpret(message.m_rawData, message.m_rawBits);

        record.m_msgType = QString::fromStdString(interpretation.m_type);
        record.m_text = escapeText(interpretation.m_text);
        record.m_protocolData.insert("checksum_errors", message.m_checksumErrors);
        record.m_protocolData.insert("checksum_passed", message.m_checksumPassed);
        record.m_protocolData.insert("score", interpretation.m_score);
        record.m_protocolData.insert("alt_type", QString::fromStdString(interpretation.m_altType));
        record.m_protocolData.insert("alt_text", escapeText(interpretation.m_altText));
        record.m_protocolData.insert("alt_score", interpretation.m_altScore);
    }
    else if (message.m_type == RadioPagingGsc::Voice)
    {
        static const char *ends[] = { "", "turn_off", "new_transmission", "timeout", "signal_lost" };

        record.m_msgType = "voice";

        if (message.m_update)
        {
            // The end of the voice message: an update of the voice page record
            record.m_id = m_gscVoiceId[pol];
            m_gscVoiceId[pol].clear();
            record.m_protocolData.insert("voice_end", ends[(int) message.m_voiceEnd]);
            record.m_protocolData.insert("voice_seconds", message.m_voiceSeconds);

            // The file is closed before the record goes out, the message log plays it from there
            finishVoiceRecording();

            if (!m_voiceFileDone.isEmpty())
            {
                record.m_protocolData.insert("voice_file", m_voiceFileDone);
                m_voiceFileDone.clear();
            }
        }
        else
        {
            // An update follows when the voice message ends
            record.m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            record.m_partial = true;
            m_gscVoiceId[pol] = record.m_id;
        }
    }
    else
    {
        record.m_msgType = "tone_only";
        record.m_protocolData.insert("interleaved", message.m_interleaved);
    }

    sendRecord(record, ProtocolGsc);
}

QString RadioPagingDemodSink::checkName(int check)
{
    switch (check)
    {
    case RadioPagingFlexDecoder::CheckOk: return "ok";
    case RadioPagingFlexDecoder::CheckFailed: return "failed";
    case RadioPagingFlexDecoder::CheckIncomplete: return "incomplete";
    default: return "";
    }
}

MessageRecord::Polarity RadioPagingDemodSink::polarity(bool inverted)
{
    return inverted ? MessageRecord::PolarityInverted : MessageRecord::PolarityNormal;
}

QString RadioPagingDemodSink::escapeText(const std::string& text)
{
    std::string out;

    for (char c : text)
    {
        unsigned char u = (unsigned char) c;

        if ((u < 0x20) || (u == 0x7f))
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "<0x%02X>", u);
            out += buf;
        }
        else
        {
            out.push_back(c);
        }
    }

    return QString::fromUtf8(out.c_str(), (int) out.size());
}

QByteArray RadioPagingDemodSink::toByteArray(const std::vector<uint8_t>& data)
{
    return QByteArray((const char *) data.data(), (int) data.size());
}
