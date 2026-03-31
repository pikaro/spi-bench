#pragma once

#include <cstdint>

namespace Totem::TaskControllerRegistry::detail {

struct Config {
    static constexpr uint8_t controllerCountMax = 10;
    static constexpr uint8_t controllerNameMaxLen = 32;
    static constexpr uint32_t stopKillDelayMs = 1000;

    [[nodiscard]] static bool validate() { return true; }
};

} // namespace Totem::TaskControllerRegistry::detail
