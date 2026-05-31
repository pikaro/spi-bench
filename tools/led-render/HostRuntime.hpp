#pragma once

#include "Audio/Interfaces/Wire.hpp"
#include "Json.hpp"
#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/Layer.hpp"
#include "LedDisplay/Interfaces/RenderContext.hpp"
#include "LedDisplay/Primitives/Canvas.hpp"
#include "LedDisplay/Renderers/GenericRenderer.hpp"
#include "LedDisplay/detail/LayerStack.hpp"
#include "LedTopology/Facade.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include "TraceFormat.hpp"
#include "Types/Angle.hpp"
#include "magic_enum/magic_enum.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Totem::LedRender {

using HsvColor = Totem::LedDisplay::HsvColor;
using RgbColor = Totem::LedDisplay::RgbColor;
using Layer = Totem::LedDisplay::Layer;
using AnimationInputSnapshot = Totem::LedDisplay::AnimationInputSnapshot;
using AnimationRenderContext = Totem::LedDisplay::AnimationRenderContext;
using Canvas = Totem::LedDisplay::Primitives::Canvas;

struct SyntheticAudioChannel {
    uint8_t base = 0;
    uint8_t amp = 0;
    uint16_t periodMs = 1000;
    uint8_t phase = 0;
};

struct SyntheticAudio {
    bool enabled = false;
    SyntheticAudioChannel bass{.base = 48, .amp = 160, .periodMs = 900};
    SyntheticAudioChannel mid{.base = 40, .amp = 120, .periodMs = 1400};
    SyntheticAudioChannel high{.base = 18, .amp = 120, .periodMs = 220};
};

struct ActiveAnimationSpecBase {
    uint32_t startMs = 0;
    uint32_t durationMs = 0;
    Layer layer = Layer::Effect;
};

struct RenderRequest {
    uint32_t firstFrame = 0;
    uint32_t lastFrame = 0;
    uint32_t fps = LedDisplayConfig::targetFps;
    uint8_t hueOffset = 0;
    uint8_t rotationOffset = 0;
    bool pipelineMode = true;
    bool includeScratch = false;
    std::string outputPath;
    std::string sourceJson;
    AnimationInputSnapshot inputs{};
    SyntheticAudio audio{};
};

[[nodiscard]] inline uint32_t frameStepUs(uint32_t fps) {
    return fps == 0 ? 0 : 1000000UL / fps;
}

[[nodiscard]] inline uint32_t frameTimeMs(uint32_t frame, uint32_t fps) {
    const auto step = frameStepUs(fps);
    return static_cast<uint32_t>((static_cast<uint64_t>(frame) * step) /
                                 1000ULL);
}

[[nodiscard]] inline uint32_t frameCount(uint32_t firstFrame,
                                         uint32_t lastFrame) {
    return lastFrame >= firstFrame ? (lastFrame - firstFrame + 1U) : 0U;
}

[[nodiscard]] inline uint8_t rotationSpokeOffset(uint8_t rotationOffset) {
    constexpr uint32_t angleSteps = 256U;
    constexpr uint32_t roundToNearestBias = angleSteps / 2U;
    const auto scaled =
        (static_cast<uint32_t>(rotationOffset) * LedDisplayConfig::spokeCount) +
        roundToNearestBias;
    return static_cast<uint8_t>((scaled / angleSteps) %
                                LedDisplayConfig::spokeCount);
}

[[nodiscard]] inline auto buildLogicalToLocalMap(uint8_t rotationOffset) {
    std::array<Totem::LedTopology::LocalPixelIndex,
               LedDisplayConfig::totalPixelCount>
        map{};
    const auto spokeOffset = rotationSpokeOffset(rotationOffset);
    for (uint8_t spoke = 0; spoke < LedDisplayConfig::spokeCount; ++spoke) {
        const auto rotatedSpoke =
            static_cast<uint8_t>((static_cast<uint32_t>(spoke) + spokeOffset) %
                                 LedDisplayConfig::spokeCount);
        for (uint8_t radial = 0; radial < LedDisplayConfig::ringCount;
             ++radial) {
            const auto logical = Canvas::logicalIndex(spoke, radial);
            const auto physical =
                Totem::LedTopology::Umbrella::physicalFor(rotatedSpoke, radial);
            const auto rotated =
                Totem::LedTopology::OwnedPixels::localIndex(physical);
            map[logical] =
                static_cast<Totem::LedTopology::LocalPixelIndex>(rotated);
        }
    }
    return map;
}

