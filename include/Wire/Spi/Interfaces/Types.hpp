#pragma once

#include "Platform/Hardware.hpp"
#include <cstdint>
#include <optional>

namespace Totem::Wire::Spi {

enum class BusId : uint8_t {
    Bus2 = 2,
    Bus3 = 3,
};

enum class Mode : uint8_t {
    Mode0 = 0,
    Mode1 = 1,
    Mode2 = 2,
    Mode3 = 3,
};

enum class BitOrder : uint8_t {
    MsbFirst,
    LsbFirst,
};

struct BusPins {
    std::optional<Pin> mosiPin = std::nullopt;
    std::optional<Pin> misoPin = std::nullopt;
    std::optional<Pin> sclkPin = std::nullopt;

    [[nodiscard]] bool validate() const {
        return mosiPin.has_value() && misoPin.has_value() &&
               sclkPin.has_value();
    }
};

} // namespace Totem::Wire::Spi
