#pragma once

#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/Types.hpp"
#include "LedDisplay/detail/FastLedCompat.hpp"
#include "LedDisplay/detail/RendererSelect.hpp"
#include "LedTopology/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <FastLED.h>
#include <array>
#include <cstddef>
#include <span>

namespace Totem::LedDisplay::Outputs {

class FastLedOutput {
  public:
    DELETE_COPY(FastLedOutput)
    DELETE_MOVE(FastLedOutput)

    FastLedOutput() = default;

    ReturnCode begin(const Config &config) {
        (void)config;
        ::fl::FastLED.clearData();
        ::fl::FastLED.setBrightness(Config::globalBrightness);
        if constexpr (Config::temporalDithering) {
            ::fl::FastLED.setDither(BINARY_DITHER);
        } else {
            ::fl::FastLED.setDither(DISABLE_DITHER);
        }

        _addControllers();
        _configured = true;
        _log_i("FastLED output ready: group=%zu/%zu pixels=%zu lines=%zu",
               Config::ledGroupIndex, Config::ledGroupCount,
               Config::ownedPixelCount, Config::dataLineCount);
        return OK();
    }

    ReturnCode show(std::span<const HsvColor> frame) {
        FAIL_IF(!_configured, ERR(CoreError, InvalidState),
                "FastLED output is not configured");
        FAIL_IF(frame.size() != _leds.size(), ERR(CoreError, InvalidSize),
                "Unexpected LED frame size");

        for (size_t i = 0; i < frame.size(); ++i) {
            const auto rgb = detail::Render::hsvToRgb(frame[i]);
            _leds[i] = ::fl::CRGB(rgb.red, rgb.green, rgb.blue);
        }
        ::fl::FastLED.show();
        return OK();
    }

    ReturnCode deinit() {
        if (!_configured) {
            return OK();
        }
        ::fl::FastLED.clear(true);
        _configured = false;
        return OK();
    }

  private:
    template <uint8_t PinValue>
    void _addLine(size_t localStart, size_t count) {
        ::fl::FastLED.addLeds<::fl::WS2812B, PinValue, ::fl::GRB>(
            _leds.data() + localStart, count);
    }

    void _addControllers() {
        constexpr auto lines = LedTopology::OwnedPixels::dataLines();

        if constexpr (Config::dataLineCount >= 1) {
            constexpr auto pin =
                static_cast<uint8_t>(Config::outputPins[0]);
            _addLine<pin>(lines[0].localStart, lines[0].count);
        }
        if constexpr (Config::dataLineCount >= 2) {
            constexpr auto pin =
                static_cast<uint8_t>(Config::outputPins[1]);
            _addLine<pin>(lines[1].localStart, lines[1].count);
        }
    }

    std::array<::fl::CRGB, Config::ownedPixelCount> _leds{};
    bool _configured = false;
};

} // namespace Totem::LedDisplay::Outputs
