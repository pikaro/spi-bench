#pragma once

#include "GpioSignalTest/Interfaces/Types.hpp"
#include "GpioSignalTest/detail/Timing.hpp"
#include "Types/Gpio.hpp"
#include <cstdint>
#include <magic_enum/magic_enum.hpp>

namespace Totem::GpioSignalTest {

struct Config {
    const char *name = "GpioSignalTest";
    Role role = Role::Consumer;
    Pin pin{};

    // Producer rate, or the rate a consumer expects to measure.
    uint32_t frequencyHz = 10;
    // 500 is a 50% duty cycle.
    uint16_t dutyPartsPerThousand = 500;

    uint32_t reportIntervalMs = 5'000;
    // Relative frequency error and cumulative period spread limits.
    uint16_t frequencyTolerancePartsPerThousand = 20;
    // Absolute duty-cycle difference, where 50 is five percentage points.
    uint16_t dutyTolerancePartsPerThousand = 50;
    uint16_t periodSpreadTolerancePartsPerThousand = 50;
    uint8_t stalePeriodCount = 3;
    GpioPull consumerPull = GpioPull::None;
    bool dumpPinConfiguration = true;

    [[nodiscard]] constexpr bool validate() const {
        const auto period = detail::timing::periodUs(frequencyHz);
        const auto highTime =
            detail::timing::highTimeUs(period, dutyPartsPerThousand);
        const auto lowTime = period - highTime;
        return name != nullptr && name[0] != '\0' &&
               magic_enum::enum_contains(role) && frequencyHz > 0 &&
               frequencyHz <= 1'000 && dutyPartsPerThousand >= 100 &&
               dutyPartsPerThousand <= 900 && reportIntervalMs > 0 &&
               frequencyTolerancePartsPerThousand <= 1'000 &&
               dutyTolerancePartsPerThousand <= 1'000 &&
               periodSpreadTolerancePartsPerThousand <= 1'000 &&
               stalePeriodCount > 0 && highTime >= 100 && lowTime >= 100;
    }
};

} // namespace Totem::GpioSignalTest
