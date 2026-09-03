// IWYU pragma: private

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Totem::BatteryMonitor::detail {

inline constexpr uint64_t chargeTrapezoidDivisor = 7'200'000'000ULL;
inline constexpr uint64_t energyTrapezoidDivisor = 7'200'000ULL;

struct FixedPointDelta {
    uint32_t whole = 0;
    uint64_t remainder = 0;
    bool overflow = false;
};

[[nodiscard]] constexpr uint32_t elapsedMs(uint32_t nowMs,
                                           uint32_t previousMs) {
    return static_cast<uint32_t>(nowMs - previousMs);
}

[[nodiscard]] constexpr bool chargingDetected(int32_t currentMicroamps,
                                              uint32_t deadbandMicroamps) {
    return currentMicroamps < -static_cast<int32_t>(deadbandMicroamps);
}

[[nodiscard]] constexpr bool dischargingDetected(int32_t currentMicroamps,
                                                 uint32_t deadbandMicroamps) {
    return currentMicroamps > static_cast<int32_t>(deadbandMicroamps);
}

[[nodiscard]] constexpr bool sampleGapExceeded(uint32_t elapsed,
                                               uint32_t tolerance) {
    return elapsed > tolerance;
}

[[nodiscard]] constexpr FixedPointDelta
integratePositiveTrapezoid(uint32_t previous, uint32_t current,
                           uint32_t deltaMs, uint64_t remainder,
                           uint64_t divisor) {
    if (divisor == 0 || remainder >= divisor) {
        return {.overflow = true};
    }
    const uint64_t sum = static_cast<uint64_t>(previous) + current;
    if (deltaMs != 0 &&
        sum > (std::numeric_limits<uint64_t>::max() - remainder) / deltaMs) {
        return {.overflow = true};
    }
    const uint64_t numerator = sum * deltaMs + remainder;
    const uint64_t whole = numerator / divisor;
    if (whole > std::numeric_limits<uint32_t>::max()) {
        return {.overflow = true};
    }
    return {
        .whole = static_cast<uint32_t>(whole),
        .remainder = numerator % divisor,
    };
}

[[nodiscard]] constexpr uint32_t saturatingAdd(uint32_t left, uint32_t right) {
    return right > std::numeric_limits<uint32_t>::max() - left
               ? std::numeric_limits<uint32_t>::max()
               : left + right;
}

[[nodiscard]] constexpr uint32_t
linearSocPartsPerThousand(uint32_t voltageMillivolts, uint32_t emptyMillivolts,
                          uint32_t fullMillivolts) {
    if (fullMillivolts <= emptyMillivolts ||
        voltageMillivolts <= emptyMillivolts) {
        return 0;
    }
    if (voltageMillivolts >= fullMillivolts) {
        return 1'000;
    }
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(voltageMillivolts - emptyMillivolts) * 1'000U) /
        (fullMillivolts - emptyMillivolts));
}

[[nodiscard]] constexpr uint32_t remainingFromSoc(uint32_t total,
                                                  uint32_t socPpt) {
    return static_cast<uint32_t>((static_cast<uint64_t>(total) *
                                  std::min<uint32_t>(socPpt, uint32_t{1'000})) /
                                 1'000U);
}

[[nodiscard]] constexpr uint32_t timeToEmptyMinutes(uint32_t remainingMwh,
                                                    uint32_t averageMw) {
    if (averageMw == 0) {
        return 0;
    }
    const uint64_t minutes =
        (static_cast<uint64_t>(remainingMwh) * 60U) / averageMw;
    return minutes > std::numeric_limits<uint32_t>::max()
               ? std::numeric_limits<uint32_t>::max()
               : static_cast<uint32_t>(minutes);
}

[[nodiscard]] constexpr uint32_t integrateConstantTrace(uint32_t value,
                                                        uint32_t intervalMs,
                                                        uint32_t intervalCount,
                                                        uint64_t divisor) {
    uint32_t total = 0;
    uint64_t remainder = 0;
    for (uint32_t index = 0; index < intervalCount; ++index) {
        const auto delta = integratePositiveTrapezoid(value, value, intervalMs,
                                                      remainder, divisor);
        if (delta.overflow) {
            return std::numeric_limits<uint32_t>::max();
        }
        total = saturatingAdd(total, delta.whole);
        remainder = delta.remainder;
    }
    return total;
}

static_assert(elapsedMs(0x10U, 0xFFFFFFF0U) == 32U);
static_assert(chargingDetected(-5'001, 5'000));
static_assert(!chargingDetected(-5'000, 5'000));
static_assert(!dischargingDetected(5'000, 5'000));
static_assert(dischargingDetected(5'001, 5'000));
static_assert(!sampleGapExceeded(2'000, 2'000));
static_assert(sampleGapExceeded(2'001, 2'000));
static_assert(linearSocPartsPerThousand(21'000, 21'000, 29'400) == 0);
static_assert(linearSocPartsPerThousand(29'400, 21'000, 29'400) == 1'000);
static_assert(linearSocPartsPerThousand(25'200, 21'000, 29'400) == 500);
static_assert(remainingFromSoc(10'000, 500) == 5'000);
static_assert(timeToEmptyMinutes(1'000, 500) == 120);
static_assert(timeToEmptyMinutes(1'000, 0) == 0);
static_assert(saturatingAdd(std::numeric_limits<uint32_t>::max(), 1) ==
              std::numeric_limits<uint32_t>::max());
static_assert(integratePositiveTrapezoid(1, 1, 1, chargeTrapezoidDivisor,
                                         chargeTrapezoidDivisor)
                  .overflow);
static_assert(integratePositiveTrapezoid(1'000'000, 1'000'000, 3'600'000, 0,
                                         chargeTrapezoidDivisor)
                  .whole == 1'000);
static_assert(integratePositiveTrapezoid(1'000, 1'000, 3'600'000, 0,
                                         energyTrapezoidDivisor)
                  .whole == 1'000);
static_assert(integrateConstantTrace(1'000'000, 1'000, 3'600,
                                     chargeTrapezoidDivisor) == 1'000);
static_assert(integrateConstantTrace(1'000, 1'000, 3'600,
                                     energyTrapezoidDivisor) == 1'000);

} // namespace Totem::BatteryMonitor::detail
