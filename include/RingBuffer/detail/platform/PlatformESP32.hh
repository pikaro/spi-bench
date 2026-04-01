#pragma once

#include "Common.hh"
#include "RingBuffer/detail/Config.hh"
#include "freertos/ringbuf.h"
#include <cstddef>
#include <expected>
#include <utility>

namespace Totem::RingBuffer::detail::platform {

struct PlatformConfig {
    RingbufferType_t type;

    static PlatformConfig
    fromAbstraction(const PlatformConfigAbstraction &abstraction) {
        PlatformConfig config;
        switch (abstraction.type) {
        case PlatformConfigAbstraction::Type::Message:
            config.type = abstraction.requiresContiguousStorage
                              ? RINGBUF_TYPE_NOSPLIT
                              : RINGBUF_TYPE_ALLOWSPLIT;
            break;
        case PlatformConfigAbstraction::Type::ByteStream:
            config.type = RINGBUF_TYPE_BYTEBUF;
            break;
        default:
            std::unreachable();
        }
        return config;
    }
};

struct RingBuffer {

    using RingBufferHandle = ::platform::RingBufferHandle;
    using Tick = ::platform::Tick;

    static std::expected<RingBufferHandle, ReturnCode> create(
        size_t size,
        const PlatformConfigAbstraction &config = PlatformConfigAbstraction()) {
        auto platformConfig = PlatformConfig::fromAbstraction(config);
        auto *handle = xRingbufferCreate(size, platformConfig.type);
        if (handle == nullptr) {
            _log_e("Failed to create ring buffer of size %zu", size);
            return std::unexpected(ERR(CoreError, OperationFailed));
        }
        return handle;
    }

    static ReturnCode destroy(RingBufferHandle handle) {
        vRingbufferDelete(handle);
        return OK(CoreError);
    }

    static ReturnCode send(RingBufferHandle handle, const void *data,
                           size_t sizeBytes, Tick timeout = MS_MAX_DELAY) {
        auto result = xRingbufferSend(handle, data, sizeBytes, timeout);
        if (result != pdTRUE) {
            return ERR(CoreError, OperationFailed);
        }
        return OK(CoreError);
    }

    template <typename T>
    static std::expected<std::pair<const T *, size_t>, ReturnCode>
    receive(RingBufferHandle handle, Tick timeout = MS_MAX_DELAY) {
        size_t sizeBytes;
        auto *data = xRingbufferReceive(handle, &sizeBytes, timeout);
        if (data == nullptr) {
            return std::unexpected(ERR(CoreError, Timeout));
        }
        return std::make_pair(reinterpret_cast<const T *>(data), sizeBytes);
    }

    static ReturnCode returnItem(RingBufferHandle handle, void *item) {
        vRingbufferReturnItem(handle, item);
        return OK(CoreError);
    }
};

} // namespace Totem::RingBuffer::detail::platform
