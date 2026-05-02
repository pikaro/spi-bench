#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#pragma push_macro("BIT_MASK")
#undef BIT_MASK
#include "inc/CRC.h"
#pragma pop_macro("BIT_MASK")

namespace platform::crc {

struct PlatformGeneric {
    [[nodiscard]] static uint8_t crc8(std::span<const std::byte> data) {
        static const auto table = ::CRC::CRC_8().MakeTable();
        return ::CRC::Calculate(data.data(), data.size(), table);
    }

    [[nodiscard]] static uint8_t crc8(std::span<const uint8_t> data) {
        return crc8(std::as_bytes(data));
    }

    [[nodiscard]] static uint32_t crc32(std::span<const std::byte> data) {
        static const auto table = ::CRC::CRC_32().MakeTable();
        return ::CRC::Calculate(data.data(), data.size(), table);
    }

    [[nodiscard]] static uint32_t crc32(std::span<const uint8_t> data) {
        return crc32(std::as_bytes(data));
    }
};

} // namespace platform::crc
