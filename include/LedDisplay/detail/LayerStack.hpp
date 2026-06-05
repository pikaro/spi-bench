#pragma once

#include "LedDisplay/Interfaces/AnimationStyle.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/Config.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "LedDisplay/detail/Compositor.hpp"
#include "magic_enum/magic_enum.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Totem::LedDisplay::detail {

struct LayerConfig {
    AnimationStyle style{};
    uint8_t decay = 0;
    bool clearEachFrame = true;
    bool enabled = true;
};

inline constexpr size_t layerCount = magic_enum::enum_count<Layer>();
inline constexpr uint8_t layerFullOpacity = std::numeric_limits<uint8_t>::max();
inline constexpr uint8_t layerWheelOpacity = 192;
inline constexpr uint8_t persistentFftLayerDecay = 4;
inline constexpr uint8_t persistentEffectLayerDecay = 3;
inline constexpr uint8_t debugLayerDecay = 8;

[[nodiscard]] inline constexpr size_t layerIndex(Layer layer) {
    return static_cast<size_t>(layer);
}

[[nodiscard]] inline constexpr std::array<LayerConfig, layerCount>
defaultLayerConfigs() {
    std::array<LayerConfig, layerCount> configs{};
    configs[layerIndex(Layer::Background)] = LayerConfig{
        .style = {.blendOp = BlendOp::MaxValue, .opacity = layerFullOpacity},
        .decay = 0,
        .clearEachFrame = true,
        .enabled = true,
    };
    configs[layerIndex(Layer::Fft)] = LayerConfig{
        .style = {.blendOp = BlendOp::MaxValue, .opacity = layerFullOpacity},
        .decay = persistentFftLayerDecay,
        .clearEachFrame = false,
        .enabled = true,
    };
    configs[layerIndex(Layer::FftAlt)] = LayerConfig{
        .style = {.blendOp = BlendOp::MaxValue, .opacity = layerFullOpacity},
        .decay = persistentFftLayerDecay,
        .clearEachFrame = false,
        .enabled = false,
    };
    configs[layerIndex(Layer::Effect)] = LayerConfig{
        .style = {.blendOp = BlendOp::MaxValue, .opacity = layerFullOpacity},
        .decay = persistentEffectLayerDecay,
        .clearEachFrame = false,
        .enabled = true,
    };
    configs[layerIndex(Layer::TransientEffect)] = LayerConfig{
        .style = {.blendOp = BlendOp::MaxValue, .opacity = layerFullOpacity},
        .decay = 0,
        .clearEachFrame = true,
        .enabled = true,
    };
    configs[layerIndex(Layer::Wheel)] = LayerConfig{
        .style = {.blendOp = BlendOp::Alpha, .opacity = layerWheelOpacity},
        .decay = 0,
        .clearEachFrame = true,
        .enabled = true,
    };
    configs[layerIndex(Layer::Debug)] = LayerConfig{
        .style = {.blendOp = BlendOp::AddValue, .opacity = layerFullOpacity},
        .decay = debugLayerDecay,
        .clearEachFrame = false,
        .enabled = true,
    };
    return configs;
}

class LayerStack {
  public:
    static constexpr size_t layerCount = detail::layerCount;

    LayerStack() {
        constexpr auto configs = defaultLayerConfigs();
        for (size_t i = 0; i < layerCount; ++i) {
            _layers[i].config = configs[i];
        }
    }

    void beginFrame(uint32_t frame) {
        (void)frame;
        for (size_t i = 0; i < layerCount; ++i) {
            auto &layer = _layers[i];
            if (!layer.config.enabled) {
                continue;
            }
            if (layer.config.clearEachFrame) {
                Compositor::clear(layer.frame);
            } else {
                Compositor::decay(layer.frame, layer.config.decay);
            }
        }
    }

    void clearScratch() { Compositor::clear(_scratch); }

    [[nodiscard]] std::span<HsvColor> scratch() { return _scratch; }

    [[nodiscard]] bool enabled(Layer layer) const {
        return _layers[layerIndex(layer)].config.enabled;
    }

    [[nodiscard]] uint8_t opacity(Layer layer) const {
        return _layers[layerIndex(layer)].config.style.opacity;
    }

    void setEnabled(Layer layer, bool enabled) {
        auto &state = _layers[layerIndex(layer)];
        if (state.config.enabled == enabled) {
            return;
        }
        state.config.enabled = enabled;
        if (!enabled) {
            Compositor::clear(state.frame);
        }
    }

    void setOpacity(Layer layer, uint8_t opacity) {
        _layers[layerIndex(layer)].config.style.opacity = opacity;
    }

    void blendScratch(Layer layer, AnimationStyle style) {
        auto &state = _layers[layerIndex(layer)];
        if (!state.config.enabled) {
            return;
        }
        Compositor::blend(
            {.src = _scratch, .dst = state.frame, .style = style});
    }

    void compose(std::span<HsvColor> out) {
        Compositor::clear(out);
        for (auto &layer : _layers) {
            if (!layer.config.enabled || layer.config.style.opacity == 0) {
                continue;
            }
            Compositor::blend(
                {.src = layer.frame, .dst = out, .style = layer.config.style});
        }
    }

  private:
    struct LayerState {
        std::array<HsvColor, Config::ownedPixelCount> frame{};
        LayerConfig config{};
    };

    std::array<LayerState, layerCount> _layers{};
    std::array<HsvColor, Config::ownedPixelCount> _scratch{};
};

} // namespace Totem::LedDisplay::detail