inline void hsvToRgb(std::span<const HsvColor> hsv, std::span<RgbColor> rgb) {
    const auto count = std::min(hsv.size(), rgb.size());
    for (size_t i = 0; i < count; ++i) {
        rgb[i] =
            Totem::LedDisplay::Renderers::GenericRenderer::hsvToRgb(hsv[i]);
    }
}

[[nodiscard]] inline bool readOptionalUint32(const JsonValue &object,
                                             std::string_view key,
                                             uint32_t &out,
                                             std::string &error) {
    const auto *value = object.find(key);
    if (value == nullptr) {
        return true;
    }
    return readJsonInteger(*value, out, error, key);
}

[[nodiscard]] inline bool readOptionalUint8(const JsonValue &object,
                                            std::string_view key, uint8_t &out,
                                            std::string &error) {
    const auto *value = object.find(key);
    if (value == nullptr) {
        return true;
    }
    return readJsonInteger(*value, out, error, key);
}

[[nodiscard]] inline bool readOptionalUint16(const JsonValue &object,
                                             std::string_view key,
                                             uint16_t &out,
                                             std::string &error) {
    const auto *value = object.find(key);
    if (value == nullptr) {
        return true;
    }
    return readJsonInteger(*value, out, error, key);
}

[[nodiscard]] inline bool readLayer(std::string_view name, Layer &layer) {
    const auto parsed = magic_enum::enum_cast<Layer>(name);
    if (!parsed.has_value()) {
        return false;
    }
    layer = *parsed;
    return true;
}

[[nodiscard]] inline std::string layerName(Layer layer) {
    const auto name = magic_enum::enum_name(layer);
    return name.empty() ? "Unknown" : std::string{name};
}

[[nodiscard]] inline bool readOptionalLayer(const JsonValue &object,
                                            std::string_view key, Layer &out,
                                            std::string &error) {
    const auto *value = object.find(key);
    if (value == nullptr) {
        return true;
    }
    std::string name;
    if (!readJsonString(*value, name, error, key)) {
        return false;
    }
    if (!readLayer(name, out)) {
        error = "Unknown layer '" + name + "'";
        return false;
    }
    return true;
}

[[nodiscard]] inline bool readInputSnapshot(const JsonValue &root,
                                            AnimationInputSnapshot &inputs,
                                            std::string &error) {
    const auto *inputValue = root.find("inputs");
    if (inputValue == nullptr) {
        return true;
    }
    if (inputValue->kind != JsonValue::Kind::Object) {
        error = "Field 'inputs' must be an object";
        return false;
    }

    if (const auto *fft = inputValue->find("fft"); fft != nullptr) {
        if (fft->kind != JsonValue::Kind::Object) {
            error = "Field 'inputs.fft' must be an object";
            return false;
        }
        auto &frame = inputs.fftFrame;
        if (!readOptionalUint16(*fft, "subBass", frame.subBass, error) ||
            !readOptionalUint16(*fft, "bass", frame.bass, error) ||
            !readOptionalUint16(*fft, "lowMid", frame.lowMid, error) ||
            !readOptionalUint16(*fft, "mid", frame.mid, error) ||
            !readOptionalUint16(*fft, "highMid", frame.highMid, error) ||
            !readOptionalUint16(*fft, "presence", frame.presence, error) ||
            !readOptionalUint16(*fft, "brilliance", frame.brilliance, error) ||
            !readOptionalUint16(*fft, "air", frame.air, error)) {
            return false;
        }
        inputs.hasFftFrame = true;
    }

    if (const auto *wheel = inputValue->find("wheel"); wheel != nullptr) {
        if (wheel->kind != JsonValue::Kind::Object) {
            error = "Field 'inputs.wheel' must be an object";
            return false;
        }
        uint16_t position = 0;
        uint16_t delta = 0;
        if (!readOptionalUint16(*wheel, "position", position, error) ||
            !readOptionalUint16(*wheel, "delta", delta, error)) {
            return false;
        }
        inputs.wheelState.position = Angle<uint16_t>::fromRaw(position);
        inputs.wheelState.delta = Angle<uint16_t>::fromRaw(delta);
        inputs.hasWheelState = true;
    }

    if (const auto *peak = inputValue->find("peak"); peak != nullptr) {
        if (peak->kind != JsonValue::Kind::Object) {
            error = "Field 'inputs.peak' must be an object";
            return false;
        }
        std::string groupName = "Bass";
        if (const auto *group = peak->find("group"); group != nullptr) {
            if (group->kind == JsonValue::Kind::String) {
                if (!readJsonString(*group, groupName, error,
                                    "inputs.peak.group")) {
                    return false;
                }
                const auto parsed =
                    magic_enum::enum_cast<Totem::Audio::PeakGroup>(groupName);
                if (!parsed.has_value()) {
                    error = "Unknown peak group '" + groupName + "'";
                    return false;
                }
                inputs.peakEvent.group = *parsed;
            } else {
                uint8_t rawGroup = 0;
                if (!readJsonInteger(*group, rawGroup, error,
                                     "inputs.peak.group")) {
                    return false;
                }
                inputs.peakEvent.group =
                    static_cast<Totem::Audio::PeakGroup>(rawGroup);
            }
        }
        if (!readOptionalUint8(*peak, "energy", inputs.peakEvent.energy,
                               error) ||
            !readOptionalUint8(*peak, "lowerBand", inputs.peakEvent.lowerBand,
                               error) ||
            !readOptionalUint8(*peak, "upperBand", inputs.peakEvent.upperBand,
                               error) ||
            !readOptionalUint32(*peak, "frameSequence",
                                inputs.peakEvent.frameSequence, error)) {
            return false;
        }
        inputs.hasPeakEvent = true;
    }

    return true;
}

