#pragma once

#include "esp_crc.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace platform::crc {

namespace detail {

[[nodiscard]] inline const uint8_t *
bytes(std::span<const std::byte> data) noexcept {
    return reinterpret_cast<const uint8_t *>(data.data());
}

[[nodiscard]] inline uint32_t espLength(std::size_t size) noexcept {
    return static_cast<uint32_t>(size);
}

} // namespace detail

struct PlatformESP32 {
    [[nodiscard]] static uint8_t crc8(std::span<const std::byte> data) {
        // CRCpp::CRC_8 is CRC-8/SMBus: poly 0x07, init 0, no reflection,
        // xorout 0. ESP's helpers complement state internally, so this
        // call/return pairing preserves the existing wire value.
        return static_cast<uint8_t>(
            ~::esp_crc8_be(std::numeric_limits<uint8_t>::max(),
                           detail::bytes(data), detail::espLength(data.size())));
    }

    [[nodiscard]] static uint8_t crc8(std::span<const uint8_t> data) {
        return crc8(std::as_bytes(data));
    }

    [[nodiscard]] static uint32_t crc32(std::span<const std::byte> data) {
        return ::esp_crc32_le(0, detail::bytes(data),
                              detail::espLength(data.size()));
    }

    [[nodiscard]] static uint32_t crc32(std::span<const uint8_t> data) {
        return crc32(std::as_bytes(data));
    }
};

} // namespace platform::crc
