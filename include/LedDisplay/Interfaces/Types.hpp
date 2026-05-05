#pragma once

#include "Macros/internal/Markers.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace Totem::LedDisplay {

struct HsvColor {
    uint8_t hue = 0;
    uint8_t saturation = 0;
    uint8_t value = 0;
};

struct RgbColor {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
};

enum class BlendOp : uint8_t {
    Replace = 0,
    MaxValue,
    AddValue,
};

enum class AnimationCommandType : uint8_t {
    Play = 0,
    Stop,
};

enum class AnimationKind : uint8_t {
    DiagnosticFill = 0,
    CenterWave,
    FftReactive,
};

enum class Layer : uint8_t {
    Main = 0,
};

struct WIRE_MSG DiagnosticFillConfig {
    uint8_t hue = 0;
    uint8_t saturation = 0;
    uint8_t value = 48;
};

struct WIRE_MSG CenterWaveConfig {
    uint8_t hue = 144;
    uint8_t saturation = 255;
    uint8_t value = 180;
    uint8_t width = 5;
};

struct WIRE_MSG FftReactiveConfig {
    uint8_t baseHue = 0;
    uint8_t saturation = 255;
    uint8_t valueScale = 128;
};

struct AnimationCommand {
    AnimationCommandType type = AnimationCommandType::Play;
    AnimationKind kind = AnimationKind::CenterWave;
    uint16_t requestId = 0;
    Layer layer = Layer::Main;
    uint16_t lifetimeMs = 1200;
    uint8_t payloadSize = 0;
    std::array<std::byte, LedDisplayConfig::animationCommandPayloadBytes>
        payload{};

    [[nodiscard]] constexpr bool validate() const {
        return payloadSize <= payload.size();
    }
};

struct DiagnosticFill {
    DiagnosticFillConfig config{};
};

struct CenterWave {
    CenterWaveConfig config{};
};

struct FftReactive {
    FftReactiveConfig config{};
};

using AnimationPayload = std::variant<DiagnosticFill, CenterWave, FftReactive>;

static_assert(std::is_trivially_copyable_v<HsvColor>,
              "HsvColor must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<RgbColor>,
              "RgbColor must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<DiagnosticFillConfig>,
              "DiagnosticFillConfig must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<CenterWaveConfig>,
              "CenterWaveConfig must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<FftReactiveConfig>,
              "FftReactiveConfig must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationCommand>,
              "AnimationCommand must remain queue-copyable");
static_assert(std::is_trivially_copyable_v<AnimationPayload>,
              "AnimationPayload must remain queue-copyable");

} // namespace Totem::LedDisplay
