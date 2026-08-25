// IWYU pragma: private

#pragma once

#include "Types/Error.hpp"
#include "Wire/I2C/Interfaces/Ina2xxConfig.hpp"
#include <cstdint>
#include <expected>
#include <limits>

namespace Totem::Wire::I2C::detail::ina2xx {

struct Calibration {
    uint16_t registerValue = 0;
    uint32_t currentLsbMicroamps = 0;
};

template <typename Value>
[[nodiscard]] constexpr Ina2xxLimitState
classify(Value value, Value absoluteMin, Value practicalMin, Value practicalMax,
         Value absoluteMax) {
    if (value < absoluteMin) {
        return Ina2xxLimitState::AbsoluteUnder;
    }
    if (value < practicalMin) {
        return Ina2xxLimitState::PracticalUnder;
    }
    if (value > absoluteMax) {
        return Ina2xxLimitState::AbsoluteOver;
    }
    if (value > practicalMax) {
        return Ina2xxLimitState::PracticalOver;
    }
    return Ina2xxLimitState::Normal;
}

[[nodiscard]] constexpr Ina2xxLimitState
classify_bus_voltage(uint32_t millivolts,
                     const Ina2xxBusVoltageWindow &window) {
    return classify(
        millivolts, window.absoluteMinMillivolts, window.practicalMinMillivolts,
        window.practicalMaxMillivolts, window.absoluteMaxMillivolts);
}

[[nodiscard]] constexpr Ina2xxLimitState
classify_current(int32_t microamps, const Ina2xxCurrentWindow &window) {
    return classify(microamps, window.absoluteMinMicroamps,
                    window.practicalMinMicroamps, window.practicalMaxMicroamps,
                    window.absoluteMaxMicroamps);
}

[[nodiscard]] constexpr Ina2xxLimitSeverity severity(Ina2xxLimitState state) {
    switch (state) {
    case Ina2xxLimitState::AbsoluteUnder:
    case Ina2xxLimitState::AbsoluteOver:
        return Ina2xxLimitSeverity::Error;
    case Ina2xxLimitState::PracticalUnder:
    case Ina2xxLimitState::PracticalOver:
        return Ina2xxLimitSeverity::Warning;
    case Ina2xxLimitState::Normal:
        return Ina2xxLimitSeverity::Normal;
    }
    return Ina2xxLimitSeverity::Error;
}

[[nodiscard]] constexpr int32_t ina219_shunt_microvolts(int16_t raw) {
    return static_cast<int32_t>(raw) * 10;
}

[[nodiscard]] constexpr int32_t ina226_shunt_microvolts(int16_t raw) {
    return (static_cast<int32_t>(raw) * 5) / 2;
}

[[nodiscard]] constexpr uint32_t ina219_bus_millivolts(uint16_t raw) {
    return static_cast<uint32_t>(raw >> 3U) * 4U;
}

[[nodiscard]] constexpr uint32_t ina226_bus_millivolts(uint16_t raw) {
    return (static_cast<uint32_t>(raw) * 5U) / 4U;
}

[[nodiscard]] constexpr uint16_t ina219_gain(uint32_t requiredMicrovolts) {
    if (requiredMicrovolts <= 39990) {
        return 0;
    }
    if (requiredMicrovolts <= 79990) {
        return 1;
    }
    if (requiredMicrovolts <= 159990) {
        return 2;
    }
    return 3;
}

[[nodiscard]] constexpr int16_t ina226_shunt_alert_raw(int32_t microvolts) {
    const auto scaled = static_cast<int64_t>(microvolts) * 2;
    const auto rounded = scaled >= 0 ? (scaled + 2) / 5 : (scaled - 2) / 5;
    return static_cast<int16_t>(rounded);
}

[[nodiscard]] constexpr uint16_t ina226_bus_alert_raw(uint32_t millivolts) {
    return static_cast<uint16_t>((static_cast<uint32_t>(millivolts) * 4U + 2U) /
                                 5U);
}

[[nodiscard]] constexpr std::expected<uint32_t, CoreError>
expected_shunt_microvolts(uint32_t expectedMaxCurrentMicroamps,
                          uint32_t shuntMicroOhms) {
    if (expectedMaxCurrentMicroamps == 0 || shuntMicroOhms == 0) {
        return std::unexpected(CoreError::InvalidArgument);
    }
    const auto product =
        static_cast<uint64_t>(expectedMaxCurrentMicroamps) * shuntMicroOhms;
    const auto microvolts = (product + 999999U) / 1000000U;
    if (microvolts > UINT32_MAX) {
        return std::unexpected(CoreError::Overflow);
    }
    return static_cast<uint32_t>(microvolts);
}

[[nodiscard]] constexpr std::expected<int32_t, CoreError>
current_to_shunt_microvolts(int32_t currentMicroamps, uint32_t shuntMicroOhms) {
    const auto product =
        static_cast<int64_t>(currentMicroamps) * shuntMicroOhms;
    const auto rounded = product >= 0 ? (product + 500000) / 1000000
                                      : (product - 500000) / 1000000;
    if (rounded < std::numeric_limits<int32_t>::min() ||
        rounded > std::numeric_limits<int32_t>::max()) {
        return std::unexpected(CoreError::Overflow);
    }
    return static_cast<int32_t>(rounded);
}

[[nodiscard]] constexpr std::expected<Calibration, CoreError>
make_calibration(Ina2xxModel model, uint32_t shuntMicroOhms,
                 uint32_t expectedMaxCurrentMicroamps) {
    if (shuntMicroOhms == 0 || expectedMaxCurrentMicroamps == 0) {
        return std::unexpected(CoreError::InvalidArgument);
    }

    constexpr uint32_t positiveRawRange = 32767;
    const auto rangeCurrentLsb = static_cast<uint32_t>(
        (static_cast<uint64_t>(expectedMaxCurrentMicroamps) + positiveRawRange -
         1U) /
        positiveRawRange);
    const uint64_t numerator = model == Ina2xxModel::Ina219
                                   ? UINT64_C(40960000000)
                                   : UINT64_C(5120000000);
    const uint32_t maximumCalibration =
        model == Ina2xxModel::Ina219 ? 0xFFFEU : 0x7FFFU;
    const auto calibrationDenominator =
        static_cast<uint64_t>(maximumCalibration) * shuntMicroOhms;
    const auto calibrationCurrentLsb = static_cast<uint32_t>(
        (numerator + calibrationDenominator - 1U) / calibrationDenominator);
    const auto currentLsb = rangeCurrentLsb > calibrationCurrentLsb
                                ? rangeCurrentLsb
                                : calibrationCurrentLsb;
    if (currentLsb == 0 ||
        currentLsb >
            static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) /
                positiveRawRange) {
        return std::unexpected(CoreError::Overflow);
    }

