#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/Logging.hpp"
#include "Support/Basic.hpp"
#include "Wire/Interfaces/Request.hpp"
#include "Wire/Rs485/detail/Pdu.hpp"

namespace Totem::Wire::Rs485::detail {

inline void log_trace_packet(const char *stage, const Header &header,
                             const char *owner = "") {
    if constexpr (!tracing_for(Tracing::rs485)) {
        ignore_unused(stage, header, owner);
        return;
    }
    _log_v("RS485 trace %-22s owner=%s type=" SV_FMT " payload=" SV_FMT
           " seq=%u responseTo=%u len=%u now=%llu us",
           stage, owner, MAGIC_SV_ARG(FrameType, header.type),
           MAGIC_SV_ARG(PayloadType, header.payloadType), header.sequenceNumber,
           header.responseTo, header.payloadLength,
           static_cast<unsigned long long>(::platform::get_time_us()));
}

} // namespace Totem::Wire::Rs485::detail
