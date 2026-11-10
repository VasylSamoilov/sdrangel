///////////////////////////////////////////////////////////////////////////////////
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

#ifndef MODEMCELLULAR_CARRIERRADIO_H_
#define MODEMCELLULAR_CARRIERRADIO_H_

#include <complex>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "carrierbatch.h"
#include "firfilter.h"
#include "export.h"

namespace modemcellular
{

// The FM radio of several carriers of one stream (the span of one SDR). The audio side is the
// instantaneous frequency deviation in Hz at 48 kHz; emphasis and the deviation of the standard
// are the business of the network. The stream rate has to be an even multiple of 48 kHz: the
// network clock is then an exact fraction of the stream clock.

// Where a carrier is in the stream, and the filter it needs.
struct CarrierTuning
{
    int id = 0;
    double offset = 0.0;            // Hz from the centre of the stream
    double passband = 9000.0;       // Hz either side: deviation plus modulation
    double spacing = 25000.0;       // Hz to the next carrier of the plan
};

class MODEMCELLULAR_API CarrierRadio
{
public:
    static constexpr double kAudioRate = 48000.0;
    static constexpr int kBlock = 480;              // 10 ms

    static bool rateSupported(double streamRate);
    // the factor from the stream rate to the audio rate, 0 if the rate is not supported
    static int decimation(double streamRate);
};

// Receiver: stream samples in, a batch of all carriers per block out.
class MODEMCELLULAR_API CarrierReceiver
{
public:
    using BatchHandler = std::function<void(const CarrierBatch&)>;

    explicit CarrierReceiver(double streamRate);
    ~CarrierReceiver();

    bool valid() const { return m_decimation > 0; }
    // a new set of carriers starts a new block (the sample count goes on)
    void setCarriers(const std::vector<CarrierTuning>& carriers);
    const std::vector<CarrierTuning>& carriers() const { return m_tuning; }
    void setBatchHandler(BatchHandler handler) { m_handler = std::move(handler); }

    // samples of the stream, full scale 1.0
    void feed(const std::complex<float>* samples, size_t count);
    // audio samples per carrier handed out so far: the clock of the networks on this stream
    uint64_t samples() const { return m_samples; }

private:
    struct Carrier;

    double m_rate;
    int m_decimation;
    std::vector<CarrierTuning> m_tuning;
    std::vector<std::unique_ptr<Carrier>> m_carriers;
    std::vector<std::complex<float>> m_mixed;
    CarrierBatch m_batch;
    std::vector<double> m_power;
    int m_fill;                     // audio samples in the current block
    uint64_t m_samples;
    BatchHandler m_handler;
};

// Transmitter: the audio of each carrier from a source, stream samples out. The carriers are
// summed at 1/N each, so the stream never goes over full scale.
class MODEMCELLULAR_API CarrierTransmitter
{
public:
    // fills count audio samples (Hz) of the carrier with the index into the carriers
    using AudioSource = std::function<void(size_t index, float* audio, size_t count)>;

    explicit CarrierTransmitter(double streamRate);
    ~CarrierTransmitter();

    bool valid() const { return m_decimation > 0; }
    void setCarriers(const std::vector<CarrierTuning>& carriers);
    const std::vector<CarrierTuning>& carriers() const { return m_tuning; }
    void setAudioSource(AudioSource source) { m_source = std::move(source); }

    // count stream samples, full scale 1.0
    void pull(std::complex<float>* samples, size_t count);

private:
    struct Carrier;

    void renderBlock();

    double m_rate;
    int m_decimation;
    std::vector<CarrierTuning> m_tuning;
    std::vector<std::unique_ptr<Carrier>> m_carriers;
    std::vector<std::complex<float>> m_pending;     // rendered stream samples not taken yet
    size_t m_taken;
    std::vector<float> m_audio;
    std::vector<std::complex<float>> m_base;
    std::vector<std::complex<float>> m_mid;
    std::vector<std::complex<float>> m_up;
    AudioSource m_source;
};

} // namespace modemcellular

#endif // MODEMCELLULAR_CARRIERRADIO_H_
