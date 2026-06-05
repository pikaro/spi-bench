#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Audio/Interfaces/Wire.hpp"
#include "Buttons/Interfaces/EventFactory.hpp"
#include "Buttons/Interfaces/Wire.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Data/Peripherals.hpp"
#include "LedDisplay/Animations/CenterWave/Command.hpp"
#include "LedDisplay/Animations/OrbitSparks/Command.hpp"
#include "LedDisplay/Animations/SpectralIris/Command.hpp"
#include "LedDisplay/Animations/SpectralWeave/Command.hpp"
#include "LedDisplay/Animations/SineWave/Command.hpp"
#include "LedDisplay/Animations/SineWave/Config.hpp"
#include "LedDisplay/Animations/StainedCells/Command.hpp"
#include "LedDisplay/Animations/WheelIndicator/Command.hpp"
#include "LedDisplay/Animations/WheelIndicator/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include "LedDisplay/Interfaces/LayerControl.hpp"
#include "LedPwm/Interfaces/CommandEvent.hpp"
#include "LedPwm/Interfaces/CommandEventFactory.hpp"
#include "LedPwm/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "Queue/Facade.hpp"
#include "Services/Commands.hpp"
#include "Services/PubSub.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include "Wheel/Interfaces/Wire.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace MasterOrchestration {

struct WheelMapping {
    bool publishHueOffset = true;
    bool publishRotationOffset = true;
    bool publishIndicatorUpdate = false;
    float hueTurnsPerWheelTurn = 1.0F;
    float rotationTurnsPerWheelTurn = 1.0F;
    uint32_t publishMinIntervalMs = 20;
    Totem::LedDisplay::Animations::WheelIndicatorConfig indicator{};
};

struct PeakWaveMapping {
    bool publishCenterWave = false;
    // std::array<uint8_t, Totem::Audio::peakGroupCount> hueByGroup{{0, 24,
    // 48}};
    std::array<uint8_t, Totem::Audio::peakGroupCount> hueByGroup{{0, 32, 64}};
    std::array<std::pair<uint8_t, uint8_t>, Totem::Audio::peakGroupCount>
        // saturationByGroup{{{240, 15}, {230, 25}, {220, 25}}};
        saturationByGroup{{{245, 10}, {240, 15}, {235, 20}}};
    std::array<std::pair<uint8_t, uint8_t>, Totem::Audio::peakGroupCount>
        valueByGroup{{{150, 100}, {100, 100}, {50, 100}}};
    // valueByGroup{{{255, 0}, {255, 0}, {255, 0}}};
    uint16_t lifetimeMs = 2000;
    uint8_t rise = 2;
    uint8_t peak = 2;
    uint8_t wake = 6;
    uint32_t minIntervalMs = 50;
};

enum class FftVisualKind : uint8_t {
    SpectralWeave,
    SpectralIris,
    OrbitSparks,
    StainedCells,
};

struct FftVisualPreset {
    FftVisualKind kind = FftVisualKind::SpectralWeave;
    const char *name = "weave";
    uint32_t dwellMs = 45000;
    uint16_t fadeMs = 10000;
};

struct FftVisualMapping {
    bool publishOnStartup = true;
    uint16_t fftRequestId = 3;
    uint16_t fftAltRequestId = 4;
    uint32_t refreshIntervalMs = 5000;
    std::array<FftVisualPreset, 4> presets{{
        {.kind = FftVisualKind::SpectralWeave,
         .name = "weave",
         .dwellMs = 45000,
         .fadeMs = 10000},
        {.kind = FftVisualKind::SpectralIris,
         .name = "iris",
         .dwellMs = 45000,
         .fadeMs = 10000},
        {.kind = FftVisualKind::OrbitSparks,
         .name = "sparks",
         .dwellMs = 45000,
         .fadeMs = 10000},
        {.kind = FftVisualKind::StainedCells,
         .name = "cells",
         .dwellMs = 45000,
         .fadeMs = 10000},
    }};
};

struct DropWaveMapping {
    bool publishCenterWave = true;
    uint32_t quietWindowMs = 2000;
    uint32_t minIntervalMs = 2500;
    uint16_t lifetimeMs = 1400;
    std::array<uint8_t, Totem::Audio::peakGroupCount> hueByGroup{{0, 32, 64}};
    uint8_t saturation = 255;
    uint8_t value = 180;
    uint8_t rise = 2;
    uint8_t peak = 1;
    uint8_t wake = 4;
};

struct BulbPulseProfile {
    Totem::LedPwm::Brightness minPeak{
        Totem::LedPwm::Brightness::fromPercent(4.0F)};
    Totem::LedPwm::Brightness maxPeak{
        Totem::LedPwm::Brightness::fromPercent(12.0F)};
    uint16_t minRiseMs = 40;
    uint16_t maxRiseMs = 100;
    uint16_t minHoldMs = 0;
    uint16_t maxHoldMs = 40;
    uint16_t minFallMs = 160;
    uint16_t maxFallMs = 420;
    uint32_t minIntervalMs = 120;
};

struct IoLedMapping {
    bool publishStartupState = true;
    uint32_t stateRefreshMs = 5000;
    Totem::LedPwm::Glitter goldGlitter{
        .base = Totem::LedPwm::Brightness::fromPercent(3.0F),
        .glimmerPeak = Totem::LedPwm::Brightness::fromPercent(22.0F),
        .sparklePeak = Totem::LedPwm::Brightness::fromPercent(100.0F),
        .stepMs = 110,
        .sparkleMs = 26,
        .sparkleChance = 42,
        .seed = 0xA53DU,
    };

