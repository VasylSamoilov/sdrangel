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

#ifndef INCLUDE_RADIOPAGINGDEMODSINK_H
#define INCLUDE_RADIOPAGINGDEMODSINK_H

#include <QMutex>
#include <QString>

#include <memory>
#include <vector>

#include "dsp/channelsamplesink.h"
#include "dsp/wavfilerecord.h"
#include "dsp/phasediscri.h"
#include "dsp/nco.h"
#include "dsp/interpolator.h"
#include "dsp/firfilter.h"
#include "audio/audiofifo.h"
#include "util/movingaverage.h"
#include "util/messagerecord.h"

#include "radiopagingpocsagreceiver.h"
#include "radiopagingflexreceiver.h"
#include "radiopaginggscreceiver.h"
#include "radiopagingtonedetector.h"
#include "radiopagingcharset.h"

#include "radiopagingdemodsettings.h"

class ChannelAPI;
class MessageQueue;

// Channel IQ at 38400 Hz is shared by one leg per protocol. Each leg has its own filter at
// the protocol's Carson bandwidth (skipped when not narrower than the channel filter) and
// its own FM discriminator scaled to the protocol's deviation, and feeds the receivers of
// all its speeds, each with both polarities. Nothing is shared between the legs: every
// receiver hunts for its own sync. Decoded messages are sent to the channel as records
class RadioPagingDemodSink : public ChannelSampleSink {
public:
    struct Status
    {
        bool m_enabled[3];              //!< POCSAG, FLEX, GSC decoding on
        RadioPagingBerWindow m_ber[3];  //!< Of the reception in progress, or the last one
        bool m_berLive[3];              //!< m_ber is of a reception in progress
        float m_offset[3];              //!< Carrier frequency offset (DC offset) of the same reception, Hz
        bool m_offsetValid[3];          //!< m_offset was measured
        int m_pocsagBatches;            //!< Batches of the POCSAG transmission in progress (or last)
        // FLEX: the network of the last frame
        RadioPagingFlexDecoder::FrameReport m_flexFrame;
        RadioPagingFlexDecoder::NetworkTime m_flexTime;
        RadioPagingBerWindow m_flexPhaseBer[4];     //!< Of each phase A to D, of the recent frames
        std::vector<RadioPagingFlexDecoder::GroupSlot> m_flexGroups;
        int m_flexReassemblies;
        // GSC: the last page received
        int m_gscPreamble;              //!< Preamble value index, -1 non battery saver or none yet
        bool m_gscBatch;
        bool m_gscNbs;
        int m_gscSlot;
        bool m_pocsagSync[RadioPagingDemodSettings::m_nbPocsagSpeeds];      //!< A decoder of the speed is synchronised
        bool m_pocsagInverted[RadioPagingDemodSettings::m_nbPocsagSpeeds];  //!< ... and it is the inverted one
        bool m_flexSync;
        QString m_flexMode;
        bool m_flexInverted;
        float m_flexQuality;
        bool m_flexErs;                 //!< Emergency re-synchronization being received
        float m_flexErsSeconds;         //!< Length of the current or last one
        quint32 m_flexErsCount;         //!< Re-synchronizations received
        bool m_gscSync;
        bool m_gscVoice;
        bool m_gscAudio;                //!< The voice message is being played
        bool m_gscInverted;
        quint32 m_messages[3];          //!< POCSAG, FLEX, GSC records sent since start
        int m_remembered[3];            //!< Messages remembered for duplicate detection
        qint64 m_resetTime[3];          //!< When the state of the protocol was last reset, ms since epoch

