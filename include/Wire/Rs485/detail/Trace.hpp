#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"
#include "Wire/Rs485/detail/Types.hpp"
#include <cstdint>

#ifndef LOG_VERBOSE_RS485
#define LOG_VERBOSE_RS485 0
#endif

namespace Totem::Wire::Rs485::detail {

inline void log_trace_packet(const char *stage, const Header &header,
                             const char *owner = "") {
#if LOG_VERBOSE_RS485
    _log_v("RS485 trace %-22s owner=%s type=" SV_FMT " payload=" SV_FMT
           " seq=%u responseTo=%u len=%u now=%llu us",
           stage, owner, MAGIC_SV_ARG(FrameType, header.type),
           MAGIC_SV_ARG(PayloadType, header.payloadType), header.sequenceNumber,
           header.responseTo, header.payloadLength,
           static_cast<unsigned long long>(::platform::get_time_us()));
#else
    (void)stage;
    (void)header;
    (void)owner;
#endif
}

} // namespace Totem::Wire::Rs485::detail
