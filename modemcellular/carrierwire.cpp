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

#include "carrierwire.h"

#include <cstring>

namespace modemcellular
{

namespace
{

// the limits of a sane packet: more carriers than any device span holds, a block of up to 1 s
const uint32_t kMaxCarriers = 1024;
const int32_t kMaxLength = 48000;

class Writer
{
public:
    explicit Writer(WireKind kind) { put(static_cast<uint8_t>(kind)); }

    template<typename T>
    void put(const T& value)
    {
        const size_t at = m_data.size();
        m_data.resize(at + sizeof(T));
        std::memcpy(m_data.data() + at, &value, sizeof(T));
    }
    template<typename T>
    void putArray(const T* values, size_t count)
    {
        const size_t at = m_data.size();
        m_data.resize(at + count * sizeof(T));
        if (count) {
            std::memcpy(m_data.data() + at, values, count * sizeof(T));
        }
    }
    std::vector<uint8_t> take() { return std::move(m_data); }

private:
    std::vector<uint8_t> m_data;
};

class Reader
{
public:
    Reader(const uint8_t* data, size_t size, WireKind kind) :
        m_data(data),
        m_size(size),
        m_pos(1),
        m_ok(data && size >= 1 && data[0] == static_cast<uint8_t>(kind))
    {
    }

