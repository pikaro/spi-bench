#pragma once

#include "Platform/Hardware.hpp"
#include "Types/Gpio.hpp"
#include <cstdint>
#include <optional>

namespace Totem::DigitalInput {

struct Config {
    const char *name = "DigitalInput";
    Pin pin{};
    GpioPull pull = GpioPull::Up;

    // When set, work() emits only after the observed level has remained stable
    // for this duration. The raw ISR remains limited to recording the level.
    std::optional<uint32_t> debounceMs = std::nullopt;

    // Polling reconciles a missed interrupt. Disable it for consumers such as
    // quadrature decoders which require a strictly interrupt-ordered stream.
    std::optional<uint32_t> pollIntervalMs = 100;

    [[nodiscard]] constexpr bool validate() const {
        return name != nullptr && name[0] != '\0' &&
               (!debounceMs.has_value() || *debounceMs > 0) &&
               (!pollIntervalMs.has_value() || *pollIntervalMs > 0);
    }
};

} // namespace Totem::DigitalInput
