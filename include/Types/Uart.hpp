#pragma once

#include "Platform/Hardware.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

struct Pins {
    std::optional<::platform::Pin> txPin = std::nullopt;
    std::optional<::platform::Pin> rxPin = std::nullopt;
    std::optional<::platform::Pin> rtsPin = std::nullopt;
    std::optional<::platform::Pin> ctsPin = std::nullopt;
};

enum class BaudRate : uint32_t {
    Baud9600 = 9600,
    Baud19200 = 19200,
    Baud38400 = 38400,
    Baud57600 = 57600,
    Baud115200 = 115200,
    Baud230400 = 230400,
    Baud460800 = 460800,
    Baud921600 = 921600,
};

struct UartConfig {
    BaudRate baudRate = BaudRate::Baud921600;
    uint8_t uartNumber = 0;
    size_t txBufferSize = 2048;
    size_t maxReadLen = 128;
    Pins pins{};

    [[nodiscard]] bool validate() const {
        if (txBufferSize == 0) {
            return false;
        }
        if (maxReadLen == 0) {
            return false;
        }
        return true;
    }

    [[nodiscard]] constexpr uint32_t timeoutFromBytes(size_t len) const {
        return (10     // 8N1 framing = 10 bits per byte
                * 1000 // Convert to ms
                / static_cast<uint32_t>(baudRate)) // Time per byte in ms
               * static_cast<uint32_t>(len);
    }
};