    Totem::LedPwm::Brightness bulbBase{Totem::LedPwm::Brightness::off()};
    bool publishPeakFlicker = true;
    BulbPulseProfile bassPulse{
        .minPeak = Totem::LedPwm::Brightness::fromPercent(4.0F),
        .maxPeak = Totem::LedPwm::Brightness::fromPercent(12.0F),
        .minRiseMs = 80,
        .maxRiseMs = 140,
        .minHoldMs = 20,
        .maxHoldMs = 80,
        .minFallMs = 550,
        .maxFallMs = 1000,
        .minIntervalMs = 220,
    };
    BulbPulseProfile midPulse{
        .minPeak = Totem::LedPwm::Brightness::fromPercent(5.0F),
        .maxPeak = Totem::LedPwm::Brightness::fromPercent(16.0F),
        .minRiseMs = 35,
        .maxRiseMs = 80,
        .minHoldMs = 10,
        .maxHoldMs = 50,
        .minFallMs = 320,
        .maxFallMs = 620,
        .minIntervalMs = 150,
    };
    BulbPulseProfile treblePulse{
        .minPeak = Totem::LedPwm::Brightness::fromPercent(8.0F),
        .maxPeak = Totem::LedPwm::Brightness::fromPercent(20.0F),
        .minRiseMs = 8,
        .maxRiseMs = 22,
        .minHoldMs = 0,
        .maxHoldMs = 40,
        .minFallMs = 100,
        .maxFallMs = 280,
        .minIntervalMs = 80,
    };
};

struct BellMapping {
    bool publish = true;
    uint16_t publishDelayMs = 100;
    uint16_t minIntervalMs = 1000;
    Totem::LedDisplay::Animations::SineWaveConfig config{
        .hue = 128,
        .saturation = 255,
        .value = 255,
        .baseValue = 50,
        .width = 8,
        .durationMs = 1500,
        .wavelength = 8,
        .outerOrigin = true,
        // .travelRings = 0,
        // .spokeGainPct = 200,
        // .tailDecay = 8,
        // .peakHold = 200,
    };
};

struct BeatMapping {
    bool logStateTransitions = true;
};

struct LayerMapping {
    bool publishStartupState = true;
    uint32_t retryIntervalMs = 5000;
    bool backgroundActive = false;
    bool fftActive = true;
    bool fftAltActive = false;
    bool effectActive = true;
    bool transientEffectActive = true;
    bool wheelActive = false;
    bool debugActive = true;
};

struct Config {
    WheelMapping wheel{};
    FftVisualMapping fftVisuals{};
    DropWaveMapping dropWave{};
    PeakWaveMapping peakWave{};
    BeatMapping beat{};
    LayerMapping layers{};
    IoLedMapping ioLed{};
    BellMapping bell{};
};

inline constexpr Config config{};
inline constexpr size_t wheelEventQueueSize = 8;
inline constexpr size_t beatEventQueueSize = 8;
inline constexpr size_t peakEventQueueSize = 8;
inline constexpr size_t buttonEventQueueSize = 4;

