#pragma once

#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/Types.hpp"
#include "LedDisplay/detail/RendererSelect.hpp"
#include "LedTopology/Facade.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Totem::LedDisplay::detail {

class PrimitiveCanvas {
  public:
    explicit PrimitiveCanvas(std::span<HsvColor> frame) : _frame(frame) {}

    void fill(HsvColor color, BlendOp op = BlendOp::Replace) {
        for (auto &pixel : _frame) {
            pixel = Render::blend(pixel, color, op);
        }
    }

    void pixel(uint8_t spoke, uint8_t radial, HsvColor color,
               BlendOp op = BlendOp::MaxValue) {
        const auto physical = LedTopology::Umbrella::physicalFor(spoke, radial);
        physicalPixel(physical, color, op);
    }

    void physicalPixel(LedTopology::PhysicalPixelIndex physical,
                       HsvColor color, BlendOp op = BlendOp::MaxValue) {
        if constexpr (Config::ledGroupCount > 1) {
            if (!LedTopology::OwnedPixels::owns(physical)) {
                return;
            }
        }
        const auto local = LedTopology::OwnedPixels::localIndex(physical);
        if (static_cast<size_t>(local) >= _frame.size()) {
            return;
        }
        auto &dst = _frame[static_cast<size_t>(local)];
        dst = Render::blend(dst, color, op);
    }

    void ring(uint8_t radial, HsvColor color,
              BlendOp op = BlendOp::MaxValue) {
        if (radial >= Config::ringCount) {
            return;
        }
        for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
            pixel(spoke, radial, color, op);
        }
    }

    void spoke(uint8_t spoke, HsvColor color,
               BlendOp op = BlendOp::MaxValue) {
        if (spoke >= Config::spokeCount) {
            return;
        }
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            pixel(spoke, radial, color, op);
        }
    }

    void radialLine(uint8_t spoke, uint8_t start, uint8_t end,
                    HsvColor color, BlendOp op = BlendOp::MaxValue) {
        if (spoke >= Config::spokeCount) {
            return;
        }
        const auto upper = std::min<uint8_t>(
            end, static_cast<uint8_t>(Config::ringCount - 1U));
        for (uint8_t radial = start; radial <= upper; ++radial) {
            pixel(spoke, radial, color, op);
            if (radial == UINT8_MAX) {
                break;
            }
        }
    }

  private:
    std::span<HsvColor> _frame;
};

struct PrimitiveParams {
    uint32_t elapsedMs = 0;
    uint32_t durationMs = 1200;
    uint8_t hue = 144;
    uint8_t saturation = 255;
    uint8_t value = 180;
    uint8_t width = 5;
    uint8_t density = 48;
    uint8_t speed = 128;
};

[[nodiscard]] inline uint32_t nonzero(uint32_t value, uint32_t fallback) {
    return value == 0 ? fallback : value;
}

[[nodiscard]] inline uint8_t distanceFalloff(uint32_t distance,
                                             uint32_t width) {
    width = std::max<uint32_t>(width, 1U);
    if (distance > width) {
        return 0;
    }
    return static_cast<uint8_t>(((width + 1U - distance) * 255U) /
                                (width + 1U));
}

[[nodiscard]] inline uint8_t triangle8(uint8_t phase) {
    return phase < 128U ? static_cast<uint8_t>(phase * 2U)
                        : static_cast<uint8_t>((255U - phase) * 2U);
}

[[nodiscard]] inline uint8_t angularDistance(uint8_t a, uint8_t b) {
    const uint8_t diff = a > b ? static_cast<uint8_t>(a - b)
                               : static_cast<uint8_t>(b - a);
    return std::min<uint8_t>(diff, Config::spokeCount - diff);
}

[[nodiscard]] inline uint16_t hash16(uint16_t value) {
    value ^= static_cast<uint16_t>(value >> 7U);
    value = static_cast<uint16_t>(value * 2053U);
    value ^= static_cast<uint16_t>(value >> 9U);
    value = static_cast<uint16_t>(value * 13849U);
    value ^= static_cast<uint16_t>(value >> 7U);
    return value;
}

