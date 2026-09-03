#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG RadialMenuConfig {
    std::array<uint8_t, 8> itemHues{{0, 32, 64, 96, 128, 160, 192, 224}};
    // One bit per item. Clear bits render as dim white placeholders.
    uint8_t populatedItems = 0xFF;
    uint8_t itemCount = 8;
    // Item index within itemCount; 255 leaves every item folded.
    uint8_t selectedItem = 0;
    uint8_t itemSaturation = 255;
    uint8_t itemValue = 255;
    uint8_t emptyItemValue = 32;

    // Zero width or depth disables that rectangular part of the petal.
    uint8_t baseSpokeWidth = 4;
    uint8_t baseRingDepth = 2;
    uint8_t baseTipSpokeWidth = 2;
    uint8_t baseTipRingDepth = 2;
    uint8_t unfurledSpokeWidth = 4;
    uint8_t unfurledRingDepth = 6;
    uint8_t unfurledTipSpokeWidth = 2;
    uint8_t unfurledTipRingDepth = 2;
    // Zero applies the unfurled geometry immediately.
    uint16_t unfurlDurationMs = 160;
};

struct RadialMenuSpec {
    static constexpr AnimationKind kind = AnimationKind::RadialMenu;
    static constexpr Layer defaultLayer = Layer::UI;
    static constexpr uint16_t defaultLifetimeMs = 0;
    static constexpr uint16_t defaultRequestId = 6;
    static constexpr size_t maximumItems = 8;
    static constexpr uint8_t noSelectedItem = 255;
};

static_assert(std::is_trivially_copyable_v<RadialMenuConfig>,
              "RadialMenuConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