namespace detail {

inline Totem::Queue::Platform::Storage<Totem::Wheel::WheelState,
                                       wheelEventQueueSize>
    wheelEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::Audio::BeatEvent,
                                       beatEventQueueSize>
    beatEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::Audio::PeakEvent,
                                       peakEventQueueSize>
    peakEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::Buttons::ButtonEvent,
                                       buttonEventQueueSize>
    buttonEventQueueStorage{};
inline Totem::Queue::Handle wheelEventQueue = nullptr;
inline Totem::Queue::Handle beatEventQueue = nullptr;
inline Totem::Queue::Handle peakEventQueue = nullptr;
inline Totem::Queue::Handle buttonEventQueue = nullptr;
inline Totem::PubSubBackend::SubscriberKey wheelSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey beatSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey peakSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey buttonSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey animationSubscription = 0;
inline bool calibrateAudioCommandRegistered = false;
inline Angle<uint16_t> wheelOffset{};
inline bool wheelOffsetDirty = false;
inline bool wheelIndicatorDirty = false;
inline bool layerStartupStatePublished = false;
inline bool ioStartupPublished = false;
inline bool fftVisualPublished = false;
inline bool fftVisualSuppressed = false;
inline bool fftFadeInProgress = false;
inline Totem::LedDisplay::Layer activeFftLayer = Totem::LedDisplay::Layer::Fft;
inline Totem::LedDisplay::Layer hiddenFftLayer =
    Totem::LedDisplay::Layer::FftAlt;
inline size_t activeFftPresetIndex = 0;
inline size_t pendingFftPresetIndex = 0;
inline uint32_t lastWheelPublishMs = 0;
inline uint32_t lastLayerStartupStatePublishMs = 0;
inline uint32_t lastIoStartupPublishMs = 0;
inline uint32_t lastFftVisualPublishMs = 0;
inline uint32_t lastFftSwitchMs = 0;
inline uint32_t fftFadeStartMs = 0;
inline uint16_t fftFadeDurationMs = 0;
inline uint32_t lastBellSinelonMs = 0;
inline uint32_t lastAnyPeakMs = 0;
inline uint32_t lastDropWaveMs = 0;
inline uint32_t lastBeatSequence = 0;
inline Totem::Audio::BeatEventKind lastBeatKind =
    Totem::Audio::BeatEventKind::Lost;
inline std::array<uint32_t, Totem::Audio::peakGroupCount> lastPeakWaveMs{};
inline std::array<uint32_t, Totem::Audio::peakGroupCount> lastIoPeakMs{};
inline uint32_t randomState = 0xC0FFEE23UL;

inline Angle<uint8_t> scaleToCommandAngle(Angle<uint16_t> angle,
                                          float turnsPerTurn) {
    return Angle<uint8_t>::fromTurns(angle.turns() * turnsPerTurn);
}

inline uint32_t nextRandom(uint32_t salt) {
    randomState ^=
        salt + 0x9E3779B9UL + (randomState << 6U) + (randomState >> 2U);
    randomState ^= randomState << 13U;
    randomState ^= randomState >> 17U;
    randomState ^= randomState << 5U;
    return randomState;
}

inline uint16_t randomRange(uint16_t min, uint16_t max, uint32_t salt) {
    if (max <= min) {
        return min;
    }
    const auto span = static_cast<uint32_t>(max - min + 1U);
    return static_cast<uint16_t>(min + (nextRandom(salt) % span));
}

inline const char *beatKindName(Totem::Audio::BeatEventKind kind) {
    switch (kind) {
    case Totem::Audio::BeatEventKind::ExpectedHit:
        return "expected-hit";
    case Totem::Audio::BeatEventKind::ExpectedMiss:
        return "expected-miss";
    case Totem::Audio::BeatEventKind::Reacquired:
        return "reacquired";
    case Totem::Audio::BeatEventKind::Lost:
        return "lost";
    default:
        return "unknown";
    }
}

inline Totem::LedPwm::Brightness
randomPeak(const BulbPulseProfile &profile,
           const Totem::Audio::PeakEvent &event, uint32_t salt) {
    const auto minRaw = profile.minPeak.value.value;
    const auto maxRaw = profile.maxPeak.value.value;
    if (maxRaw <= minRaw) {
        return profile.minPeak;
    }

    const auto jitter = static_cast<uint8_t>(nextRandom(salt) >> 24U);
    const auto mix = static_cast<uint8_t>(
        ((static_cast<uint16_t>(event.energy) * 3U) + jitter) / 4U);
    const auto raw = static_cast<uint16_t>(
        minRaw + ((static_cast<uint32_t>(maxRaw - minRaw) * mix) /
                  std::numeric_limits<uint8_t>::max()));
    return Totem::LedPwm::Brightness::fromRaw(raw);
}

inline Totem::LedPwm::Pulse makePulse(const BulbPulseProfile &profile,
                                      const Totem::Audio::PeakEvent &event,
                                      uint32_t nowMs, uint32_t salt) {
    return Totem::LedPwm::Pulse{
        .peak = randomPeak(profile, event, nowMs ^ salt),
        .riseMs = randomRange(profile.minRiseMs, profile.maxRiseMs,
                              nowMs ^ salt ^ 0x1357U),
        .holdMs = randomRange(profile.minHoldMs, profile.maxHoldMs,
                              nowMs ^ salt ^ 0x2468U),
        .fallMs = randomRange(profile.minFallMs, profile.maxFallMs,
                              nowMs ^ salt ^ 0x369CU),
        .curve = Totem::LedPwm::Curve::SmoothStep,
    };
}

inline ReturnCode publishIoCommand(Totem::LedPwm::CommandEvent event) {
    return Totem::LedPwm::publishCommandEvent(event);
}

inline ReturnCode publishLayerActive(Totem::LedDisplay::Layer layer,
                                     bool active) {
    FAIL_IF_UNEXPECTED_FWD(
        cmd, Totem::LedDisplay::makeLayerActiveCommand(layer, active),
        "Failed to build orchestrated layer active command");
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

inline ReturnCode publishLayerOpacity(Totem::LedDisplay::Layer layer,
                                      uint8_t opacity) {
    FAIL_IF_UNEXPECTED_FWD(
        cmd, Totem::LedDisplay::makeLayerOpacityCommand(layer, opacity),
        "Failed to build orchestrated layer opacity command");
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

inline ReturnCode publishLayerSwap(Totem::LedDisplay::Layer first,
                                   Totem::LedDisplay::Layer second,
                                   uint16_t durationMs) {
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::makeLayerFadeSwapCommand(first, second, durationMs),
        "Failed to build orchestrated layer swap command");
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

inline ReturnCode publishLayerStartupState() {
    auto ret = OK();
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::Background,
                                   config.layers.backgroundActive));
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::Fft,
                                   config.layers.fftActive));
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::FftAlt,
                                   config.layers.fftAltActive));
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::Effect,
                                   config.layers.effectActive));
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::TransientEffect,
                                   config.layers.transientEffectActive));
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::Wheel,
                                   config.layers.wheelActive));
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::Debug,
                                   config.layers.debugActive));
    if (!ret.ok()) {
        return ret;
    }
    _log_i("Published LED layer startup state");
    return OK();
}

inline ReturnCode publishIoStartupState(bool firstPublish) {
    auto ret = OK();
    ret.combine(publishIoCommand(Totem::LedPwm::CommandEvent::setBrightness(
        PeripheralLed::Bulb1, config.ioLed.bulbBase)));
    ret.combine(publishIoCommand(Totem::LedPwm::CommandEvent::setBrightness(
        PeripheralLed::Bulb2, config.ioLed.bulbBase)));
    ret.combine(publishIoCommand(
        Totem::LedPwm::CommandEvent::clearAnimations(PeripheralLed::Onboard)));
    ret.combine(publishIoCommand(Totem::LedPwm::CommandEvent::startGlitter(
        PeripheralLed::Onboard, config.ioLed.goldGlitter)));
    if (ret.ok()) {
        if (firstPublish) {
            _log_i("Published IO LED startup state");
        } else {
            _log_d("Refreshed IO LED startup state");
        }
    }
    return ret;
}

