#pragma once

#include "LedDisplay/Animations/RadialMenu/Config.hpp"
#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include <algorithm>
#include <cstdint>

namespace Totem::LedDisplay::Animations {

struct RadialMenu {
    static constexpr AnimationKind kind = RadialMenuSpec::kind;
    static constexpr Layer defaultLayer = RadialMenuSpec::defaultLayer;
    static constexpr uint16_t defaultLifetimeMs =
        RadialMenuSpec::defaultLifetimeMs;
    static constexpr uint16_t defaultRequestId =
        RadialMenuSpec::defaultRequestId;
    static constexpr bool requiresFullFrame = false;
    static constexpr AnimationStyle defaultStyle{.blendOp = BlendOp::Replace,
                                                 .opacity = 255};

    RadialMenuConfig config{};

    void render(AnimationRenderContext &ctx) const {
        const auto itemCount = std::min<uint8_t>(
            config.itemCount,
            static_cast<uint8_t>(std::min<size_t>(RadialMenuSpec::maximumItems,
                                                  Config::spokeCount)));
        if (itemCount == 0) {
            return;
        }

        const bool hasSelection = config.selectedItem < itemCount;
        for (uint8_t item = 0; item < itemCount; ++item) {
            if (hasSelection && item == config.selectedItem) {
                continue;
            }
            drawItem(ctx, item, itemCount, baseShape(), colorFor(item));
        }
        if (hasSelection) {
            drawItem(ctx, config.selectedItem, itemCount,
                     selectedShape(ctx.clock.elapsedMs),
                     colorFor(config.selectedItem));
        }
    }

  private:
    struct Shape {
        uint8_t spokeWidth = 0;
        uint8_t ringDepth = 0;
        uint8_t tipSpokeWidth = 0;
        uint8_t tipRingDepth = 0;
    };

    [[nodiscard]] constexpr Shape baseShape() const {
        return Shape{
            .spokeWidth = config.baseSpokeWidth,
            .ringDepth = config.baseRingDepth,
            .tipSpokeWidth = config.baseTipSpokeWidth,
            .tipRingDepth = config.baseTipRingDepth,
        };
    }

    [[nodiscard]] constexpr Shape unfurledShape() const {
        return Shape{
            .spokeWidth = config.unfurledSpokeWidth,
            .ringDepth = config.unfurledRingDepth,
            .tipSpokeWidth = config.unfurledTipSpokeWidth,
            .tipRingDepth = config.unfurledTipRingDepth,
        };
    }

    [[nodiscard]] Shape selectedShape(uint32_t elapsedMs) const {
        const auto progress =
            config.unfurlDurationMs == 0
                ? fullProgress
                : static_cast<uint8_t>(std::min<uint64_t>(
                      (static_cast<uint64_t>(elapsedMs) * fullProgress) /
                          config.unfurlDurationMs,
                      fullProgress));
        const auto base = baseShape();
        const auto unfurled = unfurledShape();
        return Shape{
            .spokeWidth =
                interpolate(base.spokeWidth, unfurled.spokeWidth, progress),
            .ringDepth =
                interpolate(base.ringDepth, unfurled.ringDepth, progress),
            .tipSpokeWidth = interpolate(base.tipSpokeWidth,
                                         unfurled.tipSpokeWidth, progress),
            .tipRingDepth =
                interpolate(base.tipRingDepth, unfurled.tipRingDepth, progress),
        };
    }

    [[nodiscard]] HsvColor colorFor(uint8_t item) const {
        const bool populated =
            (config.populatedItems & static_cast<uint8_t>(1U << item)) != 0;
        if (!populated) {
            return HsvColor{
                .hue = 0, .saturation = 0, .value = config.emptyItemValue};
        }
        // UI item colors are semantic and intentionally ignore global hue.
        return HsvColor{.hue = config.itemHues[item],
                        .saturation = config.itemSaturation,
                        .value = config.itemValue};
    }

    static void drawItem(AnimationRenderContext &ctx, uint8_t item,
                         uint8_t itemCount, Shape shape, HsvColor color) {
        const auto barDepth =
            std::min<uint8_t>(shape.ringDepth, Config::ringCount);
        drawBand(ctx, item, itemCount, 0, barDepth, shape.spokeWidth, color);
        const auto remainingRings =
            static_cast<uint8_t>(Config::ringCount - barDepth);
        drawBand(ctx, item, itemCount, barDepth,
                 std::min<uint8_t>(shape.tipRingDepth, remainingRings),
                 shape.tipSpokeWidth, color);
    }

    static void drawBand(AnimationRenderContext &ctx, uint8_t item,
                         uint8_t itemCount, uint8_t firstRing,
                         uint8_t ringDepth, uint8_t spokeWidth,
                         HsvColor color) {
        const auto width = std::min<uint8_t>(spokeWidth, Config::spokeCount);
        if (width == 0 || ringDepth == 0) {
            return;
        }

        const auto centerSpoke = static_cast<int16_t>(
            ((static_cast<uint16_t>(2U * item + 1U) * Config::spokeCount) /
             (2U * itemCount)));
        const auto firstSpoke =
            static_cast<int16_t>(centerSpoke - (width / 2U));
        for (uint8_t spokeOffset = 0; spokeOffset < width; ++spokeOffset) {
            const auto spoke =
                wrapSpoke(static_cast<int16_t>(firstSpoke + spokeOffset));
            for (uint8_t ringOffset = 0; ringOffset < ringDepth; ++ringOffset) {
                ctx.canvas.pixel(spoke,
                                 static_cast<uint8_t>(firstRing + ringOffset),
                                 color, BlendOp::Replace);
            }
        }
    }

    [[nodiscard]] static constexpr uint8_t wrapSpoke(int16_t spoke) {
        const auto count = static_cast<int16_t>(Config::spokeCount);
        return static_cast<uint8_t>(((spoke % count) + count) % count);
    }

    [[nodiscard]] static constexpr uint8_t interpolate(uint8_t from, uint8_t to,
                                                       uint8_t progress) {
        const auto delta = static_cast<int16_t>(to) - from;
        return static_cast<uint8_t>(
            static_cast<int16_t>(from) +
            ((delta * static_cast<int16_t>(progress)) / fullProgress));
    }

    static constexpr uint8_t fullProgress = 255;
};

} // namespace Totem::LedDisplay::Animations
