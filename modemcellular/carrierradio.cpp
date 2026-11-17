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

#include "carrierradio.h"

#include <algorithm>
#include <cmath>

namespace modemcellular
{

namespace
{

const double kPi = 3.14159265358979323846;
const double kMidRate = 2.0 * CarrierRadio::kAudioRate;     // between the two filter stages

// The edges of the filters of a carrier: keep the passband, reject from where the next carrier
// of the plan begins.
double stopEdge(const CarrierTuning& t)
{
    const double stop = t.spacing - t.passband;

    return stop > t.passband ? stop : 1.5 * t.passband;
}

// The filter of the stage at the stream rate: what aliases onto the passband at the middle rate
// has to go.
std::vector<float> streamTaps(const CarrierTuning& t, double rate, int factor)
{
    if (factor <= 1) {
        return {1.0f};
    }
    const double stop = kMidRate - stopEdge(t);

    return designLowpass((t.passband + stop) / 2.0, rate, lowpassTaps(stop - t.passband, rate));
}

// The channel filter at the middle rate.
std::vector<float> channelTaps(const CarrierTuning& t)
{
    const double stop = stopEdge(t);

    return designLowpass((t.passband + stop) / 2.0, kMidRate, lowpassTaps(stop - t.passband, kMidRate));
}

// A complex oscillator; renormalised now and then against the rounding of the recursion.
struct Oscillator
{
    std::complex<double> phasor{1.0, 0.0};
    std::complex<double> step{1.0, 0.0};

    void set(double frequency, double rate)
    {
        phasor = {1.0, 0.0};
        step = std::polar(1.0, 2.0 * kPi * frequency / rate);
    }
    std::complex<float> next()
    {
        const std::complex<float> out(static_cast<float>(phasor.real()), static_cast<float>(phasor.imag()));
        phasor *= step;
        return out;
    }
    void normalise() { phasor /= std::abs(phasor); }
};

// a carrier that stays as it was: same id, same place, same filters
bool sameTuning(const CarrierTuning& a, const CarrierTuning& b)
{
    return a.id == b.id && a.offset == b.offset && a.passband == b.passband && a.spacing == b.spacing;
}

} // namespace

bool CarrierRadio::rateSupported(double streamRate)
{
    return decimation(streamRate) > 0;
}

int CarrierRadio::decimation(double streamRate)
{
    const double ratio = streamRate / kAudioRate;
    const long factor = std::lround(ratio);

    if (factor < 2 || factor % 2 != 0 || std::fabs(ratio - static_cast<double>(factor)) > 1e-9) {
        return 0;
    }
    return static_cast<int>(factor);
}

// ---- receiver ----

struct CarrierReceiver::Carrier
{
    Carrier(const CarrierTuning& t, double rate, int factor) :
        stage1(factor / 2, streamTaps(t, rate, factor / 2)),
        stage2(2, channelTaps(t))
    {
        nco.set(-t.offset, rate);
    }

