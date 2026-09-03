#pragma once

#include "Generated/Wire/Support.hpp"
#include "LedDisplay/Animations/RadialMenu/Config.hpp"
#include <tuple>

namespace Totem::Generated::Wire {

template <> struct FieldList<::Totem::LedDisplay::Animations::RadialMenuConfig> {
    using Type = ::Totem::LedDisplay::Animations::RadialMenuConfig;
    static constexpr auto fields = std::make_tuple(
        Field<&Type::itemHues>{"itemHues"},
        Field<&Type::populatedItems>{"populatedItems"},
        Field<&Type::itemCount>{"itemCount"},
        Field<&Type::selectedItem>{"selectedItem"},
        Field<&Type::itemSaturation>{"itemSaturation"},
        Field<&Type::itemValue>{"itemValue"},
        Field<&Type::emptyItemValue>{"emptyItemValue"},
        Field<&Type::baseSpokeWidth>{"baseSpokeWidth"},
        Field<&Type::baseRingDepth>{"baseRingDepth"},
        Field<&Type::baseTipSpokeWidth>{"baseTipSpokeWidth"},
        Field<&Type::baseTipRingDepth>{"baseTipRingDepth"},
        Field<&Type::unfurledSpokeWidth>{"unfurledSpokeWidth"},
        Field<&Type::unfurledRingDepth>{"unfurledRingDepth"},
        Field<&Type::unfurledTipSpokeWidth>{"unfurledTipSpokeWidth"},
        Field<&Type::unfurledTipRingDepth>{"unfurledTipRingDepth"},
        Field<&Type::unfurlDurationMs>{"unfurlDurationMs"}
    );
};

} // namespace Totem::Generated::Wire
