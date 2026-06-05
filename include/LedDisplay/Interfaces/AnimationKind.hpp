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
    SpectralWeave,
    SpectralIris,
    OrbitSparks,
    StainedCells,
    WheelIndicator,
    SpokeSweep,
    Sinelon,
    SineWave,
    Starburst,
    Vortex,
    Shutter,
    OrbitRing,
    Lighthouse,
    Cymatic,
    BreathingRings,
    RadialCurtain,
    PolarLattice,
    Bolt,
};

} // namespace Totem::LedDisplay
