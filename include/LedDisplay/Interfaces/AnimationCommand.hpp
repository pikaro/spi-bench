#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay {

struct DisplayBrightness {
    uint8_t value = 0;
};

struct WIRE_MSG AnimationCommand {
    AnimationCommandType type = AnimationCommandType::None;
    AnimationKind kind = AnimationKind::None;
    uint16_t requestId = 0;
    Layer layer = Layer::Effect;
    uint16_t lifetimeMs = 1200;
    uint8_t payloadSize = 0;
    std::array<std::byte, LedDisplayConfig::animationCommandPayloadBytes>
        payload{};

    [[nodiscard]] constexpr bool validate() const {
        return payloadSize <= payload.size();
    }
};

static_assert(std::is_trivially_copyable_v<AnimationCommand>,
              "AnimationCommand must remain queue-copyable");

} // namespace Totem::LedDisplay
