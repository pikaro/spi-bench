#pragma once

#include "BatteryMonitor/Interfaces/Types.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::BatteryMonitor {

/** Compact battery-estimate snapshot published by the battery owner node. */
struct WIRE_MSG BatteryStatusEvent {
    uint16_t stateOfChargePartsPerThousand = 0;
    BatterySourceState sourceState = BatterySourceState::Unknown;
    BatteryMeasurementFreshness measurementFreshness =
        BatteryMeasurementFreshness::NeverReceived;
    BatteryEstimateConfidence confidence =
        BatteryEstimateConfidence::Unavailable;
};

[[nodiscard]] inline constexpr bool
hasUsableStateOfCharge(const BatteryStatusEvent &event) {
    const bool usableSource =
        event.sourceState == BatterySourceState::PracticalUnder ||
        event.sourceState == BatterySourceState::Normal ||
        event.sourceState == BatterySourceState::PracticalOver;
    return event.stateOfChargePartsPerThousand <= 1'000U && usableSource &&
           event.measurementFreshness == BatteryMeasurementFreshness::Fresh &&
           event.confidence != BatteryEstimateConfidence::Unavailable;
}

static_assert(std::is_trivially_copyable_v<BatteryStatusEvent>);

} // namespace Totem::BatteryMonitor
