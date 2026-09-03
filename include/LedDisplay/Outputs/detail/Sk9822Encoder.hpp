#pragma once

#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/OutputConfig.hpp"
#include "LedDisplay/detail/RendererSelect.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::LedDisplay::Outputs::detail {

enum class Sk9822EncodeResult : uint8_t {
    Ok,
    InvalidSize,
};

class Sk9822Encoder {
  public:
    static constexpr size_t startFrameBytes = 4;
    static constexpr size_t bytesPerPixel = 4;

    [[nodiscard]] static constexpr size_t endFrameBytes(size_t pixelCount) {
        return 4U * ((pixelCount / 32U) + 1U);
    }

    [[nodiscard]] static constexpr size_t encodedSize(size_t pixelCount) {
        return startFrameBytes + (pixelCount * bytesPerPixel) +
               endFrameBytes(pixelCount);
    }

    [[nodiscard]] static constexpr uint8_t brightnessLevel(uint8_t brightness) {
        auto level = static_cast<uint8_t>(
            (static_cast<uint16_t>(brightness) * 31U) / 255U);
        if (brightness != 0 && level == 0) {
            level = 1;
        }
        return level;
    }

    [[nodiscard]] static constexpr uint8_t
    effectiveValueFloor(uint8_t configuredFloor, uint8_t level) {
        if (configuredFloor == 0) {
            return 0;
        }
        if (level == 0) {
            return 255;
        }
        const auto adjusted =
            ((static_cast<uint16_t>(configuredFloor) * 31U) + level - 1U) /
            level;
        return static_cast<uint8_t>(std::min<uint16_t>(adjusted, 255U));
    }

    static Sk9822EncodeResult
    encode(std::span<const HsvColor> frame, uint8_t brightness,
           uint8_t configuredOutputValueFloor, uint8_t outputLumaFloor,
           Sk9822WireColorOrder colorOrder, std::span<std::byte> output) {
        if (output.size() != encodedSize(frame.size())) {
            return Sk9822EncodeResult::InvalidSize;
        }

        const auto level = brightnessLevel(brightness);
        const auto valueFloor =
            effectiveValueFloor(configuredOutputValueFloor, level);
        std::fill(output.begin(), output.end(), std::byte{0});

        size_t offset = startFrameBytes;
        for (const auto hsv : frame) {
            auto rgb = RgbColor{};
            if (level != 0 && hsv.value >= valueFloor) {
                rgb = LedDisplay::detail::Render::hsvToRgb(hsv);
                if (outputLumaFloor != 0 &&
                    luma(scaleForHardwareBrightness(rgb, level)) <
                        outputLumaFloor) {
                    rgb = {};
                }
            }

            output[offset++] = std::byte{static_cast<uint8_t>(0xE0U | level)};
            const auto wire = ordered(rgb, colorOrder);
            output[offset++] = std::byte{wire[0]};
            output[offset++] = std::byte{wire[1]};
            output[offset++] = std::byte{wire[2]};
        }
        return Sk9822EncodeResult::Ok;
    }

    static Sk9822EncodeResult encodeBlack(size_t pixelCount,
                                          std::span<std::byte> output) {
        if (output.size() != encodedSize(pixelCount)) {
            return Sk9822EncodeResult::InvalidSize;
        }
        std::fill(output.begin(), output.end(), std::byte{0});
        size_t offset = startFrameBytes;
        for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
            output[offset] = std::byte{0xE0};
            offset += bytesPerPixel;
        }
        return Sk9822EncodeResult::Ok;
    }

  private:
    [[nodiscard]] static constexpr std::array<uint8_t, 3>
    ordered(RgbColor rgb, Sk9822WireColorOrder order) {
        switch (order) {
        case Sk9822WireColorOrder::Rgb:
            return {rgb.red, rgb.green, rgb.blue};
        case Sk9822WireColorOrder::Rbg:
            return {rgb.red, rgb.blue, rgb.green};
        case Sk9822WireColorOrder::Grb:
            return {rgb.green, rgb.red, rgb.blue};
        case Sk9822WireColorOrder::Gbr:
            return {rgb.green, rgb.blue, rgb.red};
        case Sk9822WireColorOrder::Brg:
            return {rgb.blue, rgb.red, rgb.green};
        case Sk9822WireColorOrder::Bgr:
            return {rgb.blue, rgb.green, rgb.red};
        }
        return {};
    }

    [[nodiscard]] static constexpr uint8_t scaleChannel(uint8_t value,
                                                        uint8_t level) {
        return static_cast<uint8_t>((static_cast<uint16_t>(value) * level) /
                                    31U);
    }

    [[nodiscard]] static constexpr RgbColor
    scaleForHardwareBrightness(RgbColor rgb, uint8_t level) {
        return {
            .red = scaleChannel(rgb.red, level),
            .green = scaleChannel(rgb.green, level),
            .blue = scaleChannel(rgb.blue, level),
        };
    }

    [[nodiscard]] static constexpr uint8_t luma(RgbColor rgb) {
        constexpr uint16_t redWeight = 54;
        constexpr uint16_t greenWeight = 183;
        constexpr uint16_t blueWeight = 19;
        return static_cast<uint8_t>(
            ((static_cast<uint16_t>(rgb.red) * redWeight) +
             (static_cast<uint16_t>(rgb.green) * greenWeight) +
             (static_cast<uint16_t>(rgb.blue) * blueWeight) + 128U) /
            256U);
    }
};

} // namespace Totem::LedDisplay::Outputs::detail
