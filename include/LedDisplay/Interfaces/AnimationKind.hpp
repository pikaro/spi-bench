#pragma once

#include <cstdint>

namespace Totem::LedDisplay {

enum class AnimationCommandType : uint8_t {
    None = 0,
    Play,
    Update,
    Stop,
    SetHueOffset,
    SetRotationOffset,
    SetBrightness,
    SetLayerActive,
    SetLayerOpacity,
    FadeLayerSwap,
};

enum class AnimationKind : uint8_t {
    None = 0,
    DiagnosticFill,
    CenterWave,
    FftReactive,
    WheelIndicator,
    SpokeSweep,
    Sinelon,
    SineWave,
};

} // namespace Totem::LedDisplay
