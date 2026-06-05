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

struct WIRE_MSG AnimationPlayCommand {
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

struct WIRE_MSG AnimationUpdateCommand {
    AnimationKind kind = AnimationKind::None;
    uint16_t requestId = 0;
    uint8_t payloadSize = 0;
    std::array<std::byte, LedDisplayConfig::animationCommandPayloadBytes>
        payload{};

    [[nodiscard]] constexpr bool validate() const {
        return payloadSize <= payload.size();
    }
};

struct WIRE_MSG AnimationStopCommand {
    uint16_t requestId = 0;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

struct WIRE_MSG AnimationSetHueOffsetCommand {
    uint8_t offset = 0;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

struct WIRE_MSG AnimationSetRotationOffsetCommand {
    uint8_t offset = 0;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

struct WIRE_MSG AnimationSetBrightnessCommand {
    uint8_t value = 0;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

struct WIRE_MSG AnimationSetLayerActiveCommand {
    Layer layer = Layer::Effect;
    bool active = true;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

struct WIRE_MSG AnimationSetLayerOpacityCommand {
    Layer layer = Layer::Effect;
    uint8_t opacity = 255;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

struct WIRE_MSG AnimationFadeLayerSwapCommand {
    Layer first = Layer::Fft;
    Layer second = Layer::FftAlt;
    uint16_t durationMs = 10000;

    [[nodiscard]] constexpr bool validate() const { return true; }
};

static_assert(std::is_trivially_copyable_v<AnimationPlayCommand>,
              "AnimationPlayCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationUpdateCommand>,
              "AnimationUpdateCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationStopCommand>,
              "AnimationStopCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationSetHueOffsetCommand>,
              "AnimationSetHueOffsetCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationSetRotationOffsetCommand>,
              "AnimationSetRotationOffsetCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationSetBrightnessCommand>,
              "AnimationSetBrightnessCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationSetLayerActiveCommand>,
              "AnimationSetLayerActiveCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationSetLayerOpacityCommand>,
              "AnimationSetLayerOpacityCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationFadeLayerSwapCommand>,
              "AnimationFadeLayerSwapCommand must remain queue-copyable");

} // namespace Totem::LedDisplay
