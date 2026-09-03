#pragma once

#include <cstdint>

namespace Totem::LedDisplay {

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
    RadialGauge,
    RadialMenu,
};

} // namespace Totem::LedDisplay
