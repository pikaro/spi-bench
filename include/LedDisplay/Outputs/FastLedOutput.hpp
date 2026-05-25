#pragma once

#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/detail/FastLedCompat.hpp"
#include "LedDisplay/detail/RendererSelect.hpp"
#include "LedTopology/Facade.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <FastLED.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::LedDisplay::Outputs {

class FastLedOutput {
  public:
    DELETE_COPY(FastLedOutput)
    DELETE_MOVE(FastLedOutput)

    FastLedOutput() = default;

    ReturnCode begin(const Config &config) {
        ::fl::FastLED.clearData();
        ::fl::FastLED.setBrightness(config.globalBrightness);
        _globalBrightness = config.globalBrightness;
        _outputLumaFloor = config.outputLumaFloor;
        _configuredOutputValueFloor = config.outputValueFloor;
        _refreshEffectiveValueFloor();
        _temporalDithering = config.temporalDithering;
        _ditherNeedsReenable = config.temporalDithering;
        _hasMeasuredDitherCadence = false;

        if (_temporalDithering) {
            ::fl::FastLED.setDither(BINARY_DITHER);
        } else {
            ::fl::FastLED.setDither(DISABLE_DITHER);
        }

        _addControllers();
        _configured = true;
        _log_i("FastLED output ready: groups=%zu/%zu pixels=%zu lines=%zu "
               "valueFloor=%u effectiveValueFloor=%u lumaFloor=%u",
               Config::nodeGroupCount, Config::ledGroupCount,
               Config::ownedPixelCount, Config::dataLineCount,
               static_cast<unsigned>(_configuredOutputValueFloor),
               static_cast<unsigned>(_effectiveOutputValueFloor),
               static_cast<unsigned>(_outputLumaFloor));
        return OK();
    }

    ReturnCode setBrightness(uint8_t brightness) {
        FAIL_IF(!_configured, ERR(CoreError, InvalidState),
                "FastLED output is not configured");
        _globalBrightness = brightness;
        _refreshEffectiveValueFloor();
        ::fl::FastLED.setBrightness(brightness);
        _log_i("FastLED brightness set to %u effectiveValueFloor=%u",
               static_cast<unsigned>(_globalBrightness),
               static_cast<unsigned>(_effectiveOutputValueFloor));
        return OK();
    }

    ReturnCode show(std::span<const HsvColor> frame) {
        FAIL_IF(!_configured, ERR(CoreError, InvalidState),
                "FastLED output is not configured");
        FAIL_IF(frame.size() != _leds.size(), ERR(CoreError, InvalidSize),
                "Unexpected LED frame size");

        for (size_t i = 0; i < frame.size(); ++i) {
            if (frame[i].value < _effectiveOutputValueFloor) {
                _leds[i] = ::fl::CRGB::Black;
                continue;
            }
            const auto rgb = detail::Render::hsvToRgb(frame[i]);
            const auto clamped = _clampLowLuma(rgb);
            _leds[i] = ::fl::CRGB(clamped.red, clamped.green, clamped.blue);
        }
        ::fl::FastLED.show();
        _trackDitherCadence();
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
    template <uint8_t PinValue> void _addLine(size_t localStart, size_t count) {
        ::fl::FastLED.addLeds<::fl::WS2812B, PinValue, ::fl::GRB>(
            _leds.data() + localStart, count);
    }

    void _addControllers() {
        constexpr auto lines = LedTopology::OwnedPixels::dataLines();

        if constexpr (Config::dataLineCount >= 1) {
            constexpr auto pin = static_cast<uint8_t>(Config::outputPins[0]);
            _addLine<pin>(lines[0].localStart, lines[0].count);
        }
        if constexpr (Config::dataLineCount >= 2) {
            constexpr auto pin = static_cast<uint8_t>(Config::outputPins[1]);
            _addLine<pin>(lines[1].localStart, lines[1].count);
        }
    }

    [[nodiscard]] static constexpr uint8_t
    _brightnessAdjustedFloor(uint8_t floor, uint8_t brightness) {
        if (floor == 0) {
            return 0;
        }
        if (brightness == 0) {
            return 255;
        }

        const auto adjusted =
            ((static_cast<uint16_t>(floor) * 255U) + brightness - 1U) /
            brightness;
        return static_cast<uint8_t>(adjusted > 255U ? 255U : adjusted);
    }

    void _refreshEffectiveValueFloor() {
        _effectiveOutputValueFloor = _brightnessAdjustedFloor(
            _configuredOutputValueFloor, _globalBrightness);
    }

    [[nodiscard]] static constexpr uint8_t _scale8(uint8_t value,
                                                   uint8_t scale) {
        return static_cast<uint8_t>((static_cast<uint16_t>(value) * scale) /
                                    255U);
    }

    [[nodiscard]] static constexpr RgbColor
    _scaleBrightness(RgbColor rgb, uint8_t brightness) {
        return RgbColor{
            .red = _scale8(rgb.red, brightness),
            .green = _scale8(rgb.green, brightness),
            .blue = _scale8(rgb.blue, brightness),
        };
    }

    [[nodiscard]] static constexpr uint8_t _luma(RgbColor rgb) {
        constexpr uint16_t redWeight = 54;
        constexpr uint16_t greenWeight = 183;
        constexpr uint16_t blueWeight = 19;
        return static_cast<uint8_t>(
            ((static_cast<uint16_t>(rgb.red) * redWeight) +
             (static_cast<uint16_t>(rgb.green) * greenWeight) +
             (static_cast<uint16_t>(rgb.blue) * blueWeight) + 128U) /
            256U);
    }

    [[nodiscard]] RgbColor _clampLowLuma(RgbColor rgb) const {
        if (_outputLumaFloor == 0) {
            return rgb;
        }

        const auto scaled = _scaleBrightness(rgb, _globalBrightness);
        if (_luma(scaled) < _outputLumaFloor) {
            return RgbColor{};
        }
        return rgb;
    }

    void _trackDitherCadence() {
        if (!_temporalDithering) {
            return;
        }

        constexpr uint16_t fastLedTemporalDitherMinimumFps = 100;
        const auto fps = static_cast<uint16_t>(::fl::FastLED.getFPS());
        if (fps >= fastLedTemporalDitherMinimumFps) {
            _hasMeasuredDitherCadence = true;
            if (_ditherNeedsReenable) {
                ::fl::FastLED.setDither(BINARY_DITHER);
                _ditherNeedsReenable = false;
                _log_i("FastLED temporal dithering enabled at measured "
                       "FPS=%u",
                       fps);
            }
            return;
        }

        if (!_hasMeasuredDitherCadence) {
            return;
        }
        if (!_ditherNeedsReenable) {
            _log_e("FastLED measured FPS below dither threshold: %u < %u; "
                   "dithering will be re-enabled after recovery",
                   fps, fastLedTemporalDitherMinimumFps);
        }
        _ditherNeedsReenable = true;
    }

    std::array<::fl::CRGB, Config::ownedPixelCount> _leds{};
    uint8_t _globalBrightness = 255;
    uint8_t _configuredOutputValueFloor = 0;
    uint8_t _effectiveOutputValueFloor = 0;
    uint8_t _outputLumaFloor = 0;
    bool _configured = false;
    bool _temporalDithering = false;
    bool _hasMeasuredDitherCadence = false;
    bool _ditherNeedsReenable = false;
};

} // namespace Totem::LedDisplay::Outputs
