#pragma once

#include <cstdint>

namespace Totem::LedDisplay {

enum class AnimationCommandType : uint8_t {
    None = 0,
    Play,
    Stop,
    SetHueOffset,
    SetRotationOffset,
};

enum class AnimationKind : uint8_t {
    None = 0,
    DiagnosticFill,
    CenterWave,
    FftReactive,
};

} // namespace Totem::LedDisplay
