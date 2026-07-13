#pragma once

#include "AudioFft/Interfaces/Wire.hpp"
#include "LedDisplay/Primitives/AudioControls.hpp"
#include "LedDisplay/Primitives/Canvas.hpp"
#include "Wheel/Interfaces/Wire.hpp"
#include <cstdint>

namespace Totem::LedDisplay {

struct FrameClock {
    uint32_t nowMs = 0;
    uint32_t elapsedMs = 0;
    uint32_t durationMs = 0;
    uint32_t frame = 0;
};

struct AnimationInputSnapshot {
    Totem::AudioFft::FftFrame fftFrame{};
    Totem::AudioFft::PeakEvent peakEvent{};
    Totem::Wheel::WheelState wheelState{};
    bool hasFftFrame = false;
    bool hasPeakEvent = false;
    bool hasWheelState = false;
};

struct AnimationRenderContext {
    FrameClock clock{};
    uint8_t hueOffset = 0;
    Primitives::Canvas canvas;
    const AnimationInputSnapshot &inputs;
    Primitives::AudioControls audio{};
};

} // namespace Totem::LedDisplay