    Oscillator nco;
    FirDecimator stage1;
    FirDecimator stage2;
    std::complex<float> last{0.0f, 0.0f};
    std::vector<std::complex<float>> mid;
    std::vector<std::complex<float>> base;
    std::vector<float> audio;
};

CarrierReceiver::CarrierReceiver(double streamRate) :
    m_rate(streamRate),
    m_decimation(CarrierRadio::decimation(streamRate)),
    m_fill(0),
    m_samples(0),
    m_idle(0)
{
}

CarrierReceiver::~CarrierReceiver() = default;

void CarrierReceiver::setCarriers(const std::vector<CarrierTuning>& carriers)
{
    // the carriers that stay keep their filters, their FM state and their part of the block in
    // progress: a change of the others does not touch them
    std::vector<std::unique_ptr<Carrier>> kept(carriers.size());
    std::vector<int> ids;
    std::vector<int> from(carriers.size(), -1);

    for (size_t i = 0; i < carriers.size(); i++)
    {
        for (size_t j = 0; j < m_tuning.size() && j < m_carriers.size(); j++)
        {
            if (m_carriers[j] && sameTuning(m_tuning[j], carriers[i]))
            {
                kept[i] = std::move(m_carriers[j]);
                from[i] = static_cast<int>(j);
                break;
            }
        }
        ids.push_back(carriers[i].id);
    }
    // new carriers decimate in step with the kept ones
    const Carrier *pace = nullptr;
    for (const std::unique_ptr<Carrier>& c : kept)
    {
        if (c) {
            pace = c.get();
        }
    }
    for (size_t i = 0; i < carriers.size(); i++)
    {
        if (kept[i] || m_decimation <= 0) {
            continue;
        }
        kept[i] = std::make_unique<Carrier>(carriers[i], m_rate, m_decimation);
        if (pace)
        {
            kept[i]->stage1.setPhase(pace->stage1.phase());
            kept[i]->stage2.setPhase(pace->stage2.phase());
        }
    }

    const bool running = pace != nullptr;
    if (!running)
    {
        // the block in progress ends: its samples count on the clock
        m_samples += static_cast<uint64_t>(m_fill);
        m_fill = 0;
    }
    CarrierBatch batch;
    batch.reset(running ? m_batch.sample : m_samples, CarrierRadio::kBlock, ids);
    std::vector<double> power(ids.size(), 0.0);
    for (size_t i = 0; i < carriers.size(); i++)
    {
        if (from[i] < 0 || m_fill <= 0) {
            continue;
        }
        const float *old = m_batch.carrier(static_cast<size_t>(from[i]));
        std::copy(old, old + m_fill, batch.carrier(i));
        power[i] = m_power[static_cast<size_t>(from[i])];
    }

    m_tuning = carriers;
    m_carriers = std::move(kept);
    m_batch = std::move(batch);
    m_power = std::move(power);
    if (m_carriers.empty() || m_decimation <= 0) {
        m_carriers.clear();
    }
}

void CarrierReceiver::feed(const std::complex<float>* samples, size_t count)
{
    if (m_carriers.empty())
    {
        // the clock goes on without carriers (the remainder of the decimation is kept)
        const size_t factor = static_cast<size_t>(m_decimation > 0 ? m_decimation : 1);
        m_idle += count;
        m_samples += m_idle / factor;
        m_idle %= factor;
        return;
    }

    m_mixed.resize(count);
    const double toHz = CarrierRadio::kAudioRate / (2.0 * kPi);

    for (const std::unique_ptr<Carrier>& c : m_carriers)
    {
        for (size_t i = 0; i < count; i++) {
            m_mixed[i] = samples[i] * c->nco.next();
        }
        c->nco.normalise();
        c->mid.clear();
        c->stage1.process(m_mixed.data(), count, c->mid);
        c->base.clear();
        c->stage2.process(c->mid.data(), c->mid.size(), c->base);
        c->audio.resize(c->base.size());
        for (size_t i = 0; i < c->base.size(); i++)
        {
            // the phase step from the last sample is the frequency
            const std::complex<float> d = c->base[i] * std::conj(c->last);
            c->audio[i] = static_cast<float>(std::atan2(d.imag(), d.real()) * toHz);
            c->last = c->base[i];
        }
    }

    // all carriers have the same filters and decimate in step
    const size_t produced = m_carriers[0]->audio.size();

    for (size_t j = 0; j < produced; j++)
    {
        for (size_t c = 0; c < m_carriers.size(); c++)
        {
            m_batch.carrier(c)[m_fill] = m_carriers[c]->audio[j];
            m_power[c] += std::norm(m_carriers[c]->base[j]);
        }
        if (++m_fill < CarrierRadio::kBlock) {
            continue;
        }
        for (size_t c = 0; c < m_carriers.size(); c++)
        {
            const double mean = m_power[c] / CarrierRadio::kBlock;
            m_batch.levels[c] = static_cast<float>(10.0 * std::log10(mean > 1e-20 ? mean : 1e-20));
            m_power[c] = 0.0;
        }
        if (m_handler) {
            m_handler(m_batch);
        }
        m_samples += CarrierRadio::kBlock;
        m_fill = 0;
        m_batch.sample = m_samples;
    }
}

// ---- transmitter ----

struct CarrierTransmitter::Carrier
{
    Carrier(const CarrierTuning& t, double rate, int factor) :
        stage1(2, channelTaps(t)),
        stage2(factor / 2, streamTaps(t, rate, factor / 2))
    {
        nco.set(t.offset, rate);
    }

