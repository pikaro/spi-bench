#pragma once

#include "LedDisplay/Interfaces/Blend.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include "LedTopology/Facade.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Totem::LedDisplay::Primitives {

static_assert(Config::totalPixelCount ==
                  (Config::spokeCount * Config::ringCount),
              "Logical LED map must cover every display pixel");

class Canvas {
  public:
    static constexpr LedTopology::LocalPixelIndex invalidLocalPixel =
        std::numeric_limits<LedTopology::LocalPixelIndex>::max();

    using LogicalToLocalMap =
        std::span<const LedTopology::LocalPixelIndex, Config::totalPixelCount>;

    explicit Canvas(std::span<HsvColor> frame,
                    LogicalToLocalMap logicalToLocal)
        : _frame(frame), _logicalToLocal(logicalToLocal) {}

    [[nodiscard]] static constexpr size_t logicalIndex(uint8_t spoke,
                                                       uint8_t radial) {
        return (static_cast<size_t>(spoke) * Config::ringCount) + radial;
    }

    void fill(HsvColor color, BlendOp op = BlendOp::Replace) {
        for (auto &pixel : _frame) {
            pixel = Renderers::GenericRenderer::blend(pixel, color, op);
        }
    }

    void pixel(uint8_t spoke, uint8_t radial, HsvColor color,
               BlendOp op = BlendOp::MaxValue) {
        const auto local = _logicalToLocal[logicalIndex(spoke, radial)];
        if (local == invalidLocalPixel) {
            return;
        }
        auto &dst = _frame[static_cast<size_t>(local)];
        dst = Renderers::GenericRenderer::blend(dst, color, op);
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
        dst = Renderers::GenericRenderer::blend(dst, color, op);
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
        constexpr uint8_t lastRadialIndex = Config::ringCount - 1U;
        const auto upper = std::min<uint8_t>(
            end, lastRadialIndex);
        for (uint8_t radial = start; radial <= upper; ++radial) {
            pixel(spoke, radial, color, op);
            if (radial == UINT8_MAX) {
                break;
            }
        }
    }

  private:
    std::span<HsvColor> _frame;
    LogicalToLocalMap _logicalToLocal;
};

} // namespace Totem::LedDisplay::Primitives
