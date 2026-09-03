#pragma once

#include "Generated/Wire/Support.hpp"
#include "BatteryMonitor/Interfaces/Wire.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::BatteryMonitor::BatteryStatusEvent> {
    using Type = ::Totem::BatteryMonitor::BatteryStatusEvent;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::stateOfChargePartsPerThousand>{"stateOfChargePartsPerThousand"},
        Field<&Type::sourceState>{"sourceState"},
        Field<&Type::measurementFreshness>{"measurementFreshness"},
        Field<&Type::confidence>{"confidence"}
    );
};

} // namespace Totem::Generated::Wire
