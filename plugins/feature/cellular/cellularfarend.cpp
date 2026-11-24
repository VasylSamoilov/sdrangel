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

#include "cellularfarend.h"

#include <algorithm>
#include <cmath>

#include <QFile>

#include "audio/audiodevicemanager.h"
#include "dsp/dspengine.h"
#include "dsp/wavfilerecord.h"

namespace
{

// SDRangel's polyphase Interpolator as the NMT receiver uses it
const int kPhases = 96;
const double kTapsPerPhase = 24.0 / kPhases;
const double kCallRate = 8000.0;
const double kCutoff = 3400.0;
// a clip longer than this is cut (memory: 8 kHz floats)
const qint64 kMaxClipSeconds = 600;

} // namespace

std::vector<float> CellularFarEndAudio::to8k(const std::vector<float>& samples, int rate)
{
    std::vector<float> out;
    if (rate <= 0 || samples.empty()) {
        return out;
    }
    Interpolator resampler;
    resampler.create(kPhases, rate, kCutoff, kTapsPerPhase);
    const Real distance = static_cast<Real>(rate / kCallRate);
    Real remain = 0.0f;
    out.reserve(static_cast<size_t>(samples.size() / distance) + 2);

    for (float s : samples)
    {
        const Complex in(s, 0.0f);
        Complex o;
        // as the NMT receiver drives the Interpolator (SDRangel's DSD demodulator)
        if (distance < 1.0f)
        {
            while (!resampler.interpolate(&remain, in, &o))
            {
                out.push_back(o.real());
                remain += distance;
            }
        }
        else if (resampler.decimate(&remain, in, &o))
        {
            out.push_back(o.real());
            remain += distance;
        }
    }
    return out;
}

std::shared_ptr<std::vector<float>> CellularFarEndAudio::loadClip(const QString& path, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QString("cannot open %1").arg(path);
        return nullptr;
    }
    WavFileRecord::Header header;
    if (!WavFileRecord::readHeader(file, header))
    {
        error = QString("%1 is not a WAV file SDRangel reads").arg(path);
        return nullptr;
    }
    if (header.m_audioFormat != 1 || header.m_bitsPerSample != 16 || header.m_numChannels < 1 || header.m_sampleRate == 0)
    {
        error = QString("%1: only 16 bit PCM WAV files (%2 bit, format %3)").arg(path).arg(header.m_bitsPerSample).arg(header.m_audioFormat);
        return nullptr;
    }
    const int channels = header.m_numChannels;
    const qint64 maxFrames = kMaxClipSeconds * static_cast<qint64>(header.m_sampleRate);
    const QByteArray data = file.read(maxFrames * channels * 2);
    const qint16 *pcm = reinterpret_cast<const qint16*>(data.constData());
    const qint64 frames = data.size() / (2 * channels);
    std::vector<float> mono(static_cast<size_t>(frames));

    for (qint64 f = 0; f < frames; f++)
    {
        double sum = 0.0;
        for (int c = 0; c < channels; c++) {
            sum += pcm[f * channels + c];
        }
        mono[static_cast<size_t>(f)] = static_cast<float>(sum / channels / 32768.0);
    }
    auto clip = std::make_shared<std::vector<float>>(to8k(mono, static_cast<int>(header.m_sampleRate)));
    if (clip->empty())
    {
        error = QString("%1 has no samples").arg(path);
        return nullptr;
    }
    return clip;
}

CellularMusicInput::CellularMusicInput(const QString& device) :
    m_fifo(48000),
    m_rate(48000),
    m_distance(6.0f),
    m_remain(0.0f)
{
    AudioDeviceManager *audio = DSPEngine::instance()->getAudioDeviceManager();
    const int index = device.isEmpty() ? -1 : audio->getInputDeviceIndex(device);
    const int rate = audio->getInputSampleRate(index);
    m_rate = rate > 0 ? rate : 48000;
    m_distance = static_cast<Real>(m_rate / kCallRate);
    m_resampler.create(kPhases, m_rate, kCutoff, kTapsPerPhase);
    m_fifo.setSize(static_cast<uint32_t>(m_rate));          // 1 s
    audio->addAudioSource(&m_fifo, &m_queue, index);
}

CellularMusicInput::~CellularMusicInput()
{
    DSPEngine::instance()->getAudioDeviceManager()->removeAudioSource(&m_fifo);
}

void CellularMusicInput::fill(float* samples, int count)
{
    // what the device delivered, mixed to mono and resampled to 8 kHz
    const uint32_t available = m_fifo.fill();
    m_block.resize(available);
    const uint32_t got = available ? m_fifo.read(reinterpret_cast<quint8*>(m_block.data()), available) : 0;

    for (uint32_t i = 0; i < got; i++)
    {
        const Complex in(static_cast<Real>((m_block[i].l + m_block[i].r) / 65536.0), 0.0f);
        Complex o;
        if (m_distance < 1.0f)
        {
            while (!m_resampler.interpolate(&m_remain, in, &o))
            {
                m_ready.push_back(o.real());
                m_remain += m_distance;
            }
        }
        else if (m_resampler.decimate(&m_remain, in, &o))
        {
            m_ready.push_back(o.real());
            m_remain += m_distance;
        }
    }
    const size_t n = std::min(m_ready.size(), static_cast<size_t>(count));
    std::copy(m_ready.begin(), m_ready.begin() + static_cast<long>(n), samples);
    std::fill(samples + n, samples + count, 0.0f);
    m_ready.erase(m_ready.begin(), m_ready.begin() + static_cast<long>(n));
    // never more than 0.5 s behind the device
    if (m_ready.size() > 4000) {
        m_ready.erase(m_ready.begin(), m_ready.end() - 4000);
    }
}
