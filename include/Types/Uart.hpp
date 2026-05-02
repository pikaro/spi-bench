#pragma once

#include "Platform/Hardware.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

enum class UartEventType : uint8_t {
    Data,
    Overflow,
    Error,
    Break,
    Pattern,
    Unknown,
};

struct UartEvent {
    UartEventType type = UartEventType::Unknown;
    size_t size = 0;
};

using UartEventCallback = ReturnCode (*)(void *owner, UartEvent event);

struct UartPins {
    std::optional<Pin> txPin = std::nullopt;
    std::optional<Pin> rxPin = std::nullopt;
    std::optional<Pin> rtsPin = std::nullopt;
    std::optional<Pin> ctsPin = std::nullopt;
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
    UartPins pins{};

    [[nodiscard]] bool validate() const {
        if (txBufferSize == 0) {
            return false;
        }
        if (maxReadLen == 0) {
            return false;
        }
        if (uartNumber > 0) {
            if (!pins.txPin.has_value() || !pins.rxPin.has_value()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr uint32_t timeoutFromBytes(size_t len) const {
        if (len == 0) {
            return 0;
        }
        constexpr uint32_t bitsPerByte = 10; // 8N1 framing
        const auto baud = static_cast<uint32_t>(baudRate);
        const auto bitMs = bitsPerByte * 1000 * static_cast<uint32_t>(len);
        return (bitMs + baud - 1) / baud;
    }
};
