#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedPwm::Glitter> {
    using Type = ::Totem::LedPwm::Glitter;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::base>{"base"},
        Field<&Type::glimmerPeak>{"glimmerPeak"},
        Field<&Type::sparklePeak>{"sparklePeak"},
        Field<&Type::stepMs>{"stepMs"},
        Field<&Type::sparkleMs>{"sparkleMs"},
        Field<&Type::sparkleChance>{"sparkleChance"},
        Field<&Type::seed>{"seed"}
    );
};

} // namespace Totem::Generated::Wire