        //! The network of one polarity of a protocol
        struct Polarity
        {
            bool m_seen = false;        //!< Synchronised or a message received on the polarity since the last reset
            bool m_sync = false;
            RadioPagingBerWindow m_ber; //!< Of the reception in progress, or the last one
            bool m_berLive = false;
            float m_offset = 0.0f;      //!< Carrier frequency offset of the same reception, Hz
            bool m_offsetValid = false;
            quint32 m_messages = 0;     //!< Records sent since start
            int m_remembered = 0;       //!< Messages remembered for duplicate detection
        };
        Polarity m_pol[3][2];           //!< POCSAG, FLEX, GSC; normal, inverted
        bool m_pocsagSpeedSync[2][RadioPagingDemodSettings::m_nbPocsagSpeeds];     //!< By polarity, a decoder of the speed is synchronised
        // FLEX: the network of each polarity, of its last frame
        QString m_flexModePol[2];
        RadioPagingFlexDecoder::FrameReport m_flexFramePol[2];
        RadioPagingFlexDecoder::NetworkTime m_flexTimePol[2];
        RadioPagingBerWindow m_flexPhaseBerPol[2][4];
        std::vector<RadioPagingFlexDecoder::GroupSlot> m_flexGroupsPol[2];
        int m_flexReassembliesPol[2];
        bool m_gscVoicePol[2];
    };

    RadioPagingDemodSink();
    ~RadioPagingDemodSink();

    virtual void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end);

    void applyChannelSettings(int channelSampleRate, int channelFrequencyOffset, bool force = false);
    void applySettings(const QStringList& settingsKeys, const RadioPagingDemodSettings& settings, bool force = false);
    void setMessageQueueToChannel(MessageQueue *messageQueue) { m_messageQueueToChannel = messageQueue; }
    void setChannel(ChannelAPI *channel) { m_channel = channel; }

    double getMagSq() const { return m_magsq; }
    AudioFifo *getAudioFifo() { return &m_audioFifo; }
    void setAudioFifoLabel(const QString& label) { m_audioFifo.setLabel(label); }
    //!< Centre frequency of the device, Hz: with the channel offset it names the voice files and is the frequency of
    //!< the receivers (part of their duplicate key)
    void setCenterFrequency(qint64 frequency)
    {
        m_centerFrequency = frequency;
        applyReceiverFrequency();
    }
    void applyAudioSampleRate(int sampleRate);
    int getAudioSampleRate() const { return m_audioSampleRate; }
    Status getStatus() const;
    //!< Forget what the receivers of a protocol remember (0 POCSAG, 1 FLEX, 2 GSC, -1 all): duplicates,
    //!< fragments, groups, message numbers, network time
    void resetProtocol(int protocol);

    void getMagSqLevels(double& avg, double& peak, int& nbSamples)
    {
        if (m_magsqCount > 0)
        {
            m_magsq = m_magsqSum / m_magsqCount;
            m_magSqLevelStore.m_magsq = m_magsq;
            m_magSqLevelStore.m_magsqPeak = m_magsqPeak;
        }

        avg = m_magSqLevelStore.m_magsq;
        peak = m_magSqLevelStore.m_magsqPeak;
        nbSamples = m_magsqCount == 0 ? 1 : m_magsqCount;

        m_magsqSum = 0.0f;
        m_magsqPeak = 0.0f;
        m_magsqCount = 0;
    }