[[nodiscard]] inline bool readSyntheticAudioChannel(
    const JsonValue &object, std::string_view key, SyntheticAudioChannel &out,
    std::string &error) {
    const auto *value = object.find(key);
    if (value == nullptr) {
        return true;
    }
    if (value->kind != JsonValue::Kind::Object) {
        error = "Field 'audio." + std::string(key) + "' must be an object";
        return false;
    }
    return readOptionalUint8(*value, "base", out.base, error) &&
           readOptionalUint8(*value, "amp", out.amp, error) &&
           readOptionalUint16(*value, "period_ms", out.periodMs, error) &&
           readOptionalUint8(*value, "phase", out.phase, error);
}

[[nodiscard]] inline bool readSyntheticAudio(const JsonValue &root,
                                             SyntheticAudio &audio,
                                             std::string &error) {
    const auto *value = root.find("audio");
    if (value == nullptr) {
        return true;
    }
    if (value->kind != JsonValue::Kind::Object) {
        error = "Field 'audio' must be an object";
        return false;
    }
    audio.enabled = true;
    return readSyntheticAudioChannel(*value, "bass", audio.bass, error) &&
           readSyntheticAudioChannel(*value, "mid", audio.mid, error) &&
           readSyntheticAudioChannel(*value, "high", audio.high, error);
}

[[nodiscard]] inline uint8_t syntheticAudioValue(SyntheticAudioChannel channel,
                                                 uint32_t nowMs) {
    if (channel.periodMs == 0) {
        return channel.base;
    }
    const auto phase = static_cast<uint8_t>(
        channel.phase +
        ((static_cast<uint64_t>(nowMs) * 256ULL) / channel.periodMs));
    return Totem::LedDisplay::Renderers::GenericRenderer::qadd8(
        channel.base,
        Totem::LedDisplay::Renderers::GenericRenderer::scale8(
            Totem::LedDisplay::Renderers::GenericRenderer::sine8(phase),
            channel.amp));
}

inline void applySyntheticAudio(const SyntheticAudio &audio, uint32_t nowMs,
                                AnimationInputSnapshot &inputs) {
    if (!audio.enabled) {
        return;
    }
    const auto bass = syntheticAudioValue(audio.bass, nowMs);
    const auto mid = syntheticAudioValue(audio.mid, nowMs);
    const auto high = syntheticAudioValue(audio.high, nowMs);
    inputs.fftFrame.subBass = bass;
    inputs.fftFrame.bass = bass;
    inputs.fftFrame.lowMid = mid;
    inputs.fftFrame.mid = mid;
    inputs.fftFrame.highMid = mid;
    inputs.fftFrame.presence = high;
    inputs.fftFrame.brilliance = high;
    inputs.fftFrame.air = high;
    inputs.hasFftFrame = true;
}

} // namespace Totem::LedRender