inline ReturnCode publishWheelEffects() {
    if (config.wheel.publishHueOffset) {
        FAIL_IF_UNEXPECTED_FWD(
            hueCmd,
            Totem::LedDisplay::makeHueOffsetCommand(scaleToCommandAngle(
                wheelOffset, config.wheel.hueTurnsPerWheelTurn)),
            "Failed to build orchestrated hue offset command");
        FAIL_IF_ERR_FWD(Totem::LedDisplay::publishAnimationCommand(hueCmd),
                        "Failed to publish orchestrated hue offset command");
    }

    if (config.wheel.publishRotationOffset) {
        FAIL_IF_UNEXPECTED_FWD(
            rotationCmd,
            Totem::LedDisplay::makeRotationOffsetCommand(scaleToCommandAngle(
                wheelOffset, config.wheel.rotationTurnsPerWheelTurn)),
            "Failed to build orchestrated rotation offset command");
        FAIL_IF_ERR_FWD(
            Totem::LedDisplay::publishAnimationCommand(rotationCmd),
            "Failed to publish orchestrated rotation offset command");
    }

    if (config.wheel.publishIndicatorUpdate) {
        FAIL_IF_UNEXPECTED_FWD(
            indicatorCmd,
            Totem::LedDisplay::Animations::WheelIndicatorCommand::
                makeUpdateCommand(config.wheel.indicator,
                                  Totem::LedDisplay::Animations::
                                      WheelIndicatorCommand::defaultRequestId),
            "Failed to build orchestrated wheel indicator update");
        FAIL_IF_ERR_FWD(
            Totem::LedDisplay::publishAnimationCommand(indicatorCmd),
            "Failed to publish orchestrated wheel indicator update");
    }

    return OK();
}

inline const FftVisualPreset &fftPreset(size_t index) {
    return config.fftVisuals.presets[index % config.fftVisuals.presets.size()];
}

inline uint16_t requestIdForFftLayer(Totem::LedDisplay::Layer layer) {
    return layer == Totem::LedDisplay::Layer::FftAlt
               ? config.fftVisuals.fftAltRequestId
               : config.fftVisuals.fftRequestId;
}

inline ReturnCode publishFftPreset(size_t presetIndex,
                                   Totem::LedDisplay::Layer layer,
                                   uint16_t requestId) {
    const auto &preset = fftPreset(presetIndex);
    Totem::LedDisplay::AnimationCommand cmd{};
    switch (preset.kind) {
    case FftVisualKind::SpectralWeave: {
        FAIL_IF_UNEXPECTED_FWD(
            built,
            Totem::LedDisplay::Animations::SpectralWeaveCommand::makeCommand(
                {}, requestId,
                Totem::LedDisplay::Animations::SpectralWeaveCommand::
                    defaultLifetimeMs,
                layer),
            "Failed to build orchestrated spectral weave animation");
        cmd = built;
        break;
    }
    case FftVisualKind::SpectralIris: {
        FAIL_IF_UNEXPECTED_FWD(
            built,
            Totem::LedDisplay::Animations::SpectralIrisCommand::makeCommand(
                {}, requestId,
                Totem::LedDisplay::Animations::SpectralIrisCommand::
                    defaultLifetimeMs,
                layer),
            "Failed to build orchestrated spectral iris animation");
        cmd = built;
        break;
    }
    case FftVisualKind::OrbitSparks: {
        FAIL_IF_UNEXPECTED_FWD(
            built,
            Totem::LedDisplay::Animations::OrbitSparksCommand::makeCommand(
                {}, requestId,
                Totem::LedDisplay::Animations::OrbitSparksCommand::
                    defaultLifetimeMs,
                layer),
            "Failed to build orchestrated orbit sparks animation");
        cmd = built;
        break;
    }
    case FftVisualKind::StainedCells: {
        FAIL_IF_UNEXPECTED_FWD(
            built,
            Totem::LedDisplay::Animations::StainedCellsCommand::makeCommand(
                {}, requestId,
                Totem::LedDisplay::Animations::StainedCellsCommand::
                    defaultLifetimeMs,
                layer),
            "Failed to build orchestrated stained cells animation");
        cmd = built;
        break;
    }
    default:
        FAIL(ERR(CoreError, InvalidArgument),
             "Unknown orchestrated FFT visual kind");
    }

    FAIL_IF_ERR_FWD(Totem::LedDisplay::publishAnimationCommand(cmd),
                    "Failed to publish orchestrated FFT visual animation");
    _log_d("Published FFT visual preset=%s layer=%u request=%u",
           preset.name, static_cast<unsigned>(layer), requestId);
    return OK();
}

inline ReturnCode publishInitialFftVisual(uint32_t nowMs) {
    activeFftLayer = Totem::LedDisplay::Layer::Fft;
    hiddenFftLayer = Totem::LedDisplay::Layer::FftAlt;
    activeFftPresetIndex = 0;
    pendingFftPresetIndex = 0;
    fftFadeInProgress = false;

    auto ret = OK();
    ret.combine(publishLayerActive(activeFftLayer, true));
    ret.combine(publishLayerOpacity(activeFftLayer, 255));
    ret.combine(publishLayerActive(hiddenFftLayer, false));
    ret.combine(publishLayerOpacity(hiddenFftLayer, 0));
    ret.combine(publishFftPreset(activeFftPresetIndex, activeFftLayer,
                                 requestIdForFftLayer(activeFftLayer)));
    if (!ret.ok()) {
        return ret;
    }

    lastFftVisualPublishMs = nowMs;
    lastFftSwitchMs = nowMs;
    fftVisualPublished = true;
    const auto &preset = fftPreset(activeFftPresetIndex);
    _log_i("Started FFT visual preset=%s layer=%u request=%u",
           preset.name, static_cast<unsigned>(activeFftLayer),
           requestIdForFftLayer(activeFftLayer));
    return OK();
}