    template<typename T>
    T get()
    {
        T value{};
        if (m_ok && m_pos + sizeof(T) <= m_size) {
            std::memcpy(&value, m_data + m_pos, sizeof(T));
        } else {
            m_ok = false;
        }
        m_pos += sizeof(T);
        return value;
    }
    template<typename T>
    void getArray(T* values, size_t count)
    {
        if (m_ok && m_pos + count * sizeof(T) <= m_size)
        {
            if (count) {
                std::memcpy(values, m_data + m_pos, count * sizeof(T));
            }
        }
        else
        {
            m_ok = false;
        }
        m_pos += count * sizeof(T);
    }
    // all of it read and nothing left over
    bool done() const { return m_ok && m_pos == m_size; }
    bool ok() const { return m_ok; }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos;
    bool m_ok;
};

} // namespace

WireKind wireKind(const uint8_t* data, size_t size)
{
    if (!data || size < 1 || data[0] < 1 || data[0] > 6) {
        return WireKind::None;
    }
    return static_cast<WireKind>(data[0]);
}

std::vector<uint8_t> encodeBatch(const CarrierBatch& batch)
{
    Writer w(WireKind::Batch);
    const uint32_t n = static_cast<uint32_t>(batch.carriers.size());

    w.put(batch.sample);
    w.put(static_cast<int32_t>(batch.length));
    w.put(n);
    w.putArray(batch.carriers.data(), n);
    w.put(static_cast<uint8_t>(batch.levels.size() == n ? 1 : 0));
    if (batch.levels.size() == n) {
        w.putArray(batch.levels.data(), n);
    }
    w.putArray(batch.samples.data(), batch.samples.size());
    return w.take();
}

bool decodeBatch(const uint8_t* data, size_t size, CarrierBatch& batch)
{
    Reader r(data, size, WireKind::Batch);
    const uint64_t sample = r.get<uint64_t>();
    const int32_t length = r.get<int32_t>();
    const uint32_t n = r.get<uint32_t>();

    if (!r.ok() || length < 0 || length > kMaxLength || n > kMaxCarriers) {
        return false;
    }
    // the size the header names, before anything is allocated: ids, the level flag, maybe the
    // levels, the samples
    const size_t samples = static_cast<size_t>(n) * static_cast<size_t>(length);
    const size_t head = 1 + 8 + 4 + 4 + n * sizeof(int32_t) + 1;
    if (size != head + samples * sizeof(float) && size != head + (n + samples) * sizeof(float)) {
        return false;
    }
    std::vector<int> ids(n);
    r.getArray(ids.data(), n);
    batch.reset(sample, length, ids);
    if (r.get<uint8_t>()) {
        r.getArray(batch.levels.data(), n);
    }
    r.getArray(batch.samples.data(), batch.samples.size());
    return r.done();
}

std::vector<uint8_t> encodeSpan(const WireSpan& span)
{
    Writer w(WireKind::Span);

    w.put(span.centre);
    w.put(span.sampleRate);
    return w.take();
}

bool decodeSpan(const uint8_t* data, size_t size, WireSpan& span)
{
    Reader r(data, size, WireKind::Span);

    span.centre = r.get<double>();
    span.sampleRate = r.get<double>();
    return r.done();
}

std::vector<uint8_t> encodeSpanRequest()
{
    return Writer(WireKind::SpanRequest).take();
}

std::vector<uint8_t> encodeTuning(const WireTuning& tuning)
{
    Writer w(WireKind::Tuning);

    w.put(tuning.lead);
    w.put(static_cast<uint32_t>(tuning.carriers.size()));
    for (const CarrierTuning& t : tuning.carriers)
    {
        w.put(static_cast<int32_t>(t.id));
        w.put(t.offset);
        w.put(t.passband);
        w.put(t.spacing);
    }
    return w.take();
}

bool decodeTuning(const uint8_t* data, size_t size, WireTuning& tuning)
{
    Reader r(data, size, WireKind::Tuning);

    tuning.lead = r.get<uint32_t>();
    const uint32_t n = r.get<uint32_t>();
    // id, offset, passband and spacing per carrier
    if (!r.ok() || n > kMaxCarriers || size != 1 + 4 + 4 + n * (4 + 3 * sizeof(double)) || tuning.lead > 10 * 48000) {
        return false;
    }
    tuning.carriers.resize(n);
    for (CarrierTuning& t : tuning.carriers)
    {
        t.id = r.get<int32_t>();
        t.offset = r.get<double>();
        t.passband = r.get<double>();
        t.spacing = r.get<double>();
    }
    return r.done();
}

std::vector<uint8_t> encodeTxStatus(const WireTxStatus& status)
{
    Writer w(WireKind::TxStatus);
    const uint32_t n = static_cast<uint32_t>(status.carriers.size());

    if (status.fill.size() != n || status.underruns.size() != n) {
        return Writer(WireKind::None).take();
    }
    w.put(n);
    w.putArray(status.carriers.data(), n);
    w.putArray(status.fill.data(), n);
    w.putArray(status.underruns.data(), n);
    return w.take();
}

bool decodeTxStatus(const uint8_t* data, size_t size, WireTxStatus& status)
{
    Reader r(data, size, WireKind::TxStatus);
    const uint32_t n = r.get<uint32_t>();

    // id, fill and underruns per carrier
    if (!r.ok() || n > kMaxCarriers || size != 1 + 4 + n * (4 + 4 + 8)) {
        return false;
    }
    status.carriers.resize(n);
    status.fill.resize(n);
    status.underruns.resize(n);
    r.getArray(status.carriers.data(), n);
    r.getArray(status.fill.data(), n);
    r.getArray(status.underruns.data(), n);
    return r.done();
}

std::vector<uint8_t> encodeNames(const WireNames& names)
{
    Writer w(WireKind::Names);
    const uint32_t n = static_cast<uint32_t>(names.networks.size());

    if (names.names.size() != n || n > kMaxCarriers) {
        return Writer(WireKind::None).take();
    }
    w.put(n);
    for (uint32_t i = 0; i < n; i++)
    {
        const std::string& name = names.names[i];
        if (name.size() > WireNames::kMaxName) {
            return Writer(WireKind::None).take();
        }
        w.put(static_cast<int32_t>(names.networks[i]));
        w.put(static_cast<uint8_t>(name.size()));
        w.putArray(name.data(), name.size());
    }
    return w.take();
}

bool decodeNames(const uint8_t* data, size_t size, WireNames& names)
{
    Reader r(data, size, WireKind::Names);
    const uint32_t n = r.get<uint32_t>();

    // at least an id and a length per network
    if (!r.ok() || n > kMaxCarriers || size < 1 + 4 + n * (4 + 1)) {
        return false;
    }
    names.networks.resize(n);
    names.names.resize(n);
    for (uint32_t i = 0; i < n && r.ok(); i++)
    {
        names.networks[i] = r.get<int32_t>();
        const uint8_t length = r.get<uint8_t>();
        names.names[i].resize(r.ok() ? length : 0);
        r.getArray(&names.names[i][0], names.names[i].size());
    }
    return r.done();
}

} // namespace modemcellular