    const auto denominator = static_cast<uint64_t>(currentLsb) * shuntMicroOhms;
    auto registerValue = numerator / denominator;
    if (model == Ina2xxModel::Ina219) {
        registerValue &= UINT64_C(0xFFFE);
    }
    if (registerValue == 0 || registerValue > maximumCalibration) {
        return std::unexpected(CoreError::InvalidArgument);
    }
    return Calibration{
        .registerValue = static_cast<uint16_t>(registerValue),
        .currentLsbMicroamps = currentLsb,
    };
}

static_assert(ina219_shunt_microvolts(-1) == -10);
static_assert(ina226_shunt_microvolts(-1) == -2);
static_assert(ina226_shunt_microvolts(2) == 5);
static_assert(ina219_bus_millivolts(0x0008) == 4);
static_assert(ina226_bus_millivolts(4) == 5);
static_assert(ina219_gain(32000) == 0);
static_assert(ina219_gain(40000) == 1);
static_assert(ina226_shunt_alert_raw(10) == 4);
static_assert(ina226_shunt_alert_raw(-10) == -4);
static_assert(ina226_bus_alert_raw(30000) == 24000);
static_assert(*expected_shunt_microvolts(2000000, 2000) == 4000);
static_assert(*current_to_shunt_microvolts(2000000, 2000) == 4000);
static_assert(*current_to_shunt_microvolts(-2000000, 2000) == -4000);
static_assert(classify_bus_voltage(0, Ina2xxBusVoltageWindow{}) ==
              Ina2xxLimitState::Normal);
static_assert(classify_bus_voltage(26001, Ina2xxBusVoltageWindow{}) ==
              Ina2xxLimitState::AbsoluteOver);
static_assert(classify_current(0, Ina2xxCurrentWindow{}) ==
              Ina2xxLimitState::Normal);
static_assert(classify_current(-3000001, Ina2xxCurrentWindow{}) ==
              Ina2xxLimitState::PracticalUnder);
static_assert(classify_current(-3200001, Ina2xxCurrentWindow{}) ==
              Ina2xxLimitState::AbsoluteUnder);
static_assert(classify_current(3000001, Ina2xxCurrentWindow{}) ==
              Ina2xxLimitState::PracticalOver);
static_assert(classify_current(3200001, Ina2xxCurrentWindow{}) ==
              Ina2xxLimitState::AbsoluteOver);
static_assert(
    make_calibration(Ina2xxModel::Ina219, 10000, 3200000).has_value());
static_assert(
    make_calibration(Ina2xxModel::Ina226, 10000, 3200000).has_value());
static_assert(make_calibration(Ina2xxModel::Ina226, 2000, 2000000)
                  ->registerValue == 32405);
static_assert(make_calibration(Ina2xxModel::Ina226, 2000, 2000000)
                  ->currentLsbMicroamps == 79);

} // namespace Totem::Wire::I2C::detail::ina2xx
