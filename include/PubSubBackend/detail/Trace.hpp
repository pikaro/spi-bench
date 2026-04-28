#pragma once

#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include "StaticConfig/Logging.hpp"
#include "Support/Basic.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::PubSubBackend::detail {

inline void log_trace_packet(const char *stage, const Header &header,
                             const char *owner = "") {
    if constexpr (!tracing_for(Tracing::pubSub)) {
        ignore_unused(stage, header, owner);
        return;
    }

    const auto nowUs = static_cast<uint64_t>(::platform::get_time_us());
    const auto ageUs =
        header.timestampUs == 0 || nowUs < header.timestampUs
            ? 0U
            : static_cast<unsigned long long>(nowUs - header.timestampUs);
    _log_v("PubSub trace %-24s owner=%s msg=%lu topic=" SV_FMT " source=" SV_FMT
           " created=%llu us now=%llu us age=%llu us "
           "payload=%u",
           stage, owner, header.messageId,
           MAGIC_SV_ARG(NodeData::PubSub::Topic, header.topic),
           MAGIC_SV_ARG(NodeData::PubSub::NodeId, header.source),
           static_cast<unsigned long long>(header.timestampUs),
           static_cast<unsigned long long>(nowUs), ageUs, header.payloadSize);
}

inline void log_trace_bytes(const char *stage, size_t bytes,
                            const char *owner = "") {
    if constexpr (!tracing_for(Tracing::pubSub)) {
        ignore_unused(stage, bytes, owner);
        return;
    }
    _log_v("PubSub trace %-24s owner=%s bytes=%zu now=%llu us", stage, owner,
           bytes, static_cast<unsigned long long>(::platform::get_time_us()));
}

template <typename HeaderReader>
inline void log_trace_frame(const char *stage, std::span<const std::byte> frame,
                            HeaderReader reader, const char *owner = "") {
    if constexpr (!tracing_for(Tracing::pubSub)) {
        ignore_unused(stage, frame, reader, owner);
        return;
    }
    auto headerResult = reader(frame);
    if (headerResult) {
        log_trace_packet(stage, *headerResult, owner);
        return;
    }
    log_trace_bytes(stage, frame.size(), owner);
}

} // namespace Totem::PubSubBackend::detail
