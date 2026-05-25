#pragma once

#if __has_include(<FastLED.h>)
#include "LedDisplay/Renderers/FastLedRenderer.hpp"
#define TOTEM_LED_DISPLAY_FASTLED_WAVEFORM 1
#else
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#define TOTEM_LED_DISPLAY_FASTLED_WAVEFORM 0
#endif

#include <cstdint>

namespace Totem::LedDisplay::Renderers {

struct Waveform {
    [[nodiscard]] static uint8_t sine8(uint8_t phase) {
#if TOTEM_LED_DISPLAY_FASTLED_WAVEFORM
        return FastLedRenderer::sine8(phase);
#else
        return GenericRenderer::sine8(phase);
#endif
    }
};

} // namespace Totem::LedDisplay::Renderers

#undef TOTEM_LED_DISPLAY_FASTLED_WAVEFORM
