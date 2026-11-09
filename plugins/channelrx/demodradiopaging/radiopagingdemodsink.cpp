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
#include <QUuid>

#include "dsp/dspengine.h"
#include "channel/channelapi.h"
#include "util/messagequeue.h"
#include "maincore.h"

#include "radiopagingpocsag.h"
#include "radiopagingflex.h"
#include "radiopaginggsc.h"

#include "radiopagingdemodsink.h"

namespace {

const int legFilterTaps = 65;
const int statusPeriodSamples = RadioPagingDemodSettings::m_channelSampleRate / 10;   // 100 ms

enum Protocol {
    ProtocolPocsag,
    ProtocolFlex,
    ProtocolGsc
};

} // namespace

void RadioPagingDemodSink::Leg::create(double bandwidth, double channelBandwidth, double deviation)
{
    m_filtered = bandwidth < channelBandwidth;

    if (m_filtered) {
        m_filter.create(legFilterTaps, RadioPagingDemodSettings::m_channelSampleRate, bandwidth / 2.0);
    }

    // Outer deviation gives +/-1
    m_discri.setFMScaling(RadioPagingDemodSettings::m_channelSampleRate / (2.0 * deviation));
    m_discri.reset();
}

Real RadioPagingDemodSink::Leg::demod(const Complex& sample)
{
    double magsq;
    Real fmDev;
    Complex c = m_filtered ? m_filter.filter(sample) : sample;

    return m_discri.phaseDiscriminatorDelta(c, magsq, fmDev);
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
    m_statusCount(0)
{
    for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++) {
        m_pocsag[i].create(RadioPagingDemodSettings::m_channelSampleRate, RadioPagingDemodSettings::m_pocsagSpeeds[i]);
    }
    m_flex.create(RadioPagingDemodSettings::m_channelSampleRate);
    m_gsc.create(RadioPagingDemodSettings::m_channelSampleRate);

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

    if (m_settings.pocsagEnabled())
    {
        Real fm = m_pocsagLeg.demod(ci);

        for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++)
        {
            if (!m_settings.m_pocsagSpeedEnabled[i]) {
                continue;
            }

            m_pocsag[i].process(fm);

            while (m_pocsag[i].hasMessage()) {
                sendPocsag(m_pocsag[i].takeMessage(), RadioPagingDemodSettings::m_pocsagSpeeds[i]);
            }
        }
    }

    if (m_settings.flexEnabled())
    {
        m_flex.process(m_flexLeg.demod(ci), magsq);

        while (m_flex.hasMessage()) {
            sendFlex(m_flex.takeMessage());
        }
    }

    if (m_settings.m_gscEnabled)
    {
        m_gsc.process(m_gscLeg.demod(ci));

        while (m_gsc.hasMessage()) {
            sendGsc(m_gsc.takeMessage());
        }
    }

    if (++m_statusCount >= statusPeriodSamples)
    {
        m_statusCount = 0;
        updateStatus();
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

    bool polarities = settingsKeys.contains("normalPolarity") || settingsKeys.contains("invertedPolarity") || force;
    bool legs = settingsKeys.contains("rfBandwidth") || settingsKeys.contains("pocsagSpeedEnabled")
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

    if (polarities)
    {
        for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++) {
            m_pocsag[i].setPolarities(m_settings.m_normalPolarity, m_settings.m_invertedPolarity);
        }
        m_flex.setPolarities(m_settings.m_normalPolarity, m_settings.m_invertedPolarity);
        m_gsc.setPolarities(m_settings.m_normalPolarity, m_settings.m_invertedPolarity);
    }

    if (settingsKeys.contains("pocsagSpeedEnabled") || force)
    {
        for (int i = 0; i < RadioPagingDemodSettings::m_nbPocsagSpeeds; i++)
        {
            if (!m_settings.m_pocsagSpeedEnabled[i]) {
                m_pocsag[i].reset();
            }
        }
    }

    if (settingsKeys.contains("flexModeEnabled") || force)
    {
        for (int i = 0; i < RadioPagingDemodSettings::m_nbFlexModes; i++) {
            m_flex.setModeEnabled((RadioPagingFlex::Mode) i, m_settings.m_flexModeEnabled[i]);
        }
        if (!m_settings.flexEnabled()) {
            m_flex.reset();
        }
    }

    if (settingsKeys.contains("gscEnabled") || force)
    {
        if (!m_settings.m_gscEnabled)
        {
            m_gsc.reset();
            m_gscVoiceId[0].clear();
            m_gscVoiceId[1].clear();
        }
    }

    if (settingsKeys.contains("gscNonBatterySaver") || force) {
        m_gsc.setNonBatterySaver(m_settings.m_gscNonBatterySaver);
    }
    if (settingsKeys.contains("gscVoiceTimeout") || force) {
        m_gsc.setVoiceTimeout(m_settings.m_gscVoiceTimeout);
    }
}

