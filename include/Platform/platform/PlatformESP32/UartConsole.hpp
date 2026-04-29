// IWYU pragma: private

#pragma once

#include "Platform/platform/PlatformESP32/Uart.hpp"
#include "StaticConfig/Console.hpp"
#include "Types/Error.hpp"
#include "driver/gpio.h" // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace platform {

static inline Uart UartConsole;

struct Console {
    static ReturnCode init() { return UartConsole.init(UartConsoleConfig); }

    static ReturnCode write(std::span<const std::byte> data, bool drain = false,
                            std::optional<uint32_t> timeoutMs = std::nullopt) {
        return UartConsole.write(data, drain, timeoutMs);
    }

    static ReturnCode waitTxComplete(uint32_t timeoutMs) {
        return UartConsole.waitTxComplete(timeoutMs);
    }

    static std::expected<size_t, ReturnCode>
    read(std::span<std::byte> buffer,
         std::optional<uint32_t> timeoutMs = std::nullopt) {
        return UartConsole.read(buffer, timeoutMs);
    }

    static ReturnCode deinit() { return UartConsole.deinit(); }
};

} // namespace platform
