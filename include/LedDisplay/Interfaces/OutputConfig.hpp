#pragma once

#include "Platform/Hardware.hpp"
#include <cstdint>
#include <optional>

namespace Totem::LedDisplay {

enum class Sk9822WireColorOrder : uint8_t {
    Rgb,
    Rbg,
    Grb,
    Gbr,
    Brg,
    Bgr,
};

enum class Sk9822SpiHost : uint8_t {
    Spi3,
};

struct Sk9822OutputConfig {
    Sk9822SpiHost host = Sk9822SpiHost::Spi3;
    std::optional<Pin> dataPin = std::nullopt;
    std::optional<Pin> clockPin = std::nullopt;
    uint32_t clockHz = 4'000'000;
    uint32_t transferTimeoutMs = 10;
    Sk9822WireColorOrder colorOrder = Sk9822WireColorOrder::Bgr;

    [[nodiscard]] bool validate() const {
        return host == Sk9822SpiHost::Spi3 && dataPin.has_value() &&
               clockPin.has_value() && dataPin != clockPin && clockHz > 0 &&
               transferTimeoutMs > 0;
    }
};

} // namespace Totem::LedDisplay
