#pragma once

#include "LedDisplay/Animations/Cymatic/Config.hpp"
#include "LedDisplay/Animations/detail/SimpleField.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/FieldCoordinates.hpp"
#include "LedDisplay/Primitives/FieldMath.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct Cymatic {
    static constexpr AnimationKind kind = CymaticSpec::kind;
    static constexpr Layer defaultLayer = CymaticSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        CymaticSpec::defaultLifetimeMs;
    static constexpr bool requiresFullFrame = false;
    static constexpr uint8_t minimumWavelength = CymaticSpec::minimumWavelength;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::MaxValue,
                                                 .opacity = 255};

    CymaticConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto duration =
            detail::nonzero(ctx.clock.durationMs, defaultLifetimeMs);
        const auto timePhase =
            static_cast<uint16_t>((static_cast<uint32_t>(detail::progressQ8(
                                       ctx.clock.elapsedMs, duration)) *
                                   config.speed) >>
                                  8U);
        const auto sourceCount = resolvedSourceCount(config.sourceMode);
        const auto wavelength =
            std::max<uint8_t>(config.wavelength, minimumWavelength);
        const auto baseHue = static_cast<uint8_t>(config.hue + ctx.hueOffset);

        Primitives::forEachLogicalPixel(
            [&](const Primitives::FieldPoint &point) {
                uint16_t total = 0;
                for (uint8_t source = 0; source < sourceCount; ++source) {
                    const auto sourcePoint =
                        sourceFieldPoint(config.sourceMode, source, timePhase);
                    const auto distance = detail::approxDistance(
                        static_cast<int16_t>(point.x - sourcePoint.x),
                        static_cast<int16_t>(point.y - sourcePoint.y));
                    const auto distance8 = static_cast<uint8_t>(distance >> 8U);
                    const auto phase = static_cast<uint16_t>(
                        (static_cast<uint32_t>(distance8) * wavelength *
                         detail::simpleQ8Unit) +
                        timePhase);
                    total += Primitives::FieldMath::sine8Q8(phase);
                }

                const auto average = static_cast<uint8_t>(total / sourceCount);
                const auto contrasted =
                    detail::contrastAroundMid(average, config.contrast);
                const auto band = detail::highBand(contrasted, 128);
                if (band == 0) {
                    return;
                }
                const auto value =
                    Primitives::FieldMath::scale8(config.value, band);
                if (value == 0) {
                    return;
                }
                const auto hue = static_cast<uint8_t>(
                    baseHue + Primitives::FieldMath::scale8(
                                  detail::stripRadius8(point), config.hueStep));
                ctx.canvas.pixel(point.spoke, point.radial,
                                 HsvColor{.hue = hue,
                                          .saturation = config.saturation,
                                          .value = value});
            });
    }

  private:
    [[nodiscard]] static constexpr uint8_t resolvedSourceCount(uint8_t mode) {
        switch (mode % 4U) {
        case 1:
            return 3;
        case 2:
            return 4;
        default:
            return 2;
        }
    }

    [[nodiscard]] static Primitives::FieldPoint
    sourceFieldPoint(uint8_t mode, uint8_t source, uint16_t timePhase) {
        const auto modeIndex = static_cast<uint8_t>(mode % 4U);
        uint8_t spoke = 0;
        switch (modeIndex) {
        case 1:
            spoke = static_cast<uint8_t>((source * (Config::spokeCount / 3U)) %
                                         Config::spokeCount);
            break;
        case 2:
            spoke = static_cast<uint8_t>((source * (Config::spokeCount / 4U)) %
                                         Config::spokeCount);
            break;
        case 3:
            spoke = static_cast<uint8_t>(
                ((timePhase >> 12U) + (source * (Config::spokeCount / 2U))) %
                Config::spokeCount);
            break;
        default:
            spoke = source == 0 ? 0U
                                : static_cast<uint8_t>(Config::spokeCount / 2U);
            break;
        }
        return Primitives::fieldPoint(
            spoke, static_cast<uint8_t>(Config::ringCount - 1U));
    }
};

} // namespace Totem::LedDisplay::Animations