private:
    struct MagSqLevelsStore
    {
        MagSqLevelsStore() :
            m_magsq(1e-12),
            m_magsqPeak(1e-12)
        {}
        double m_magsq;
        double m_magsqPeak;
    };

    //! Low pass whose taps are changed without clearing its samples (same number of taps, so
    //! the same delay): the leg changes width without a transient
    struct LegFilter : public Lowpass<Complex>
    {
        void retune(int nTaps, double sampleRate, double cutoff) {
            FirFilterGenerators::generateLowPassFilter(nTaps, sampleRate, cutoff, this->m_taps);
        }
    };

    // Frequency correction, filter and FM discriminator of one protocol. Hunting, the leg is
    // the channel wide, centred on the last carrier offset, which decays to 0. While a
    // transmission is received it is moved onto the carrier offset measured on the known
    // patterns and narrowed: POCSAG and FLEX to the Carson bandwidth, GSC to the Carson
    // bandwidth and the offset room
    struct Leg
    {
        NCO m_nco;
        LegFilter m_filter;
        PhaseDiscriminators m_discri;
        double m_huntBandwidth = 0.0;
        double m_trackBandwidth = 0.0;
        double m_deviation = 1.0;       //!< Hz at discriminator output 1
        double m_maxShift = 0.0;        //!< Largest correction: the room the channel leaves, Hz
        bool m_tracking = false;
        double m_shift = 0.0;           //!< Correction applied: the signal is moved down by it, Hz
        double m_huntCentre = 0.0;      //!< Correction while hunting, Hz
        int m_fits = 0;                 //!< Measurements of the transmission being received
        quint32 m_messagesAtStart = 0;
        uint32_t m_framesAtStart = 0;
        int m_pendingSamples = 0;       //!< Samples until a correction reaches the discriminator
        float m_pendingDelta = 0.0f;    //!< DC change of that correction, discriminator units

        void create(double huntBandwidth, double trackBandwidth, double channelBandwidth, double deviation);
        void setTracking(bool tracking);
        //!< Move the correction; the receivers' levels move by the returned delta once it has
        //!< gone through the filter (takeHandover())
        void setShift(double shift);
        bool takeHandover(float& delta);
        Real demod(const Complex& sample);
        //!< The same, and the IQ the discriminator saw (shifted and filtered)
        Real demod(const Complex& sample, Complex& filtered);
    };

    RadioPagingDemodSettings m_settings;
    ChannelAPI *m_channel;
    MessageQueue *m_messageQueueToChannel;
    int m_channelSampleRate;
    int m_channelFrequencyOffset;

    NCO m_nco;
    Interpolator m_interpolator;
    Real m_interpolatorDistance;
    Real m_interpolatorDistanceRemain;

    double m_magsq;
    double m_magsqSum;
    double m_magsqPeak;
    int m_magsqCount;
    MagSqLevelsStore m_magSqLevelStore;
    MovingAverageUtil<Real, double, 16> m_movingAverage;

    Leg m_pocsagLeg;
    Leg m_flexLeg;
    Leg m_gscLeg;
    RadioPagingPocsagReceiver m_pocsag[RadioPagingDemodSettings::m_nbPocsagSpeeds];

    //! Tone detectors of a leg and their carrier offset and deviation (see RadioPagingToneOffset)
    struct ToneFront
    {
        struct Detector
        {
            double m_symbolRate;
            int m_levels;
            double m_window;            //!< Symbols
        };

        RadioPagingToneOffset m_estimator;
        std::vector<RadioPagingToneDetector> m_detectors;
        std::vector<int> m_windows;     //!< Samples integrated by each detector
        //!< Signal quality of each detector at the last sample: log10 of the strongest tone over the others
        std::vector<float> m_quality;
        //!< The detectors get the IQ one estimator block late: the carrier and the deviation measured on a
        //!< block are those of its samples, so a new transmission is received from its first symbol
        std::vector<Complex> m_delayIQ;
        std::vector<float> m_delayMagsq;
        int m_delayPos = 0;
        double m_phase = 0.0;
        double m_measuredDeviation = -1.0;
        //!< Carrier offset and deviation the detectors use: the estimate, or once a transmission has been decoded
        //!< with it (confirm()), the estimates near it only, until none has been decoded for a while. A transmitter
        //!< changes them slowly; a false fit of the estimator (idle fill, the 4-level alias) is far from them
        double m_usedOffset = 0.0;
        double m_usedDeviation = 0.0;
        bool m_confirmed = false;
        //!< The carrier offset and the deviation used are kept (GSC voice: the spectrum is that of the voice)
        bool m_hold = false;
        double m_confirmedOffset = 0.0;
        double m_confirmedDeviation = 0.0;
        int64_t m_sinceConfirm = 0;
        bool m_created = false;

        //!< The detectors, the levels of the tones the estimator looks for and the deviations it searches, Hz. delay:
        //!< the detectors get the IQ one estimator block late (their symbols are decoded); without, they are on the
        //!< time of the discriminator, whose symbols their quality goes with
        void create(const std::vector<Detector>& detectors, int estimatorLevels, double minDeviation, double maxDeviation, bool delay);
        //!< The output of each detector for the IQ of the leg, and its strongest tone; the magnitude squared
        //!< returned has the same delay
        float process(const Complex& filtered, float magsq, std::vector<float>& out, std::vector<int>& strongest);
        //!< A sample of the leg (the voice of the discriminator) with the delay of the detectors
        float delay(float sample)
        {
            if (m_delayIQ.empty()) {
                return sample;
            }
            if (m_delayAudio.size() != m_delayIQ.size())
            {
                m_delayAudio.assign(m_delayIQ.size(), 0.0f);
                m_delayAudioPos = 0;
            }
            float out = m_delayAudio[m_delayAudioPos];
            m_delayAudio[m_delayAudioPos] = sample;
            m_delayAudioPos = (m_delayAudioPos + 1) % (int) m_delayAudio.size();
            return out;
        }
        std::vector<float> m_delayAudio;
        int m_delayAudioPos = 0;
        //!< A transmission has been decoded with the carrier offset and the deviation used
        void confirm();
        //!< The leg moved its frequency correction to shift (the signal moved down by it): the carrier used and
        //!< confirmed move with it
        void followLeg(double shift)
        {
            if (std::isnan(m_legShift))
            {
                m_legShift = shift;
                return;
            }
            double delta = shift - m_legShift;
            m_usedOffset -= delta;
            m_confirmedOffset -= delta;
            m_legShift = shift;
        }
        double m_legShift = NAN;
    };
    ToneFront m_pocsagTones;
    ToneFront m_flexTones;
    ToneFront m_gscTones;
    std::vector<float> m_toneOut;
    std::vector<int> m_toneStrongest;
    uint32_t m_flexToneFrames = 0;      //!< Frames of the FLEX receiver when its tone estimate was last confirmed