inline void drawCenterWave(PrimitiveCanvas &canvas,
                           const PrimitiveParams &params) {
    const uint32_t duration = nonzero(params.durationMs, 1200U);
    const uint32_t width = std::max<uint32_t>(params.width, 1U);
    const uint32_t extent = Config::ringCount + width + 1U;
    const uint32_t head = (params.elapsedMs * extent) / duration;

    for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
        const auto diff = head > radial ? head - radial : radial - head;
        const auto scale = distanceFalloff(diff, width);
        if (scale == 0) {
            continue;
        }
        canvas.ring(radial,
                    HsvColor{.hue = params.hue,
                             .saturation = params.saturation,
                             .value = Render::scale8(params.value, scale)});
    }
}

inline void drawRingSweep(PrimitiveCanvas &canvas,
                          const PrimitiveParams &params) {
    drawCenterWave(canvas, params);
}

inline void drawSpokeSweep(PrimitiveCanvas &canvas,
                           const PrimitiveParams &params) {
    const uint32_t duration = nonzero(params.durationMs, 1600U);
    const uint8_t head = static_cast<uint8_t>(
        ((params.elapsedMs * Config::spokeCount) / duration) %
        Config::spokeCount);
    const uint32_t width = std::max<uint32_t>(params.width, 1U);

    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        const auto scale = distanceFalloff(angularDistance(spoke, head), width);
        if (scale == 0) {
            continue;
        }
        canvas.spoke(spoke,
                     HsvColor{.hue = params.hue,
                              .saturation = params.saturation,
                              .value = Render::scale8(params.value, scale)});
    }
}

inline void drawTheaterChase(PrimitiveCanvas &canvas,
                             const PrimitiveParams &params) {
    constexpr uint8_t spacing = 3;
    const uint32_t stepMs = 40U + (255U - params.speed);
    const uint8_t phase =
        static_cast<uint8_t>((params.elapsedMs / stepMs) % spacing);

    for (size_t local = 0; local < Config::ownedPixelCount; ++local) {
        const auto physical = LedTopology::OwnedPixels::physicalIndex(
            static_cast<LedTopology::LocalPixelIndex>(local));
        if (static_cast<uint8_t>((physical + phase) % spacing) != 0U) {
            continue;
        }
        canvas.physicalPixel(
            physical, HsvColor{.hue = params.hue,
                               .saturation = params.saturation,
                               .value = params.value});
    }
}

inline void drawTwinkle(PrimitiveCanvas &canvas,
                        const PrimitiveParams &params) {
    const uint16_t time =
        static_cast<uint16_t>(params.elapsedMs / (20U + (255U - params.speed)));
    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const uint16_t seed =
                static_cast<uint16_t>(spoke * 71U + radial * 131U);
            const uint16_t hashed = hash16(seed);
            if (static_cast<uint8_t>(hashed) > params.density) {
                continue;
            }
            const auto pulse =
                triangle8(static_cast<uint8_t>(time + (hashed >> 8U)));
            canvas.pixel(spoke, radial,
                         HsvColor{.hue = static_cast<uint8_t>(
                                      params.hue + (hashed & 0x1FU)),
                                  .saturation = params.saturation,
                                  .value = Render::scale8(params.value, pulse)});
        }
    }
}

inline void drawSparkle(PrimitiveCanvas &canvas,
                        const PrimitiveParams &params) {
    const uint16_t time =
        static_cast<uint16_t>(params.elapsedMs / (10U + (255U - params.speed)));
    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const uint16_t hashed =
                hash16(static_cast<uint16_t>(time + spoke * 97U +
                                             radial * 193U));
            if (static_cast<uint8_t>(hashed) > params.density) {
                continue;
            }
            canvas.pixel(spoke, radial,
                         HsvColor{.hue = params.hue,
                                  .saturation = params.saturation,
                                  .value = params.value});
        }
    }
}

