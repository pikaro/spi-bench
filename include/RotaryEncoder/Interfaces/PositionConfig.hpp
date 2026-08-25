#pragma once

#include "RotaryEncoder/Interfaces/Types.hpp"
#include <cstdint>
#include <limits>
#include <optional>

namespace Totem::RotaryEncoder {

/** Optional bounds and reset value for a rotary position counter. */
struct PositionConfig {
    int32_t initialValue = 0;
    std::optional<int32_t> minimum = std::nullopt;
    std::optional<int32_t> maximum = std::nullopt;

    [[nodiscard]] constexpr bool validate() const {
        return (!minimum.has_value() || !maximum.has_value() ||
                *minimum <= *maximum) &&
               (!minimum.has_value() || initialValue >= *minimum) &&
               (!maximum.has_value() || initialValue <= *maximum);
    }

    /** Return the next accepted position, or nullopt at a configured bound. */
    [[nodiscard]] constexpr std::optional<int32_t>
    advance(int32_t current, Direction direction) const {
        if (direction == Direction::Clockwise) {
            if ((maximum.has_value() && current >= *maximum) ||
                current == std::numeric_limits<int32_t>::max()) {
                return std::nullopt;
            }
            return current + 1;
        }

        if ((minimum.has_value() && current <= *minimum) ||
            current == std::numeric_limits<int32_t>::min()) {
            return std::nullopt;
        }
        return current - 1;
    }
};

} // namespace Totem::RotaryEncoder
