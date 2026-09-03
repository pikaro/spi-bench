#pragma once

#include <cstdint>

namespace Totem::GpioSignalTest::detail::timing {

inline constexpr uint32_t microsecondsPerSecond = 1'000'000U;
inline constexpr uint32_t partsPerThousand = 1'000U;

[[nodiscard]] constexpr uint32_t periodUs(uint32_t frequencyHz) {
    return frequencyHz == 0 ? 0 : microsecondsPerSecond / frequencyHz;
}

[[nodiscard]] constexpr uint32_t highTimeUs(uint32_t period, uint16_t duty) {
    return static_cast<uint32_t>((static_cast<uint64_t>(period) * duty) /
                                 partsPerThousand);
}

[[nodiscard]] constexpr uint32_t frequencyMilliHz(uint32_t period) {
    return period == 0 ? 0
                       : static_cast<uint32_t>(
                             (1'000'000'000ULL + (period / 2U)) / period);
}

[[nodiscard]] constexpr uint16_t dutyPartsPerThousand(uint32_t highTime,
                                                      uint32_t lowTime) {
    const auto total = static_cast<uint64_t>(highTime) + lowTime;
    return total == 0
               ? 0
               : static_cast<uint16_t>(
                     ((static_cast<uint64_t>(highTime) * partsPerThousand) +
                      (total / 2U)) /
                     total);
}

[[nodiscard]] constexpr uint32_t absoluteDifference(uint32_t lhs,
                                                    uint32_t rhs) {
    return lhs >= rhs ? lhs - rhs : rhs - lhs;
}

[[nodiscard]] constexpr uint32_t errorPartsPerThousand(uint32_t observed,
                                                       uint32_t expected) {
    return expected == 0 ? 0
                         : static_cast<uint32_t>(
                               (static_cast<uint64_t>(
                                    absoluteDifference(observed, expected)) *
                                partsPerThousand) /
                               expected);
}

} // namespace Totem::GpioSignalTest::detail::timing