    Oscillator nco;
    FirInterpolator stage1;
    FirInterpolator stage2;
    double phase = 0.0;
    // keying: a carrier comes up and goes down over one block, not at once (a step would splatter
    // over the whole stream, also into the neighbouring carriers)
    enum class Ramp { Up, On, Down } ramp = Ramp::Up;
};

CarrierTransmitter::CarrierTransmitter(double streamRate) :
    m_rate(streamRate),
    m_decimation(CarrierRadio::decimation(streamRate)),
    m_taken(0)
{
}

CarrierTransmitter::~CarrierTransmitter() = default;

void CarrierTransmitter::setCarriers(const std::vector<CarrierTuning>& carriers)
{
    // the carriers that stay keep their filters and their FM phase
    std::vector<std::unique_ptr<Carrier>> kept(carriers.size());

    for (size_t i = 0; i < carriers.size(); i++)
    {
        for (size_t j = 0; j < m_tuning.size() && j < m_carriers.size(); j++)
        {
            if (m_carriers[j] && sameTuning(m_tuning[j], carriers[i]))
            {
                kept[i] = std::move(m_carriers[j]);
                break;
            }
        }
        if (!kept[i] && m_decimation > 0) {
            kept[i] = std::make_unique<Carrier>(carriers[i], m_rate, m_decimation);
        }
    }
    // the ones that go are sent down over the next block
    for (std::unique_ptr<Carrier>& gone : m_carriers)
    {
        if (gone)
        {
            gone->ramp = Carrier::Ramp::Down;
            m_fading.push_back(std::move(gone));
        }
    }
    m_tuning = carriers;
    m_carriers = std::move(kept);
    if (m_decimation <= 0)
    {
        m_carriers.clear();
        m_fading.clear();
    }
}

void CarrierTransmitter::renderBlock()
{
    const size_t block = static_cast<size_t>(CarrierRadio::kBlock);
    const size_t out = block * static_cast<size_t>(m_decimation);
    // the carriers that go down still count: the sum stays below full scale
    const size_t carriers = m_carriers.size() + m_fading.size();
    // Each carrier has the share weight / sum of the weights: on 1, coming up r, going down 1 - r
    // (r a raised cosine over the block). The sum of the shares is kTxLevel at every sample (below
    // full scale), and no level steps: a step of the envelope of a carrier would splatter.
    size_t on = 0;
    size_t up = 0;
    size_t down = 0;
    for (const std::unique_ptr<Carrier>& c : m_carriers)
    {
        on += c->ramp == Carrier::Ramp::On ? 1 : 0;
        up += c->ramp == Carrier::Ramp::Up ? 1 : 0;
    }
    down = m_fading.size();
    const double toPhase = 2.0 * kPi / CarrierRadio::kAudioRate;

    // what was taken goes
    m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(m_taken));
    m_taken = 0;
    const size_t start = m_pending.size();
    m_pending.resize(start + out, std::complex<float>(0.0f, 0.0f));

    m_audio.resize(block);
    m_base.resize(block);
    for (size_t index = 0; index < carriers; index++)
    {
        const bool active = index < m_carriers.size();
        Carrier& c = active ? *m_carriers[index] : *m_fading[index - m_carriers.size()];

        // a carrier that goes down is not modulated any more
        std::fill(m_audio.begin(), m_audio.end(), 0.0f);
        if (active && m_source) {
            m_source(index, m_audio.data(), block);
        }
        for (size_t i = 0; i < block; i++)
        {
            c.phase = std::fmod(c.phase + toPhase * m_audio[i], 2.0 * kPi);
            m_base[i] = std::complex<float>(static_cast<float>(std::cos(c.phase)), static_cast<float>(std::sin(c.phase)));
        }
        m_mid.clear();
        c.stage1.process(m_base.data(), block, m_mid);
        m_up.clear();
        c.stage2.process(m_mid.data(), m_mid.size(), m_up);
        for (size_t i = 0; i < out && i < m_up.size(); i++)
        {
            const double t = static_cast<double>(i) / static_cast<double>(out);
            const double rise = 0.5 - 0.5 * std::cos(kPi * t);
            const double weights = static_cast<double>(on) + static_cast<double>(up) * rise + static_cast<double>(down) * (1.0 - rise);
            const double weight = c.ramp == Carrier::Ramp::On ? 1.0 : (c.ramp == Carrier::Ramp::Up ? rise : 1.0 - rise);
            const float level = weights > 0.0 ? static_cast<float>(CarrierRadio::kTxLevel * weight / weights) : 0.0f;
            m_pending[start + i] += m_up[i] * c.nco.next() * level;
        }
        c.nco.normalise();
        if (c.ramp == Carrier::Ramp::Up) {
            c.ramp = Carrier::Ramp::On;
        }
    }
    m_fading.clear();
}

void CarrierTransmitter::pull(std::complex<float>* samples, size_t count)
{
    if (m_carriers.empty() && m_fading.empty() && m_taken >= m_pending.size())
    {
        std::fill(samples, samples + count, std::complex<float>(0.0f, 0.0f));
        return;
    }
    size_t done = 0;

    while (done < count)
    {
        if (m_taken >= m_pending.size())
        {
            if (m_carriers.empty() && m_fading.empty())
            {
                std::fill(samples + done, samples + count, std::complex<float>(0.0f, 0.0f));
                return;
            }
            renderBlock();
        }
        const size_t n = std::min(count - done, m_pending.size() - m_taken);
        std::copy(m_pending.begin() + static_cast<std::ptrdiff_t>(m_taken),
            m_pending.begin() + static_cast<std::ptrdiff_t>(m_taken + n), samples + done);
        m_taken += n;
        done += n;
    }
}

} // namespace modemcellular