inline void drawExplosion(PrimitiveCanvas &canvas,
                          const PrimitiveParams &params) {
    const uint32_t duration = nonzero(params.durationMs, 1400U);
    const uint8_t originSpoke = static_cast<uint8_t>(
        ((params.elapsedMs / duration) * 5U) % Config::spokeCount);
    constexpr uint8_t originRing = 0;
    const uint32_t maxDistance =
        Config::ringCount + (Config::spokeCount / 2U) * 3U;
    const uint32_t head = (params.elapsedMs * (maxDistance + params.width)) /
                          duration;
    const uint32_t width = std::max<uint32_t>(params.width * 2U, 2U);

    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const uint32_t distance =
                (radial > originRing ? radial - originRing
                                     : originRing - radial) +
                angularDistance(spoke, originSpoke) * 3U;
            const auto scale = distanceFalloff(
                head > distance ? head - distance : distance - head, width);
            if (scale == 0) {
                continue;
            }
            canvas.pixel(spoke, radial,
                         HsvColor{.hue = static_cast<uint8_t>(
                                      params.hue + radial),
                                  .saturation = params.saturation,
                                  .value = Render::scale8(params.value, scale)});
        }
    }
}

inline void drawFire(PrimitiveCanvas &canvas, const PrimitiveParams &params) {
    const uint16_t time = static_cast<uint16_t>(
        params.elapsedMs / std::max<uint32_t>(12U, 260U - params.speed));
    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const uint8_t base = static_cast<uint8_t>(
                ((Config::ringCount - radial) * params.value) /
                Config::ringCount);
            const uint8_t flicker = static_cast<uint8_t>(
                hash16(static_cast<uint16_t>(time + spoke * 41U +
                                             radial * 113U)) >>
                8U);
            const uint8_t value =
                Render::scale8(base, static_cast<uint8_t>(160U + flicker / 3U));
            canvas.pixel(spoke, radial,
                         HsvColor{.hue = static_cast<uint8_t>(
                                      params.hue + radial / 3U),
                                  .saturation = params.saturation,
                                  .value = value});
        }
    }
}

inline void drawComet(PrimitiveCanvas &canvas, const PrimitiveParams &params) {
    const uint32_t duration = nonzero(params.durationMs, 1800U);
    const uint8_t head = static_cast<uint8_t>(
        ((params.elapsedMs * Config::spokeCount) / duration) %
        Config::spokeCount);
    const uint32_t width = std::max<uint32_t>(params.width, 1U);
    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        const auto scale = distanceFalloff(angularDistance(spoke, head), width);
        if (scale == 0) {
            continue;
        }
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const auto radialScale = static_cast<uint8_t>(
                128U + ((static_cast<uint32_t>(radial) * 127U) /
                        Config::ringCount));
            canvas.pixel(spoke, radial,
                         HsvColor{.hue = static_cast<uint8_t>(
                                      params.hue + radial),
                                  .saturation = params.saturation,
                                  .value = Render::scale8(
                                      Render::scale8(params.value, scale),
                                      radialScale)});
        }
    }
}

inline void drawRainbow(PrimitiveCanvas &canvas, const PrimitiveParams &params) {
    const uint8_t baseHue = static_cast<uint8_t>(params.elapsedMs / 12U);
    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            canvas.pixel(spoke, radial,
                         HsvColor{.hue = static_cast<uint8_t>(
                                      baseHue + spoke * 16U + radial * 3U),
                                  .saturation = params.saturation,
                                  .value = params.value});
        }
    }
}

inline void drawPrimitiveDemo(PrimitiveCanvas &canvas, PrimitiveKind kind,
                              const PrimitiveParams &params) {
    switch (kind) {
    case PrimitiveKind::RingSweep:
        drawRingSweep(canvas, params);
        break;
    case PrimitiveKind::SpokeSweep:
        drawSpokeSweep(canvas, params);
        break;
    case PrimitiveKind::TheaterChase:
        drawTheaterChase(canvas, params);
        break;
    case PrimitiveKind::Twinkle:
        drawTwinkle(canvas, params);
        break;
    case PrimitiveKind::Sparkle:
        drawSparkle(canvas, params);
        break;
    case PrimitiveKind::Explosion:
        drawExplosion(canvas, params);
        break;
    case PrimitiveKind::Fire:
        drawFire(canvas, params);
        break;
    case PrimitiveKind::Comet:
        drawComet(canvas, params);
        break;
    case PrimitiveKind::Rainbow:
        drawRainbow(canvas, params);
        break;
    default:
        drawExplosion(canvas, params);
        break;
    }
}

} // namespace Totem::LedDisplay::detail
