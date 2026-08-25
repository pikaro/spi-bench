// IWYU pragma: private

#pragma once

#include <cstdint>

namespace Totem::Wire::I2C::detail::ina2xx {

enum class Register : uint8_t {
    Configuration = 0x00,
    ShuntVoltage = 0x01,
    BusVoltage = 0x02,
    Power = 0x03,
    Current = 0x04,
    Calibration = 0x05,
    MaskEnable = 0x06,
    AlertLimit = 0x07,
    ManufacturerId = 0xFE,
    DieId = 0xFF,
};

inline constexpr uint16_t ina219DefaultConfiguration = 0x399F;
inline constexpr uint16_t ina226DefaultConfiguration = 0x4127;
inline constexpr uint16_t ina226ManufacturerId = 0x5449;
inline constexpr uint16_t ina226DieIdMask = 0xFFF0;
inline constexpr uint16_t ina226DieId = 0x2260;

inline constexpr uint16_t ina219BusOverflow = 1U << 0U;

inline constexpr uint16_t ina226ShuntOver = 1U << 15U;
inline constexpr uint16_t ina226ShuntUnder = 1U << 14U;
inline constexpr uint16_t ina226BusOver = 1U << 13U;
inline constexpr uint16_t ina226BusUnder = 1U << 12U;
inline constexpr uint16_t ina226AlertFunctionFlag = 1U << 4U;
inline constexpr uint16_t ina226ConversionReadyFlag = 1U << 3U;
inline constexpr uint16_t ina226MathOverflowFlag = 1U << 2U;
inline constexpr uint16_t ina226AlertPolarity = 1U << 1U;
inline constexpr uint16_t ina226AlertLatchEnable = 1U << 0U;

static_assert((ina226ShuntOver & ina226ShuntUnder) == 0);
static_assert((ina226BusOver & ina226BusUnder) == 0);
static_assert(((ina226ShuntOver | ina226ShuntUnder) &
               (ina226BusOver | ina226BusUnder)) == 0);
static_assert((ina226AlertFunctionFlag & ina226MathOverflowFlag) == 0);

} // namespace Totem::Wire::I2C::detail::ina2xx
