#pragma once

#include "LedDisplay/Interfaces/AnimationKind.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "Macros/internal/Markers.hpp"
#include <cstdint>
#include <type_traits>

namespace Totem::LedDisplay::Animations {

struct WIRE_MSG StainedCellsConfig {
    uint8_t baseHue = 160;
    uint8_t saturation = 245;
    uint8_t value = 210;
    uint8_t baseValue = 10;
    uint8_t seedCount = 6;
    uint8_t borderWidth = 28;
    uint8_t interiorValue = 64;
    uint8_t driftSpeed = 16;
    uint8_t contrast = 180;
    uint8_t peakSensitivity = 96;
    uint8_t seed = 0x3D;
    uint8_t hueModulation = 144;
};

struct StainedCellsSpec {
    static constexpr AnimationKind kind = AnimationKind::StainedCells;
    static constexpr Layer defaultLayer = Layer::Fft;
    static constexpr uint16_t defaultLifetimeMs = 0;
};

static_assert(std::is_trivially_copyable_v<StainedCellsConfig>,
              "StainedCellsConfig must remain queue-copyable");

} // namespace Totem::LedDisplay::Animations