inline void completeFftFadeIfDue(uint32_t nowMs) {
    if (!fftFadeInProgress) {
        return;
    }
    if (nowMs - fftFadeStartMs < fftFadeDurationMs) {
        return;
    }

    const auto oldActiveLayer = activeFftLayer;
    activeFftLayer = hiddenFftLayer;
    hiddenFftLayer = oldActiveLayer;
    activeFftPresetIndex = pendingFftPresetIndex;
    lastFftSwitchMs = nowMs;
    lastFftVisualPublishMs = nowMs;
    fftFadeInProgress = false;
    const auto &preset = fftPreset(activeFftPresetIndex);
    _log_i("Completed FFT visual fade preset=%s activeLayer=%u hiddenLayer=%u",
           preset.name, static_cast<unsigned>(activeFftLayer),
           static_cast<unsigned>(hiddenFftLayer));
}

inline ReturnCode stageNextFftVisual(uint32_t nowMs) {
    if (fftFadeInProgress) {
        return OK();
    }

    const auto nextPreset =
        (activeFftPresetIndex + 1U) % config.fftVisuals.presets.size();
    const auto &preset = fftPreset(nextPreset);
    const auto requestId = requestIdForFftLayer(hiddenFftLayer);

    FAIL_IF_ERR_FWD(publishLayerActive(hiddenFftLayer, true),
                    "Failed to activate hidden FFT layer");
    FAIL_IF_ERR_FWD(publishLayerOpacity(hiddenFftLayer, 0),
                    "Failed to zero hidden FFT layer opacity");
    FAIL_IF_ERR_FWD(publishFftPreset(nextPreset, hiddenFftLayer, requestId),
                    "Failed to publish staged FFT visual");
    FAIL_IF_ERR_FWD(publishLayerSwap(activeFftLayer, hiddenFftLayer,
                                     preset.fadeMs),
                    "Failed to publish FFT visual layer swap");

    pendingFftPresetIndex = nextPreset;
    fftFadeInProgress = true;
    fftFadeStartMs = nowMs;
    fftFadeDurationMs = preset.fadeMs;
    lastFftVisualPublishMs = nowMs;
    _log_i("Staged FFT visual preset=%s fromLayer=%u toLayer=%u request=%u "
           "fade=%ums",
           preset.name, static_cast<unsigned>(activeFftLayer),
           static_cast<unsigned>(hiddenFftLayer), requestId,
           static_cast<unsigned>(preset.fadeMs));
    return OK();
}

inline ReturnCode handleFftVisuals(uint32_t nowMs) {
    if (!config.fftVisuals.publishOnStartup || fftVisualSuppressed) {
        return OK();
    }

    completeFftFadeIfDue(nowMs);

    if (!fftVisualPublished) {
        return publishInitialFftVisual(nowMs);
    }
    if (fftFadeInProgress) {
        return OK();
    }

    const auto &activePreset = fftPreset(activeFftPresetIndex);
    if (activePreset.dwellMs != 0 &&
        nowMs - lastFftSwitchMs >= activePreset.dwellMs) {
        return stageNextFftVisual(nowMs);
    }

    const auto elapsed = nowMs - lastFftVisualPublishMs;
    if (config.fftVisuals.refreshIntervalMs != 0 &&
        lastFftVisualPublishMs != 0 &&
        elapsed >= config.fftVisuals.refreshIntervalMs) {
        FAIL_IF_ERR_FWD(publishFftPreset(activeFftPresetIndex, activeFftLayer,
                                         requestIdForFftLayer(activeFftLayer)),
                        "Failed to refresh active FFT visual");
        lastFftVisualPublishMs = nowMs;
        _log_d("Refreshed FFT visual preset=%s layer=%u request=%u",
               activePreset.name, static_cast<unsigned>(activeFftLayer),
               requestIdForFftLayer(activeFftLayer));
    }
    return OK();
}

inline ReturnCode
onWheelEnvelope(void * /*unused*/,
                const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(state,
                           envelope.getPayloadAs<Totem::Wheel::WheelState>(),
                           "Failed to decode orchestrated wheel state");

    if (wheelEventQueue == nullptr) {
        _log_w("Dropping wheel event before orchestration queue is ready");
        return OK();
    }

    auto ret = Totem::Queue::Platform::send(wheelEventQueue, &state, 0);
    if (!ret.ok()) {
        _log_w("Dropping wheel event: orchestration queue is full");
    }
    return OK();
}

inline ReturnCode
onBeatEnvelope(void * /*unused*/,
               const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(event,
                           envelope.getPayloadAs<Totem::Audio::BeatEvent>(),
                           "Failed to decode orchestrated beat event");

    if (beatEventQueue == nullptr) {
        _log_w("Dropping beat event before orchestration queue is ready");
        return OK();
    }

    auto ret = Totem::Queue::Platform::send(beatEventQueue, &event, 0);
    if (!ret.ok()) {
        _log_w("Dropping beat event: orchestration queue is full");
    }
    return OK();
}

