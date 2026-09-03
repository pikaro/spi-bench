#pragma once

#include "Generic/InlineCallback.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Gpio.hpp"
#include "Wire/I2C/Interfaces/MasterConfig.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace Totem::Wire::I2C {

enum class Ina2xxModel : uint8_t {
    Ina219,
    Ina226,
    Ina228,
};

enum class Ina2xxCapability : uint32_t {
    ShuntVoltage = 1U << 0U,
    BusVoltage = 1U << 1U,
    Current = 1U << 2U,
    Power = 1U << 3U,
    HardwareAlert = 1U << 4U,
    Identity = 1U << 5U,
    Temperature = 1U << 6U,
    Energy = 1U << 7U,
    Charge = 1U << 8U,
};

enum class Ina2xxLimitState : uint8_t {
    AbsoluteUnder,
    PracticalUnder,
    Normal,
    PracticalOver,
    AbsoluteOver,
};

enum class Ina2xxLimitSeverity : uint8_t {
    Normal,
    Warning,
    Error,
};

enum class Ina2xxLimitMeasurement : uint8_t {
    BusVoltage,
    Current,
};

enum class Ina2xxAlertFunction : uint8_t {
    BusVoltageOver,
    BusVoltageUnder,
    CurrentOver,
    CurrentUnder,
};

struct Ina2xxBusVoltageWindow {
    uint32_t absoluteMinMillivolts = 0;
    uint32_t practicalMinMillivolts = 0;
    uint32_t practicalMaxMillivolts = 24000;
    uint32_t absoluteMaxMillivolts = 26000;

    [[nodiscard]] constexpr bool validate() const {
        return absoluteMinMillivolts <= practicalMinMillivolts &&
               practicalMinMillivolts <= practicalMaxMillivolts &&
               practicalMaxMillivolts <= absoluteMaxMillivolts &&
               absoluteMinMillivolts < absoluteMaxMillivolts;
    }
};

struct Ina2xxCurrentWindow {
    int32_t absoluteMinMicroamps = -3200000;
    int32_t practicalMinMicroamps = -3000000;
    int32_t practicalMaxMicroamps = 3000000;
    int32_t absoluteMaxMicroamps = 3200000;

    [[nodiscard]] constexpr bool validate() const {
        return absoluteMinMicroamps <= practicalMinMicroamps &&
               practicalMinMicroamps <= practicalMaxMicroamps &&
               practicalMaxMicroamps <= absoluteMaxMicroamps &&
               absoluteMinMicroamps < absoluteMaxMicroamps;
    }
};

struct Ina2xxHardwareAlertConfig {
    Pin pin{};
    Ina2xxAlertFunction function = Ina2xxAlertFunction::CurrentOver;
    bool activeHigh = false;
    bool latched = true;
};

struct Ina2xxConfig {
    DeviceConfig device{
        .address = 0x40,
    };
    uint32_t sampleIntervalMs = 1000;
    uint32_t shuntMicroOhms = 10000;
    uint32_t expectedMaxCurrentMicroamps = 3200000;
    Ina2xxBusVoltageWindow busVoltage{};
    Ina2xxCurrentWindow current{};
    std::optional<Ina2xxHardwareAlertConfig> hardwareAlert = std::nullopt;

    // Must be unique per live sensor instance. It is copied by the driver.
    const char *metricsGroupName = "ina2xx";

    [[nodiscard]] constexpr bool validate() const {
        return device.validate() && device.addressBits == AddressBits::Seven &&
               device.address >= 0x40 && device.address <= 0x4F &&
               sampleIntervalMs > 0 && shuntMicroOhms > 0 &&
               expectedMaxCurrentMicroamps > 0 && busVoltage.validate() &&
               current.validate() && metricsGroupName != nullptr &&
               metricsGroupName[0] != '\0' &&
               std::string_view{metricsGroupName}.size() <=
                   MetricConfig::maxMetricGroupNameLength;
    }
};