public:
    //!< Experimental: symbols from tone detectors instead of the discriminator
    static bool s_tones;
    //!< Window of each detector, symbols: 0 for the one of each mode, else this one for all
    static double s_toneWindow;
    //!< 4-level symbols decided by the strongest tone (else by the level of the detector output)
    static bool s_toneStrongest;
    static double s_toneOffsetSeconds;
    //!< Deviations searched, Hz
    static double s_toneMinDeviation;
    static double s_toneMaxDeviation;
    //!< A confirmed estimate takes new estimates this near it only (Hz, fraction of the deviation), for this long
    //!< after the last transmission decoded with it
    static double s_toneConfirmedOffset;
    static double s_toneConfirmedDeviation;
    static double s_toneConfirmedSeconds;
private:
    RadioPagingFlexReceiver m_flex;
    RadioPagingGscReceiver m_gsc;
    QString m_gscVoiceId[2];            //!< Record id of the voice page in progress, by polarity
    RadioPagingBerWindow m_lastBer[3];  //!< BER of the last reception of each protocol
    float m_lastOffset[3] = {0.0f, 0.0f, 0.0f};         //!< Carrier offset of the last reception, Hz
    bool m_lastOffsetValid[3] = {false, false, false};
    int m_lastPocsagBatches = 0;
    int m_lastGscPreamble = -1;
    bool m_lastGscBatch = false;
    bool m_lastGscNbs = false;
    int m_lastGscSlot = 0;
    qint64 m_resetTime[3] = {0, 0, 0};  //!< When the state of each protocol was last reset, ms since epoch
    //! What is kept of the reception of each polarity of each protocol between the status updates
    struct PolarityLast
    {
        bool m_seen = false;
        RadioPagingBerWindow m_ber;
        float m_offset = 0.0f;
        bool m_offsetValid = false;
    };
    PolarityLast m_polLast[3][2];
    quint32 m_polMessages[3][2] = {{0, 0}, {0, 0}, {0, 0}};    //!< Records sent, by polarity (m_statusMutex)
    bool m_polMessageSeen[3][2] = {{false, false}, {false, false}, {false, false}}; //!< A record sent since the reset (m_statusMutex)
    QString m_flexLastMode[2];
    RadioPagingFlexDecoder::FrameReport m_flexLastFrame[2];
    RadioPagingBerWindow m_flexLastPhaseBer[2][4];

    // GSC voice audio: the GSC leg discriminator, DC blocked, low pass filtered and
    // resampled to the audio rate, only while a voice page is open
    AudioFifo m_audioFifo;
    AudioVector m_audioBuffer;
    std::size_t m_audioBufferFill;
    int m_audioSampleRate;
    Interpolator m_audioInterpolator;
    Real m_audioDistance;
    Real m_audioDistanceRemain;
    bool m_audioOpen;
    // One voice message at a time is recorded, that of the GSC receiver in a voice message
    std::unique_ptr<WavFileRecord> m_voiceRecorder; //!< Set while the voice message is recorded
    QString m_voiceFile;                //!< The file being recorded
    qint64 m_centerFrequency = 0;
    QString m_voiceFileDone;            //!< The file of the voice message that ended, for its record
    std::vector<qint16> m_voiceBuffer;  //!< Samples waiting to be written to the file
    Real m_dcIn;
    Real m_dcOut;

    mutable QMutex m_statusMutex;
    Status m_status;
    int m_statusCount;

    void processOneSample(Complex& ci);
    void applyLegs();
    //!< A receiver measured the DC offset dc on a known pattern: the leg absorbs it
    void legMeasured(Leg& leg, int protocol, float dc);
    //!< The receivers of the leg hunt again: back to the hunting width and centre
    void legReleased(Leg& leg, int protocol, bool confirmed);
    int m_afcCount = 0;                 //!< Status periods since the last decay of the hunting centres
    void decayHuntCentres();
    void processAudio(Real fm);
    void pushAudio(Real sample);
    //!< The voice is written to a WAV file in the voice directory, at the audio rate, whatever the volume
    void startVoiceRecording();
    void recordAudio(Real sample);
    //!< Closes the file; its name, empty if none was recorded
    QString finishVoiceRecording();
    void closeAudio();
    void updateStatus();
    void updatePolarityStatus(Status& status, const RadioPagingFlexReceiver::Status& flex, const RadioPagingGscReceiver::Status& gsc);
    void sendRecord(MessageRecord& record, int protocol);
    void sendPocsag(const RadioPagingPocsagDecoder::Message& message, int baud);
    void sendFlex(const RadioPagingFlexDecoder::Message& message);
    //!< Frequency of the channel to the receivers
    void applyReceiverFrequency();
    void sendGsc(const RadioPagingGscDecoder::Message& message);
    static MessageRecord::Polarity polarity(bool inverted);
    static QString checkName(int check);
    //!< Characters below 0x20 and 0x7f as <0xNN>, other bytes kept (UTF-8 passes)
    static QString escapeText(const std::string& text);
    //!< The codepage of the key in the protocol, else the default of the protocol
    static const RadioPagingCharset::Codepage *codepage(const RadioPagingCharset::Key& key, const char *protocol);
    //!< The codepage of HEX/binary messages (FLEX Kanji, STD-43A Japanese Text Service)
    static bool isKanji(const RadioPagingCharset::Codepage *codepage);
    static QByteArray toByteArray(const std::vector<uint8_t>& data);
};

#endif // INCLUDE_RADIOPAGINGDEMODSINK_H
