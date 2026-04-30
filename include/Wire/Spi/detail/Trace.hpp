#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Wire/Spi/detail/Pdu.hpp"

namespace Totem::Wire::Spi::detail {

inline void log_trace_slot(const char *stage, const SlotHeader &header,
                           const char *owner = "") {
    _log_v("SPI trace slot %-18s owner=%s peer=%u conn=%u seq=%u ack=%u "
           "frames=%u payload=%u slot=%u bucket=%u flags=0x%04X now=%llu us",
           stage, owner, header.peerId, header.connectionId, header.sequence,
           header.ackSequence, header.frameCount, header.payloadBytes,
           header.slotLength, header.bucketLength,
           static_cast<unsigned>(header.flags),
           static_cast<unsigned long long>(::platform::get_time_us()));
}

inline void log_trace_frame(const char *stage, const FrameHeader &header,
                            const char *owner = "") {
    _log_v("SPI trace frame %-17s owner=%s type=" SV_FMT " payload=" SV_FMT
           " seq=%u responseTo=%u len=%u flags=0x%02X now=%llu us",
           stage, owner, MAGIC_SV_ARG(FrameType, header.type),
           MAGIC_SV_ARG(PayloadType, header.payloadType), header.sequence,
           header.responseTo, header.payloadLength,
           static_cast<unsigned>(header.flags),
           static_cast<unsigned long long>(::platform::get_time_us()));
}

} // namespace Totem::Wire::Spi::detail