// The filters are rebuilt when the bandwidth changes: the POCSAG filter follows the fastest
// enabled speed (OI-5a)
void RadioPagingDemodSink::applyLegs()
{
    double channel = m_settings.m_rfBandwidth;

    m_pocsagLeg.create(m_settings.pocsagBandwidth(), channel, m_settings.m_pocsagDeviation);
    m_flexLeg.create(m_settings.flexBandwidth(), channel, m_settings.m_flexDeviation);
    m_gscLeg.create(m_settings.gscBandwidth(), channel, m_settings.m_gscDeviation);

    qDebug() << "RadioPagingDemodSink::applyLegs:"
        << "POCSAG" << m_settings.pocsagBandwidth() << (m_pocsagLeg.m_filtered ? "filtered" : "channel filter")
        << "FLEX" << m_settings.flexBandwidth() << (m_flexLeg.m_filtered ? "filtered" : "channel filter")
        << "GSC" << m_settings.gscBandwidth() << (m_gscLeg.m_filtered ? "filtered" : "channel filter");
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

        status.m_pocsagEnabled[i] = m_settings.m_pocsagSpeedEnabled[i];
        status.m_pocsagSync[i] = status.m_pocsagEnabled[i] && (normal || inverted);
        status.m_pocsagInverted[i] = inverted && !normal;
    }

    RadioPagingFlexReceiver::Status flex = m_flex.getStatus();
    status.m_flexEnabled = m_settings.flexEnabled();
    status.m_flexSync = status.m_flexEnabled && (flex.m_state != RadioPagingFlexReceiver::Hunting);
    status.m_flexMode = RadioPagingFlex::modeName(flex.m_mode);
    status.m_flexInverted = flex.m_inverted;
    status.m_flexQuality = flex.m_quality;

    RadioPagingGscReceiver::Status gsc = m_gsc.getStatus();
    bool gscNormal = gsc.m_normal.m_state != RadioPagingGscDecoder::Hunting;
    bool gscInverted = gsc.m_inverted.m_state != RadioPagingGscDecoder::Hunting;
    status.m_gscEnabled = m_settings.m_gscEnabled;
    status.m_gscSync = status.m_gscEnabled && (gscNormal || gscInverted);
    status.m_gscVoice = (gsc.m_normal.m_state == RadioPagingGscDecoder::Voice) || (gsc.m_inverted.m_state == RadioPagingGscDecoder::Voice);
    status.m_gscInverted = gscInverted && !gscNormal;

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
    record.m_address = QString("%1").arg(message.m_address, 7, 10, QChar('0'));
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
    record.m_partial = message.m_isFragment;
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

    sendRecord(record, ProtocolFlex);
}

void RadioPagingDemodSink::sendGsc(const RadioPagingGscDecoder::Message& message)
{
    MessageRecord record;
    int pol = (message.m_polarity == RadioPagingGscDecoder::Inverted) ? 1 : 0;
    RadioPagingGsc::MessageType suffixType = (message.m_type == RadioPagingGsc::Data) ? RadioPagingGsc::Alphanumeric : message.m_type;

    record.m_protocol = "gsc";
    record.m_mode = "600";
    record.m_speed = 600;
    record.m_address = QString::fromStdString(message.m_address) + QChar(RadioPagingGsc::suffixDigit(suffixType, message.m_addressNumber));
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