inline ReturnCode
onPeakEnvelope(void * /*unused*/,
               const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(event,
                           envelope.getPayloadAs<Totem::Audio::PeakEvent>(),
                           "Failed to decode orchestrated peak event");

    if (peakEventQueue == nullptr) {
        _log_w("Dropping peak event before orchestration queue is ready");
        return OK();
    }

    auto ret = Totem::Queue::Platform::send(peakEventQueue, &event, 0);
    if (!ret.ok()) {
        _log_w("Dropping peak event: orchestration queue is full");
    }
    return OK();
}

inline ReturnCode
onButtonEnvelope(void * /*unused*/,
                 const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(event,
                           envelope.getPayloadAs<Totem::Buttons::ButtonEvent>(),
                           "Failed to decode orchestrated button event");

    if (buttonEventQueue == nullptr) {
        _log_w("Dropping button event before orchestration queue is ready");
        return OK();
    }

    auto ret = Totem::Queue::Platform::send(buttonEventQueue, &event, 0);
    if (!ret.ok()) {
        _log_w("Dropping button event: orchestration queue is full");
    }
    return OK();
}

inline ReturnCode
onAnimationEnvelope(void * /*unused*/,
                    const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(
        cmd, envelope.getPayloadAs<Totem::LedDisplay::AnimationCommand>(),
        "Failed to decode orchestrated animation command");

    const auto stopsFftVisual =
        cmd.type == Totem::LedDisplay::AnimationCommandType::Stop &&
        (cmd.requestId == 0 ||
         cmd.requestId == config.fftVisuals.fftRequestId ||
         cmd.requestId == config.fftVisuals.fftAltRequestId);
    if (stopsFftVisual && !fftVisualSuppressed) {
        fftVisualSuppressed = true;
        fftVisualPublished = false;
        fftFadeInProgress = false;
        lastFftVisualPublishMs = 0;
        lastFftSwitchMs = 0;
        _log_i("Suppressed FFT visual sequencing after animation stop request=%u",
               cmd.requestId);
    }

    return OK();
}

inline ReturnCode handleCalibrateAudioCommand(CommandDesc::ParsedArgs /*args*/,
                                              void * /*ctx*/) {
    _log_i("Publishing calibration button press from console command");
    return Totem::Buttons::publishPressed(PeripheralButton::Calibration);
}

inline CommandDesc calibrateAudioCmd = {
    .name = "calibrate-audio",
    .description = "Publish the audio calibration button event",
    .args = {},
    .handler = handleCalibrateAudioCommand,
    .subcommands = {},
};

} // namespace detail

inline ReturnCode begin() {
    if (detail::wheelEventQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::wheelEventQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create master orchestration wheel queue");
        }
        detail::wheelEventQueue = *queueResult;
    }
    if (detail::beatEventQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::beatEventQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create master orchestration beat queue");
        }
        detail::beatEventQueue = *queueResult;
    }
    if (detail::peakEventQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::peakEventQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create master orchestration peak queue");
        }
        detail::peakEventQueue = *queueResult;
    }
    if (detail::buttonEventQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::buttonEventQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create master orchestration button queue");
        }
        detail::buttonEventQueue = *queueResult;
    }

    FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                "PubSub backend is not configured for master orchestration");
    if (detail::wheelSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-wheel",
                {.subscriber = nullptr, .callback = detail::onWheelEnvelope},
                PubSubService::Topic::Wheel),
            "Failed to subscribe master orchestration to wheel events");
        detail::wheelSubscription = sub;
    }
    if (detail::beatSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-beat",
                {.subscriber = nullptr, .callback = detail::onBeatEnvelope},
                PubSubService::Topic::Beat),
            "Failed to subscribe master orchestration to beat events");
        detail::beatSubscription = sub;
    }
    if (detail::peakSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-peak",
                {.subscriber = nullptr, .callback = detail::onPeakEnvelope},
                PubSubService::Topic::Peak),
            "Failed to subscribe master orchestration to peak events");
        detail::peakSubscription = sub;
    }
    if (detail::buttonSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-btn",
                {.subscriber = nullptr, .callback = detail::onButtonEnvelope},
                PubSubService::Topic::Button),
            "Failed to subscribe master orchestration to button events");
        detail::buttonSubscription = sub;
    }
    if (detail::animationSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-anim",
                {.subscriber = nullptr,
                 .callback = detail::onAnimationEnvelope},
                PubSubService::Topic::Animation),
            "Failed to subscribe master orchestration to animation commands");
        detail::animationSubscription = sub;
    }
    if (!detail::calibrateAudioCommandRegistered) {
        FAIL_IF_UNEXPECTED_FWD(commandKey,
                               CommandRegistrarService::get().registerCommand(
                                   detail::calibrateAudioCmd),
                               "Failed to register /calibrate-audio command");
        (void)commandKey;
        detail::calibrateAudioCommandRegistered = true;
    }
    _log_i("Master orchestration subscribed to wheel, beat, peak, button, and "
           "animation events");
    return OK();
}

inline ReturnCode handleWheel(const Totem::Wheel::WheelState &state) {
    if (state.delta.value == 0) {
        return OK();
    }
    detail::wheelOffset += state.delta;
    detail::wheelOffsetDirty = true;
    detail::wheelIndicatorDirty = true;
    return OK();
}

