// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "RingBuffer/detail/Config.hpp"
#include "Types/Error.hpp"
#include "freertos/projdefs.h"
#include "freertos/ringbuf.h"
#include <array>
#include <cstddef>
#include <cstdint>
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

    template <size_t N> struct Storage {
        std::array<uint8_t, N> buffer{};
        StaticRingbuffer_t ringBuffer{};
    };

    static std::expected<RingBufferHandle, ReturnCode> create(
        size_t size,
        const PlatformConfigAbstraction &config = PlatformConfigAbstraction()) {
        auto platformConfig = PlatformConfig::fromAbstraction(config);
        auto *handle = xRingbufferCreate(size, platformConfig.type);
        if (handle == nullptr) {
            _log_e("Failed to create ring buffer of size %zu", size);
            return std::unexpected(ERR(OperationFailed));
        }
        return handle;
    }

    template <size_t N>
    static std::expected<RingBufferHandle, ReturnCode> create(
        Storage<N> &storage, size_t size,
        const PlatformConfigAbstraction &config = PlatformConfigAbstraction()) {
        if (size == 0 || size > storage.buffer.size()) {
            _log_e("Invalid static ring buffer size %zu of capacity %zu", size,
                   storage.buffer.size());
            return std::unexpected(ERR(InvalidArgument));
        }
        auto platformConfig = PlatformConfig::fromAbstraction(config);
        auto *handle = xRingbufferCreateStatic(
            size, platformConfig.type, storage.buffer.data(),
            &storage.ringBuffer);
        if (handle == nullptr) {
            _log_e("Failed to create static ring buffer of size %zu", size);
            return std::unexpected(ERR(OperationFailed));
        }
        return handle;
    }

    static ReturnCode destroy(RingBufferHandle handle) {
        vRingbufferDelete(handle);
        return OK();
    }

    static ReturnCode send(RingBufferHandle handle, const void *data,
                           size_t sizeBytes, Tick timeout = TICK_MAX_DELAY) {
        auto result = xRingbufferSend(handle, data, sizeBytes, timeout);
        if (result != pdTRUE) {
            return ERR(Timeout);
        }
        return OK();
    }

    template <typename T>
    static std::expected<std::pair<const T *, size_t>, ReturnCode>
    receive(RingBufferHandle handle, Tick timeout = TICK_MAX_DELAY) {
        size_t sizeBytes;
        auto *data = xRingbufferReceive(handle, &sizeBytes, timeout);
        if (data == nullptr) {
            return std::unexpected(ERR(Timeout));
        }
        return std::make_pair(reinterpret_cast<const T *>(data), sizeBytes);
    }

    static ReturnCode returnItem(RingBufferHandle handle, void *item) {
        vRingbufferReturnItem(handle, item);
        return OK();
    }
};

} // namespace Totem::RingBuffer::detail::platform