struct Ina2xxSample {
    int32_t shuntMicrovolts = 0;
    uint32_t busMillivolts = 0;
    int32_t currentMicroamps = 0;
    int32_t powerMilliwatts = 0;

    // INA219/INA226 leave these values zero and their validity bits clear.
    // The protected INA228 backend will set each bit only when that individual
    // sample contains a successfully acquired value; zero is otherwise data,
    // not an availability sentinel.
    int32_t temperatureMillicelsius = 0;
    uint32_t energyMillijoules = 0;
    int32_t chargeMillicoulombs = 0;

    uint32_t capturedAtMs = 0;
    uint32_t validCapabilities = 0;
};

static_assert(std::is_trivially_copyable_v<Ina2xxSample>);

struct Ina2xxLimitEvent {
    Ina2xxLimitMeasurement measurement = Ina2xxLimitMeasurement::BusVoltage;
    Ina2xxLimitState previous = Ina2xxLimitState::Normal;
    Ina2xxLimitState current = Ina2xxLimitState::Normal;
    Ina2xxLimitSeverity severity = Ina2xxLimitSeverity::Normal;
    Ina2xxSample sample{};
};

struct Ina2xxAlertEvent {
    Ina2xxAlertFunction function = Ina2xxAlertFunction::CurrentOver;
    Ina2xxSample sample{};
};

using Ina2xxLimitCallback = Generic::InlineCallback<const Ina2xxLimitEvent &>;
using Ina2xxAlertCallback = Generic::InlineCallback<const Ina2xxAlertEvent &>;
using Ina2xxSampleCallback = Generic::InlineCallback<const Ina2xxSample &>;

[[nodiscard]] constexpr uint32_t
ina2xx_capability_mask(Ina2xxCapability capability) {
    return static_cast<uint32_t>(capability);
}

[[nodiscard]] constexpr uint32_t ina2xx_capabilities(Ina2xxModel model) {
    constexpr auto measurements =
        ina2xx_capability_mask(Ina2xxCapability::ShuntVoltage) |
        ina2xx_capability_mask(Ina2xxCapability::BusVoltage) |
        ina2xx_capability_mask(Ina2xxCapability::Current) |
        ina2xx_capability_mask(Ina2xxCapability::Power);
    switch (model) {
    case Ina2xxModel::Ina219:
        return measurements;
    case Ina2xxModel::Ina226:
        return measurements |
               ina2xx_capability_mask(Ina2xxCapability::HardwareAlert) |
               ina2xx_capability_mask(Ina2xxCapability::Identity);
    case Ina2xxModel::Ina228:
        return measurements |
               ina2xx_capability_mask(Ina2xxCapability::HardwareAlert) |
               ina2xx_capability_mask(Ina2xxCapability::Identity) |
               ina2xx_capability_mask(Ina2xxCapability::Temperature) |
               ina2xx_capability_mask(Ina2xxCapability::Energy) |
               ina2xx_capability_mask(Ina2xxCapability::Charge);
    }
    return 0;
}

[[nodiscard]] constexpr bool ina2xx_supports(Ina2xxModel model,
                                             Ina2xxCapability capability) {
    return (ina2xx_capabilities(model) & ina2xx_capability_mask(capability)) !=
           0;
}

static_assert(!ina2xx_supports(Ina2xxModel::Ina219,
                               Ina2xxCapability::HardwareAlert));
static_assert(ina2xx_supports(Ina2xxModel::Ina226,
                              Ina2xxCapability::HardwareAlert));
static_assert(!ina2xx_supports(Ina2xxModel::Ina226,
                               Ina2xxCapability::Temperature));
static_assert(ina2xx_supports(Ina2xxModel::Ina228,
                              Ina2xxCapability::Temperature));
static_assert(ina2xx_supports(Ina2xxModel::Ina228, Ina2xxCapability::Energy));
static_assert(ina2xx_supports(Ina2xxModel::Ina228, Ina2xxCapability::Charge));

} // namespace Totem::Wire::I2C