inline ReturnCode handlePeakWave(const Totem::Audio::PeakEvent &event,
                                 uint32_t nowMs) {
    if (!config.peakWave.publishCenterWave) {
        return OK();
    }
    const auto groupIndex = Totem::Audio::peakGroupIndex(event.group);
    if (groupIndex >= Totem::Audio::peakGroupCount) {
        return OK();
    }

    const auto lastMs = detail::lastPeakWaveMs[groupIndex];
    const auto elapsed = nowMs - lastMs;
    if (lastMs != 0 && elapsed < config.peakWave.minIntervalMs) {
        return OK();
    }

    const auto saturation = static_cast<uint8_t>(
        config.peakWave.saturationByGroup[groupIndex].first +
        ((config.peakWave.saturationByGroup[groupIndex].second * event.energy) /
         std::numeric_limits<uint8_t>::max()));

    const auto value = static_cast<uint8_t>(
        config.peakWave.valueByGroup[groupIndex].first +
        ((config.peakWave.valueByGroup[groupIndex].second * event.energy) /
         std::numeric_limits<uint8_t>::max()));

    detail::lastPeakWaveMs[groupIndex] = nowMs;
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::Animations::CenterWaveCommand::makeCommand(
            {.hue = config.peakWave.hueByGroup[groupIndex],
             .saturation = saturation,
             .value = value,
             .rise = config.peakWave.rise,
             .peak = config.peakWave.peak,
             .wake = config.peakWave.wake},
            0, config.peakWave.lifetimeMs),
        "Failed to build peak center wave command");
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

inline ReturnCode handleDropWave(const Totem::Audio::PeakEvent &event,
                                 uint32_t nowMs) {
    if (!config.dropWave.publishCenterWave) {
        detail::lastAnyPeakMs = nowMs;
        return OK();
    }

    const auto groupIndex = Totem::Audio::peakGroupIndex(event.group);
    if (groupIndex >= Totem::Audio::peakGroupCount) {
        detail::lastAnyPeakMs = nowMs;
        return OK();
    }

    const auto lastPeakMs = detail::lastAnyPeakMs;
    detail::lastAnyPeakMs = nowMs;
    if (lastPeakMs == 0 ||
        (nowMs - lastPeakMs) < config.dropWave.quietWindowMs) {
        return OK();
    }

    if (detail::lastDropWaveMs != 0 &&
        (nowMs - detail::lastDropWaveMs) < config.dropWave.minIntervalMs) {
        return OK();
    }

    detail::lastDropWaveMs = nowMs;
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::Animations::CenterWaveCommand::makeCommand(
            {.hue = config.dropWave.hueByGroup[groupIndex],
             .saturation = config.dropWave.saturation,
             .value = config.dropWave.value,
             .rise = config.dropWave.rise,
             .peak = config.dropWave.peak,
             .wake = config.dropWave.wake},
            0, config.dropWave.lifetimeMs),
        "Failed to build drop center wave command");
    _log_i("Published drop center wave after %lums quiet",
           static_cast<unsigned long>(nowMs - lastPeakMs));
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

inline ReturnCode handleIoPeak(const Totem::Audio::PeakEvent &event,
                               uint32_t nowMs) {
    if (!config.ioLed.publishPeakFlicker) {
        return OK();
    }

    const auto groupIndex = Totem::Audio::peakGroupIndex(event.group);
    if (groupIndex >= Totem::Audio::peakGroupCount) {
        return OK();
    }

    const BulbPulseProfile *profile = nullptr;
    switch (event.group) {
    case Totem::Audio::PeakGroup::Bass:
        profile = &config.ioLed.bassPulse;
        break;
    case Totem::Audio::PeakGroup::Mid:
        profile = &config.ioLed.midPulse;
        break;
    case Totem::Audio::PeakGroup::High:
        profile = &config.ioLed.treblePulse;
        break;
    default:
        return OK();
    }

    const auto lastMs = detail::lastIoPeakMs[groupIndex];
    const auto elapsed = nowMs - lastMs;
    if (lastMs != 0 && elapsed < profile->minIntervalMs) {
        return OK();
    }
    detail::lastIoPeakMs[groupIndex] = nowMs;

    auto ret = OK();
    if (event.group == Totem::Audio::PeakGroup::Bass ||
        event.group == Totem::Audio::PeakGroup::High) {
        ret.combine(
            detail::publishIoCommand(Totem::LedPwm::CommandEvent::startPulse(
                PeripheralLed::Bulb1,
                detail::makePulse(*profile, event, nowMs, 0xB11BU))));
    }
    if (event.group == Totem::Audio::PeakGroup::Mid ||
        event.group == Totem::Audio::PeakGroup::High) {
        ret.combine(
            detail::publishIoCommand(Totem::LedPwm::CommandEvent::startPulse(
                PeripheralLed::Bulb2,
                detail::makePulse(*profile, event, nowMs, 0xB22BU))));
    }
    return ret;
}

inline ReturnCode handleBeat(const Totem::Audio::BeatEvent &event) {
    detail::lastBeatSequence = event.sequence;
    const bool transition = event.kind != detail::lastBeatKind;
    detail::lastBeatKind = event.kind;
    if (config.beat.logStateTransitions &&
        (transition || event.kind == Totem::Audio::BeatEventKind::Reacquired ||
         event.kind == Totem::Audio::BeatEventKind::Lost)) {
        _log_i("Beat %s: bpm=%u confidence=%u energy=%u sequence=%lu",
               detail::beatKindName(event.kind), event.bpm, event.confidence,
               event.energy, static_cast<unsigned long>(event.sequence));
    }
    return OK();
}

