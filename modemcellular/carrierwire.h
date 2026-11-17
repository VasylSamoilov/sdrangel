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

#ifndef MODEMCELLULAR_CARRIERWIRE_H_
#define MODEMCELLULAR_CARRIERWIRE_H_

#include <cstdint>
#include <vector>

#include "carrierbatch.h"
#include "carrierradio.h"
#include "export.h"

namespace modemcellular
{

// What the radio plugins and the controller of the cellular base stations send each other, as
// packets of bytes (the plugins are separate libraries; a packet of bytes is a message type they
// all know). Inside one process: native byte order, no versioning.
enum class WireKind : uint8_t
{
    None = 0,
    Batch = 1,          // receiver to controller: received audio; controller to transmitter: audio to send
    Span = 2,           // radio to controller: centre and rate of its stream
    SpanRequest = 3,    // controller to radio: send the span
    Tuning = 4,         // controller to radio: the carriers to run (none: stop)
    TxStatus = 5        // transmitter to controller: queue fill and underruns per carrier
};

struct WireSpan
{
    double centre = 0.0;        // Hz
    double sampleRate = 0.0;    // Hz, 0 while the device does not run
};

struct WireTuning
{
    std::vector<CarrierTuning> carriers;
    uint32_t lead = 0;          // transmitter: samples of lead of the queues
};

struct WireTxStatus
{
    std::vector<int> carriers;
    std::vector<uint32_t> fill;
    std::vector<uint64_t> underruns;
};

MODEMCELLULAR_API WireKind wireKind(const uint8_t* data, size_t size);

MODEMCELLULAR_API std::vector<uint8_t> encodeBatch(const CarrierBatch& batch);
MODEMCELLULAR_API bool decodeBatch(const uint8_t* data, size_t size, CarrierBatch& batch);

MODEMCELLULAR_API std::vector<uint8_t> encodeSpan(const WireSpan& span);
MODEMCELLULAR_API bool decodeSpan(const uint8_t* data, size_t size, WireSpan& span);

MODEMCELLULAR_API std::vector<uint8_t> encodeSpanRequest();

MODEMCELLULAR_API std::vector<uint8_t> encodeTuning(const WireTuning& tuning);
MODEMCELLULAR_API bool decodeTuning(const uint8_t* data, size_t size, WireTuning& tuning);

MODEMCELLULAR_API std::vector<uint8_t> encodeTxStatus(const WireTxStatus& status);
MODEMCELLULAR_API bool decodeTxStatus(const uint8_t* data, size_t size, WireTxStatus& status);

} // namespace modemcellular

#endif // MODEMCELLULAR_CARRIERWIRE_H_
