#pragma once

#include "Data.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Wire.hpp"
#include <cstdint>
#include <span>

#ifndef LOG_VERBOSE_PUBSUB
#define LOG_VERBOSE_PUBSUB 0
#endif

namespace Totem::PubSubBackend::detail {

inline void log_trace_packet(const char *stage, const Header &header,
                             const char *owner = "") {
#if LOG_VERBOSE_PUBSUB
    const auto nowUs = static_cast<uint64_t>(::platform::get_time_us());
    const auto ageUs =
        header.timestampUs == 0 || nowUs < header.timestampUs
            ? 0U
            : static_cast<unsigned long long>(nowUs - header.timestampUs);
    _log_v("PubSub trace %-24s owner=%s msg=%lu topic=" SV_FMT
           " source=" SV_FMT " created=%llu us now=%llu us age=%llu us "
           "payload=%u",
           stage, owner, header.messageId, MAGIC_SV_ARG(Spec::Topic,
                                                        header.topic),
           MAGIC_SV_ARG(Spec::NodeId, header.source),
           static_cast<unsigned long long>(header.timestampUs),
           static_cast<unsigned long long>(nowUs), ageUs, header.payloadSize);
#else
    (void)stage;
    (void)header;
    (void)owner;
#endif
}

inline void log_trace_bytes(const char *stage, size_t bytes,
                            const char *owner = "") {
#if LOG_VERBOSE_PUBSUB
    _log_v("PubSub trace %-24s owner=%s bytes=%zu now=%llu us", stage, owner,
           bytes, static_cast<unsigned long long>(::platform::get_time_us()));
#else
    (void)stage;
    (void)bytes;
    (void)owner;
#endif
}

template <typename HeaderReader>
inline void log_trace_frame(const char *stage, std::span<const std::byte> frame,
                            HeaderReader reader, const char *owner = "") {
#if LOG_VERBOSE_PUBSUB
    auto headerResult = reader(frame);
    if (headerResult) {
        log_trace_packet(stage, *headerResult, owner);
        return;
    }
    log_trace_bytes(stage, frame.size(), owner);
#else
    (void)stage;
    (void)frame;
    (void)reader;
    (void)owner;
#endif
}

} // namespace Totem::PubSubBackend::detail