inline ReturnCode handlePeak(const Totem::Audio::PeakEvent &event,
                             uint32_t nowMs) {
    auto ret = OK();
    ret.combine(handleDropWave(event, nowMs));
    ret.combine(handlePeakWave(event, nowMs));
    ret.combine(handleIoPeak(event, nowMs));
    return ret;
}

inline ReturnCode handleButton(const Totem::Buttons::ButtonEvent &event,
                               uint32_t nowMs) {
    if (event.type != Totem::Buttons::ButtonEventType::Pressed) {
        return OK();
    }

    if (event.button == PeripheralButton::Calibration) {
        _log_i("Audio calibration button press observed");
        return OK();
    }

    if (!config.bell.publish || event.button != PeripheralButton::Bell) {
        return OK();
    }

    const auto elapsed = nowMs - detail::lastBellSinelonMs;
    if (detail::lastBellSinelonMs != 0 && elapsed < config.bell.minIntervalMs) {
        return OK();
    }

    detail::lastBellSinelonMs = nowMs;
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::Animations::SineWaveCommand::makeCommand(
            config.bell.config),
        "Failed to build bell bell command");
    return Totem::LedDisplay::publishAnimationCommand(cmd);
}

inline ReturnCode work(uint32_t nowMs, bool allowNormalOperation = true) {
    if (detail::beatEventQueue != nullptr) {
        Totem::Audio::BeatEvent beat{};
        while (Totem::Queue::Platform::receive(detail::beatEventQueue, &beat, 0)
                   .ok()) {
            if (allowNormalOperation) {
                FAIL_IF_ERR_FWD(handleBeat(beat),
                                "Failed to handle queued beat event");
            }
        }
    }

    if (detail::peakEventQueue != nullptr) {
        Totem::Audio::PeakEvent peak{};
        while (Totem::Queue::Platform::receive(detail::peakEventQueue, &peak, 0)
                   .ok()) {
            if (allowNormalOperation) {
                FAIL_IF_ERR_FWD(handlePeak(peak, nowMs),
                                "Failed to handle queued peak event");
            }
        }
    }

    if (detail::buttonEventQueue != nullptr) {
        Totem::Buttons::ButtonEvent button{};
        while (Totem::Queue::Platform::receive(detail::buttonEventQueue,
                                               &button, 0)
                   .ok()) {
            if (allowNormalOperation) {
                FAIL_IF_ERR_FWD(handleButton(button, nowMs),
                                "Failed to handle queued button event");
            }
        }
    }

    if (detail::wheelEventQueue != nullptr) {
        Totem::Wheel::WheelState state{};
        while (
            Totem::Queue::Platform::receive(detail::wheelEventQueue, &state, 0)
                .ok()) {
            FAIL_IF_ERR_FWD(handleWheel(state),
                            "Failed to handle queued wheel event");
        }
    }

    if (!allowNormalOperation) {
        return OK();
    }

    const auto layerStartupElapsed =
        nowMs - detail::lastLayerStartupStatePublishMs;
    const auto layerStartupRetryDue =
        !detail::layerStartupStatePublished &&
        (detail::lastLayerStartupStatePublishMs == 0 ||
         (config.layers.retryIntervalMs != 0 &&
          layerStartupElapsed >= config.layers.retryIntervalMs));
    if (config.layers.publishStartupState && layerStartupRetryDue) {
        const auto publishResult = detail::publishLayerStartupState();
        detail::lastLayerStartupStatePublishMs = nowMs;
        if (!publishResult.ok()) {
            _log_w("Failed to publish LED layer startup state; retrying later");
            return OK();
        }
        detail::layerStartupStatePublished = true;
    }

    const auto ioStartupElapsed = nowMs - detail::lastIoStartupPublishMs;
    const auto ioStartupRetryDue =
        !detail::ioStartupPublished &&
        (detail::lastIoStartupPublishMs == 0 ||
         (config.ioLed.stateRefreshMs != 0 &&
          ioStartupElapsed >= config.ioLed.stateRefreshMs));
    const auto ioStartupRefreshDue =
        detail::ioStartupPublished && config.ioLed.stateRefreshMs != 0 &&
        detail::lastIoStartupPublishMs != 0 &&
        ioStartupElapsed >= config.ioLed.stateRefreshMs;
    if (config.ioLed.publishStartupState &&
        (ioStartupRetryDue || ioStartupRefreshDue)) {
        const auto firstPublish = !detail::ioStartupPublished;
        const auto publishResult = detail::publishIoStartupState(firstPublish);
        detail::lastIoStartupPublishMs = nowMs;
        if (!publishResult.ok()) {
            _log_w("Failed to publish IO LED startup state; retrying later");
            return OK();
        }
        detail::ioStartupPublished = true;
    }

    const auto fftVisualResult = detail::handleFftVisuals(nowMs);
    if (!fftVisualResult.ok()) {
        _log_w("Failed to update FFT visual sequencing; retrying later");
        return OK();
    }

    if (!detail::wheelOffsetDirty && !detail::wheelIndicatorDirty) {
        return OK();
    }

    const auto elapsed = nowMs - detail::lastWheelPublishMs;
    if (detail::lastWheelPublishMs != 0 &&
        elapsed < config.wheel.publishMinIntervalMs) {
        return OK();
    }

    detail::lastWheelPublishMs = nowMs;
    FAIL_IF_ERR_FWD(detail::publishWheelEffects(),
                    "Failed to publish orchestrated wheel effects");
    detail::wheelOffsetDirty = false;
    detail::wheelIndicatorDirty = false;
    return OK();
}

} // namespace MasterOrchestration
