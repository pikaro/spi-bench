#pragma once

#include "AudioFft/Interfaces/Types.hpp"
#include "AudioFft/Interfaces/Wire.hpp"
#include "BatteryMonitor/Interfaces/Wire.hpp"
#include "Button/Interfaces/Types.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Data/ButtonEvent.hpp"
#include "Data/DialEvent.hpp"
#include "Data/MenuEvent.hpp"
#include "Data/Peripherals.hpp"
#include "LedDisplay/Animations/Bolt/Command.hpp"
#include "LedDisplay/Animations/Bolt/Config.hpp"
#include "LedDisplay/Animations/CenterWave/Command.hpp"
#include "LedDisplay/Animations/OrbitSparks/Command.hpp"
#include "LedDisplay/Animations/PolarLattice/Command.hpp"
#include "LedDisplay/Animations/PolarLattice/Config.hpp"
#include "LedDisplay/Animations/RadialCurtain/Command.hpp"
#include "LedDisplay/Animations/RadialCurtain/Config.hpp"
#include "LedDisplay/Animations/RadialGauge/Command.hpp"
#include "LedDisplay/Animations/RadialGauge/Config.hpp"
#include "LedDisplay/Animations/RadialMenu/Command.hpp"
#include "LedDisplay/Animations/RadialMenu/Config.hpp"
#include "LedDisplay/Animations/SineWave/Command.hpp"
#include "LedDisplay/Animations/SineWave/Config.hpp"
#include "LedDisplay/Animations/Sinelon/Command.hpp"
#include "LedDisplay/Animations/Sinelon/Config.hpp"
#include "LedDisplay/Animations/SpectralIris/Command.hpp"
#include "LedDisplay/Animations/SpectralWeave/Command.hpp"
#include "LedDisplay/Animations/StainedCells/Command.hpp"
#include "LedDisplay/Animations/Starburst/Command.hpp"
#include "LedDisplay/Animations/Starburst/Config.hpp"
#include "LedDisplay/Animations/WheelIndicator/Command.hpp"
#include "LedDisplay/Animations/WheelIndicator/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
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
#include "debug_mode.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace MasterOrchestration {

struct WheelMapping {
    bool publishHueOffset = true;
    bool publishRotationOffset = false;
    bool publishIndicatorUpdate = false;
    float hueTurnsPerWheelTurn = 1.0F;
    float rotationTurnsPerWheelTurn = 1.0F;
    uint32_t publishMinIntervalMs = 20;
    Totem::LedDisplay::Animations::WheelIndicatorConfig indicator{};
};

struct BrightnessMapping {
    bool publish = true;
    int32_t minimumPosition = 0;
    int32_t maximumPosition = 31;
    Totem::LedDisplay::Animations::RadialGaugeConfig indicator{
        .value = 16,
        .maximumValue = 31,
        .startHue = 0,
        .startSaturation = 0,
        .startValue = 255,
        .endHue = 0,
        .endSaturation = 0,
        .endValue = 255,
        .centerRing =
            Totem::LedDisplay::Animations::RadialGaugeSpec::outermostRing,
        .ringWidth = 1,
    };
    uint16_t indicatorLifetimeMs =
        Totem::LedDisplay::Animations::RadialGaugeCommand::defaultLifetimeMs;
    uint16_t indicatorRequestId =
        Totem::LedDisplay::Animations::RadialGaugeCommand::defaultRequestId;
};

struct MenuMapping {
    bool publish = true;
    int32_t minimumPosition = Totem::Data::mainMenuMinimumPosition;
    uint16_t requestId =
        Totem::LedDisplay::Animations::RadialMenuCommand::defaultRequestId;
    Totem::LedDisplay::Animations::RadialMenuConfig indicator{
        .itemHues = {{0, 0, 224, 32, 128, 192, 96, 0}},
        .populatedItems = 0x7D,
        .itemCount = static_cast<uint8_t>(Totem::Data::mainMenuPositionCount),
        .selectedItem =
            Totem::LedDisplay::Animations::RadialMenuSpec::noSelectedItem,
        .itemSaturation = 255,
        .itemValue = 255,
        .emptyItemValue = 32,
        .baseSpokeWidth = 4,
        .baseRingDepth = 2,
        .baseTipSpokeWidth = 2,
        .baseTipRingDepth = 2,
        .unfurledSpokeWidth = 4,
        .unfurledRingDepth = 6,
        .unfurledTipSpokeWidth = 2,
        .unfurledTipRingDepth = 2,
        .unfurlDurationMs = 160,
    };
};

struct DebugModeMapping {
    uint16_t wheelRequestId =
        Totem::LedDisplay::Animations::WheelIndicatorCommand::defaultRequestId;
    Totem::LedDisplay::Animations::WheelIndicatorConfig wheelIndicator{
        .hue = 0,
        .saturation = 255,
        .value = 255,
        .spokes = 1,
        .falloff = 0,
    };
};

struct BatteryGaugeMapping {
    bool publish = true;
    Totem::LedDisplay::Animations::RadialGaugeConfig indicator{
        .value = 0,
        .maximumValue = 1'000,
        .startHue = 0,
        .startSaturation = 255,
        .startValue = 255,
        .endHue = 96,
        .endSaturation = 255,
        .endValue = 255,
        .centerRing =
            Totem::LedDisplay::Animations::RadialGaugeSpec::outermostRing,
        .ringWidth = 1,
    };
    uint16_t lifetimeMs =
        Totem::LedDisplay::Animations::RadialGaugeCommand::defaultLifetimeMs;
    uint16_t requestId =
        Totem::LedDisplay::Animations::RadialGaugeCommand::defaultRequestId;
};

struct PeakWaveMapping {
    bool publishCenterWave = false;
    // std::array<uint8_t, Totem::AudioFft::peakGroupCount> hueByGroup{{0, 24,
    // 48}};
    std::array<uint8_t, Totem::AudioFft::peakGroupCount> hueByGroup{{0, 32, 64}};
    std::array<std::pair<uint8_t, uint8_t>, Totem::AudioFft::peakGroupCount>
        // saturationByGroup{{{240, 15}, {230, 25}, {220, 25}}};
        saturationByGroup{{{245, 10}, {240, 15}, {235, 20}}};
    std::array<std::pair<uint8_t, uint8_t>, Totem::AudioFft::peakGroupCount>
        valueByGroup{{{150, 100}, {0, 0}, {0, 0}}};
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
    std::array<FftVisualPreset, 3> presets{{
        {.kind = FftVisualKind::SpectralWeave,
         .name = "weave",
         .dwellMs = 200000,
         .fadeMs = 10000},
        {.kind = FftVisualKind::SpectralIris,
         .name = "iris",
         .dwellMs = 200000,
         .fadeMs = 10000},
        // {.kind = FftVisualKind::OrbitSparks,
        //  .name = "sparks",
        //  .dwellMs = 45000,
        //  .fadeMs = 10000},
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
    std::array<uint8_t, Totem::AudioFft::peakGroupCount> hueByGroup{{0, 32, 64}};
    uint8_t saturation = 255;
    uint8_t value = 180;
    uint8_t rise = 2;
    uint8_t peak = 1;
    uint8_t wake = 4;
};

struct TotalEnergyWaveMapping {
    bool publishCenterWave = true;
    uint8_t triggerEnergy = 200;
    uint32_t minIntervalMs = 2500;
    uint16_t lifetimeMs = 1400;
    uint8_t hue = 0;
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
    uint16_t boltDurationMs = 900;
    bool randomizeBoltSeed = true;
    Totem::LedDisplay::Animations::BoltConfig bolt{
        .hue = 24,
        .saturation = 255,
        .value = 255,
        .width = 1,
        .jitter = 1,
        .forks = 1,
        .seed = 0,
        .outerOrigin = true,
    };
    uint16_t polarLatticeDurationMs = 2400;
    Totem::LedDisplay::Animations::PolarLatticeConfig polarLattice{
        .hue = 64,
        .saturation = 255,
        .value = 170,
        .radialMode = 4,
        .angularMode = 3,
        .speed = 96,
        .mix = 128,
        .contrast = 160,
    };
    uint16_t radialCurtainDurationMs = 2600;
    Totem::LedDisplay::Animations::RadialCurtainConfig radialCurtain{
        .hue = 200,
        .saturation = 220,
        .value = 190,
        .width = 8,
        .tilt = 64,
        .speed = 128,
        .outerOrigin = true,
        .spokePhase = 16,
    };
    // Zero uses SineWaveCommand's projected lifetime from the visible trail.
    uint16_t sineWaveLifetimeMs = 0;
    Totem::LedDisplay::Animations::SineWaveConfig sineWave{
        .hue = 128,
        .saturation = 255,
        .value = 255,
        .baseValue = 50,
        .width = 8,
        .durationMs = 1500,
        .wavelength = 8,
        .outerOrigin = true,
        .travelRings = 0,
        .spokeGainPct = 100,
        .tailDecay = 8,
        .peakHold = 160,
    };
    uint16_t sinelonDurationMs = 2400;
    Totem::LedDisplay::Animations::SinelonConfig sinelon{
        .hue = 96,
        .saturation = 255,
        .value = 220,
        .width = 3,
        .periodMs = 1000,
        .outerOrigin = false,
        .travelRings = 0,
        .bounceAttenuation = 255,
        .spokeGainPct = 100,
        .spokeGainPhaseStep = 64,
    };
    uint16_t starburstDurationMs = 1200;
    Totem::LedDisplay::Animations::StarburstConfig starburst{
        .hue = 32,
        .saturation = 255,
        .value = 220,
        .rise = 1,
        .peak = 2,
        .wake = 16,
        .points = 2,
        .pointGain = 2,
        .twist = 16,
        .cycles = 1,
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
    bool uiActive = true;
};

struct Config {
    WheelMapping wheel{};
    BrightnessMapping brightness{};
    MenuMapping menu{};
    DebugModeMapping debugMode{};
    BatteryGaugeMapping batteryGauge{};
    FftVisualMapping fftVisuals{};
    DropWaveMapping dropWave{};
    TotalEnergyWaveMapping totalEnergyWave{};
    PeakWaveMapping peakWave{};
    BeatMapping beat{};
    LayerMapping layers{};
    IoLedMapping ioLed{};
    BellMapping bell{};
};

inline constexpr Config config{};
static_assert(Totem::Data::mainMenuPositionCount <=
              Totem::LedDisplay::Animations::RadialMenuSpec::maximumItems);
static_assert(config.menu.indicator.itemCount ==
              Totem::Data::mainMenuPositionCount);
static_assert(config.menu.requestId != config.brightness.indicatorRequestId,
              "Menu and gauge animations need distinct request IDs");
static_assert(config.brightness.indicatorRequestId ==
                  config.batteryGauge.requestId,
              "UI gauges must share one request ID so a new gauge replaces "
              "the previous one");
inline constexpr size_t wheelEventQueueSize = 8;
inline constexpr size_t beatEventQueueSize = 8;
inline constexpr size_t fftFrameQueueSize = 2;
inline constexpr size_t peakEventQueueSize = 8;
inline constexpr size_t buttonEventQueueSize = 4;
inline constexpr size_t dialEventQueueSize = 4;
inline constexpr size_t menuEventQueueSize = 8;
inline constexpr size_t batteryStatusQueueSize = 2;

namespace detail {

inline Totem::Queue::Platform::Storage<Totem::Wheel::WheelState,
                                       wheelEventQueueSize>
    wheelEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::AudioFft::BeatEvent,
                                       beatEventQueueSize>
    beatEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::AudioFft::FftFrame,
                                       fftFrameQueueSize>
    fftFrameQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::AudioFft::PeakEvent,
                                       peakEventQueueSize>
    peakEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::Data::ButtonEvent,
                                       buttonEventQueueSize>
    buttonEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::Data::DialEvent,
                                       dialEventQueueSize>
    dialEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::Data::MenuEvent,
                                       menuEventQueueSize>
    menuEventQueueStorage{};
inline Totem::Queue::Platform::Storage<
    Totem::BatteryMonitor::BatteryStatusEvent, batteryStatusQueueSize>
    batteryStatusQueueStorage{};
inline Totem::Queue::Handle wheelEventQueue = nullptr;
inline Totem::Queue::Handle beatEventQueue = nullptr;
inline Totem::Queue::Handle fftFrameQueue = nullptr;
inline Totem::Queue::Handle peakEventQueue = nullptr;
inline Totem::Queue::Handle buttonEventQueue = nullptr;
inline Totem::Queue::Handle dialEventQueue = nullptr;
inline Totem::Queue::Handle menuEventQueue = nullptr;
inline Totem::Queue::Handle batteryStatusQueue = nullptr;
inline Totem::PubSubBackend::SubscriberKey wheelSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey beatSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey fftSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey peakSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey buttonSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey dialSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey menuSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey batteryStatusSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey animationSubscription = 0;
inline bool calibrateAudioCommandRegistered = false;
inline bool debugModeCommandRegistered = false;
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
inline uint32_t lastBellAnimationMs = 0;
inline uint32_t lastAnyPeakMs = 0;
inline uint32_t lastDropWaveMs = 0;
inline uint32_t lastTotalEnergyWaveMs = 0;
inline uint32_t lastBeatSequence = 0;
inline Totem::AudioFft::BeatEventKind lastBeatKind =
    Totem::AudioFft::BeatEventKind::Lost;
inline std::array<uint32_t, Totem::AudioFft::peakGroupCount> lastPeakWaveMs{};
inline std::array<uint32_t, Totem::AudioFft::peakGroupCount> lastIoPeakMs{};
inline uint32_t randomState = 0xC0FFEE23UL;
inline Totem::BatteryMonitor::BatteryStatusEvent latestBatteryStatus{};
inline bool hasBatteryStatus = false;

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

inline uint8_t scaledTotalFrameEnergy(const Totem::AudioFft::FftFrame &frame) {
    constexpr uint32_t bandCount = 8;
    const uint32_t total = static_cast<uint32_t>(frame.subBass) +
                           static_cast<uint32_t>(frame.bass) +
                           static_cast<uint32_t>(frame.lowMid) +
                           static_cast<uint32_t>(frame.mid) +
                           static_cast<uint32_t>(frame.highMid) +
                           static_cast<uint32_t>(frame.presence) +
                           static_cast<uint32_t>(frame.brilliance) +
                           static_cast<uint32_t>(frame.air);
    return static_cast<uint8_t>((total + (bandCount / 2U)) / bandCount);
}

inline void enqueueLatestFftFrame(const Totem::AudioFft::FftFrame &frame) {
    auto ret = Totem::Queue::Platform::send(fftFrameQueue, &frame, 0);
    if (ret.ok()) {
        return;
    }

    Totem::AudioFft::FftFrame dropped{};
    (void)Totem::Queue::Platform::receive(fftFrameQueue, &dropped, 0);
    ret = Totem::Queue::Platform::send(fftFrameQueue, &frame, 0);
    if (!ret.ok()) {
        _log_w("Dropping FFT frame: orchestration queue is full");
    }
}

inline ReturnCode publishBellAnimation(uint32_t nowMs) {
    constexpr uint16_t bellAnimationChoiceCount = 6;
    constexpr uint16_t lastBellAnimationChoice = bellAnimationChoiceCount - 1;
    const auto choice =
        randomRange(0, lastBellAnimationChoice, nowMs ^ 0xBE110001UL);

    switch (choice) {
    case 0: {
        auto animationConfig = config.bell.bolt;
        if (config.bell.randomizeBoltSeed) {
            animationConfig.seed =
                static_cast<uint8_t>(nextRandom(nowMs ^ 0xB0115EEDUL));
        }
        FAIL_IF_UNEXPECTED_FWD(
            cmd,
            Totem::LedDisplay::Animations::BoltCommand::makeCommand(
                animationConfig, 0, config.bell.boltDurationMs),
            "Failed to build bell bolt command");
        animationConfig.seed += 1;
        FAIL_IF_UNEXPECTED_FWD(
            cmd2,
            Totem::LedDisplay::Animations::BoltCommand::makeCommand(
                animationConfig, 0, config.bell.boltDurationMs),
            "Failed to build bell bolt command");
        animationConfig.seed += 1;
        FAIL_IF_UNEXPECTED_FWD(
            cmd3,
            Totem::LedDisplay::Animations::BoltCommand::makeCommand(
                animationConfig, 0, config.bell.boltDurationMs),
            "Failed to build bell bolt command");
        auto ret = OK();
        ret.combine(Totem::LedDisplay::publishAnimationPlayCommand(cmd));
        ret.combine(Totem::LedDisplay::publishAnimationPlayCommand(cmd2));
        ret.combine(Totem::LedDisplay::publishAnimationPlayCommand(cmd3));
        return ret;
    }
    case 1: {
        FAIL_IF_UNEXPECTED_FWD(
            cmd,
            Totem::LedDisplay::Animations::PolarLatticeCommand::makeCommand(
                config.bell.polarLattice, 0,
                config.bell.polarLatticeDurationMs),
            "Failed to build bell polar lattice command");
        return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
    }
    case 2: {
        FAIL_IF_UNEXPECTED_FWD(
            cmd,
            Totem::LedDisplay::Animations::RadialCurtainCommand::makeCommand(
                config.bell.radialCurtain, 0,
                config.bell.radialCurtainDurationMs),
            "Failed to build bell radial curtain command");
        return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
    }
    case 3: {
        FAIL_IF_UNEXPECTED_FWD(
            cmd,
            Totem::LedDisplay::Animations::SineWaveCommand::makeCommand(
                config.bell.sineWave, 0, config.bell.sineWaveLifetimeMs),
            "Failed to build bell sine wave command");
        return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
    }
    case 4: {
        FAIL_IF_UNEXPECTED_FWD(
            cmd,
            Totem::LedDisplay::Animations::SinelonCommand::makeCommand(
                config.bell.sinelon, 0, config.bell.sinelonDurationMs),
            "Failed to build bell sinelon command");
        return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
    }
    default: {
        FAIL_IF_UNEXPECTED_FWD(
            cmd,
            Totem::LedDisplay::Animations::StarburstCommand::makeCommand(
                config.bell.starburst, 0, config.bell.starburstDurationMs),
            "Failed to build bell starburst command");
        return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
    }
    }
}

inline const char *beatKindName(Totem::AudioFft::BeatEventKind kind) {
    switch (kind) {
    case Totem::AudioFft::BeatEventKind::ExpectedHit:
        return "expected-hit";
    case Totem::AudioFft::BeatEventKind::ExpectedMiss:
        return "expected-miss";
    case Totem::AudioFft::BeatEventKind::Reacquired:
        return "reacquired";
    case Totem::AudioFft::BeatEventKind::Lost:
        return "lost";
    default:
        return "unknown";
    }
}

inline Totem::LedPwm::Brightness
randomPeak(const BulbPulseProfile &profile,
           const Totem::AudioFft::PeakEvent &event, uint32_t salt) {
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
                                      const Totem::AudioFft::PeakEvent &event,
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

// Add future debug-mode startup effects at this lifecycle seam.
inline ReturnCode onDebugModeStart() {
    FAIL_IF_UNEXPECTED_FWD(
        wheelCmd,
        Totem::LedDisplay::Animations::WheelIndicatorCommand::makeCommand(
            config.debugMode.wheelIndicator, config.debugMode.wheelRequestId),
        "Failed to build debug-mode wheel animation");

    auto ret = publishLayerActive(Totem::LedDisplay::Layer::Wheel, true);
    ret.combine(Totem::LedDisplay::publishAnimationPlayCommand(wheelCmd));
    return ret;
}

// Add future debug-mode shutdown effects at this lifecycle seam.
inline ReturnCode onDebugModeStop() {
    FAIL_IF_UNEXPECTED_FWD(stopCmd,
                           Totem::LedDisplay::makeStopAnimationCommand(
                               config.debugMode.wheelRequestId),
                           "Failed to build debug-mode wheel stop command");

    auto ret = Totem::LedDisplay::publishAnimationCommand(stopCmd);
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::Wheel, false));
    return ret;
}

inline MasterDebugMode::Mode<onDebugModeStart, onDebugModeStop> debugMode{};

[[nodiscard]] inline bool debugModeActive() {
    return debugMode.active();
}

inline ReturnCode startDebugMode() {
    if (debugModeActive()) {
        return OK();
    }
    FAIL_IF_ERR_FWD(debugMode.start(), "Failed to start master debug mode");
    _log_i("Master debug mode started");
    return OK();
}

inline ReturnCode stopDebugMode() {
    if (!debugModeActive()) {
        return OK();
    }
    FAIL_IF_ERR_FWD(debugMode.stop(), "Failed to stop master debug mode");
    _log_i("Master debug mode stopped");
    return OK();
}

inline ReturnCode toggleDebugMode() {
    return debugModeActive() ? stopDebugMode() : startDebugMode();
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
    ret.combine(
        publishLayerActive(Totem::LedDisplay::Layer::Wheel,
                           config.layers.wheelActive || debugModeActive()));
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::Debug,
                                   config.layers.debugActive));
    ret.combine(publishLayerActive(Totem::LedDisplay::Layer::UI,
                                   config.layers.uiActive));
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
            Totem::LedDisplay::publishAnimationUpdateCommand(indicatorCmd),
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
    Totem::LedDisplay::AnimationPlayCommand cmd{};
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

    FAIL_IF_ERR_FWD(Totem::LedDisplay::publishAnimationPlayCommand(cmd),
                    "Failed to publish orchestrated FFT visual animation");
    _log_d("Published FFT visual preset=%s layer=%u request=%u", preset.name,
           static_cast<unsigned>(layer), requestId);
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
    _log_i("Started FFT visual preset=%s layer=%u request=%u", preset.name,
           static_cast<unsigned>(activeFftLayer),
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
    FAIL_IF_ERR_FWD(
        publishLayerSwap(activeFftLayer, hiddenFftLayer, preset.fadeMs),
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
                           envelope.getPayloadAs<Totem::AudioFft::BeatEvent>(),
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
onFftEnvelope(void * /*unused*/,
              const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(frame,
                           envelope.getPayloadAs<Totem::AudioFft::FftFrame>(),
                           "Failed to decode orchestrated FFT frame");

    if (fftFrameQueue == nullptr) {
        _log_w("Dropping FFT frame before orchestration queue is ready");
        return OK();
    }

    enqueueLatestFftFrame(frame);
    return OK();
}

inline ReturnCode
onPeakEnvelope(void * /*unused*/,
               const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(event,
                           envelope.getPayloadAs<Totem::AudioFft::PeakEvent>(),
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
                           envelope.getPayloadAs<Totem::Data::ButtonEvent>(),
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
onDialEnvelope(void * /*unused*/,
               const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(event,
                           envelope.getPayloadAs<Totem::Data::DialEvent>(),
                           "Failed to decode orchestrated dial event");

    if (dialEventQueue == nullptr) {
        _log_w("Dropping dial event before orchestration queue is ready");
        return OK();
    }

    auto ret = Totem::Queue::Platform::send(dialEventQueue, &event, 0);
    if (!ret.ok()) {
        _log_w("Dropping dial event: orchestration queue is full");
    }
    return OK();
}

inline ReturnCode
onMenuEnvelope(void * /*unused*/,
               const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(event,
                           envelope.getPayloadAs<Totem::Data::MenuEvent>(),
                           "Failed to decode orchestrated menu event");

    if (menuEventQueue == nullptr) {
        _log_w("Dropping menu event before orchestration queue is ready");
        return OK();
    }

    auto ret = Totem::Queue::Platform::send(menuEventQueue, &event, 0);
    if (!ret.ok()) {
        _log_w("Dropping menu event: orchestration queue is full");
    }
    return OK();
}

inline ReturnCode
onBatteryStatusEnvelope(void * /*unused*/,
                        const Totem::PubSubBackend::Envelope &envelope) {
    if (envelope.header.source !=
        static_cast<uint16_t>(NodeData::PubSub::NodeId::Power)) {
        _log_w("Ignoring battery status from non-power node=%u",
               static_cast<unsigned>(envelope.header.source));
        return OK();
    }
    FAIL_IF_UNEXPECTED_FWD(
        event,
        envelope.getPayloadAs<Totem::BatteryMonitor::BatteryStatusEvent>(),
        "Failed to decode orchestrated battery status");

    if (batteryStatusQueue == nullptr) {
        _log_w("Dropping battery status before orchestration queue is ready");
        return OK();
    }

    auto ret = Totem::Queue::Platform::send(batteryStatusQueue, &event, 0);
    if (!ret.ok()) {
        Totem::BatteryMonitor::BatteryStatusEvent discarded{};
        (void)Totem::Queue::Platform::receive(batteryStatusQueue, &discarded,
                                              0);
        ret = Totem::Queue::Platform::send(batteryStatusQueue, &event, 0);
    }
    if (!ret.ok()) {
        _log_w("Dropping battery status: orchestration queue is full");
    }
    return OK();
}

inline ReturnCode
onAnimationStopEnvelope(void * /*unused*/,
                        const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(
        stop, envelope.getPayloadAs<Totem::LedDisplay::AnimationStopCommand>(),
        "Failed to decode orchestrated animation stop command");

    const auto stopsFftVisual =
        stop.requestId == 0 ||
        stop.requestId == config.fftVisuals.fftRequestId ||
        stop.requestId == config.fftVisuals.fftAltRequestId;
    if (stopsFftVisual && !fftVisualSuppressed) {
        fftVisualSuppressed = true;
        fftVisualPublished = false;
        fftFadeInProgress = false;
        lastFftVisualPublishMs = 0;
        lastFftSwitchMs = 0;
        _log_i(
            "Suppressed FFT visual sequencing after animation stop request=%u",
            stop.requestId);
    }

    return OK();
}

inline ReturnCode handleCalibrateAudioCommand(CommandDesc::ParsedArgs /*args*/,
                                              void * /*ctx*/) {
    _log_i("Publishing calibration button press from console command");
    return PubSubService::publish(PubSubService::Topic::Button,
                                  Totem::Data::ButtonEvent{
                                      .event = Totem::Button::Event::Pressed,
                                      .button = PeripheralButton::Calibration,
                                  });
}

inline CommandDesc calibrateAudioCmd = {
    .name = "calibrate-audio",
    .description = "Publish the audio calibration button event",
    .args = {},
    .handler = handleCalibrateAudioCommand,
    .subcommands = {},
};

inline ReturnCode handleDebugModeCommand(CommandDesc::ParsedArgs /*args*/,
                                         void * /*ctx*/) {
    return toggleDebugMode();
}

inline CommandDesc debugModeCmd = {
    .name = "debug",
    .description = "Toggle master debug mode",
    .args = {},
    .handler = handleDebugModeCommand,
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
    if (detail::fftFrameQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::fftFrameQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create master orchestration FFT queue");
        }
        detail::fftFrameQueue = *queueResult;
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
    if (detail::dialEventQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::dialEventQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create master orchestration dial queue");
        }
        detail::dialEventQueue = *queueResult;
    }
    if (detail::menuEventQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::menuEventQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(queueResult.error(),
                         "Failed to create master orchestration menu queue");
        }
        detail::menuEventQueue = *queueResult;
    }
    if (detail::batteryStatusQueue == nullptr) {
        auto queueResult =
            Totem::Queue::Platform::create(detail::batteryStatusQueueStorage);
        if (!queueResult) {
            FAIL_ERR_FWD(
                queueResult.error(),
                "Failed to create master orchestration battery status queue");
        }
        detail::batteryStatusQueue = *queueResult;
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
    if (detail::fftSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-fft",
                {.subscriber = nullptr, .callback = detail::onFftEnvelope},
                PubSubService::Topic::FftFrame),
            "Failed to subscribe master orchestration to FFT frames");
        detail::fftSubscription = sub;
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
    if (detail::dialSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-dial",
                {.subscriber = nullptr, .callback = detail::onDialEnvelope},
                PubSubService::Topic::Dial),
            "Failed to subscribe master orchestration to dial events");
        detail::dialSubscription = sub;
    }
    if (detail::menuSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-menu",
                {.subscriber = nullptr, .callback = detail::onMenuEnvelope},
                PubSubService::Topic::Menu),
            "Failed to subscribe master orchestration to menu events");
        detail::menuSubscription = sub;
    }
    if (detail::batteryStatusSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-battery",
                {.subscriber = nullptr,
                 .callback = detail::onBatteryStatusEnvelope},
                PubSubService::Topic::Power),
            "Failed to subscribe master orchestration to battery status");
        detail::batteryStatusSubscription = sub;
    }
    if (detail::animationSubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            sub,
            PubSubService::get().subscribe(
                "master-orch-stop",
                {.subscriber = nullptr,
                 .callback = detail::onAnimationStopEnvelope},
                PubSubService::Topic::AnimationStop),
            "Failed to subscribe master orchestration to animation stop "
            "commands");
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
    if (!detail::debugModeCommandRegistered) {
        FAIL_IF_UNEXPECTED_FWD(commandKey,
                               CommandRegistrarService::get().registerCommand(
                                   detail::debugModeCmd),
                               "Failed to register /debug command");
        (void)commandKey;
        detail::debugModeCommandRegistered = true;
    }
    _log_i("Master orchestration subscribed to wheel, beat, FFT, peak, button, "
           "dial, menu, battery, and animation events");
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

inline ReturnCode handleDial(const Totem::Data::DialEvent &event) {
    if (!config.brightness.publish || event.dial != PeripheralDial::Main) {
        return OK();
    }
    if (event.position < config.brightness.minimumPosition ||
        event.position > config.brightness.maximumPosition) {
        _log_w("Ignoring brightness dial position outside configured range: "
               "%ld",
               static_cast<long>(event.position));
        return OK();
    }

    FAIL_IF_UNEXPECTED_FWD(
        brightnessCmd, Totem::LedDisplay::makeBrightnessCommand(event.value),
        "Failed to build orchestrated display brightness command");
    auto gaugeConfig = config.brightness.indicator;
    gaugeConfig.value = static_cast<uint16_t>(
        event.position - config.brightness.minimumPosition);
    gaugeConfig.maximumValue = static_cast<uint16_t>(
        config.brightness.maximumPosition - config.brightness.minimumPosition);
    FAIL_IF_UNEXPECTED_FWD(
        indicatorCmd,
        Totem::LedDisplay::Animations::RadialGaugeCommand::makeCommand(
            gaugeConfig, config.brightness.indicatorRequestId,
            config.brightness.indicatorLifetimeMs),
        "Failed to build orchestrated brightness gauge command");

    auto ret = Totem::LedDisplay::publishAnimationCommand(brightnessCmd);
    ret.combine(Totem::LedDisplay::publishAnimationPlayCommand(indicatorCmd));
    if (ret.ok()) {
        _log_i("Published display brightness=%u level=%ld",
               static_cast<unsigned>(event.value),
               static_cast<long>(event.position));
    }
    return ret;
}

inline ReturnCode handleMenu(const Totem::Data::MenuEvent &event,
                             uint32_t nowMs) {
    if (event.menu != PeripheralMenu::Main) {
        return OK();
    }

    auto ret = OK();
    if (config.menu.publish) {
        if (event.event == Totem::Data::MenuEventType::Selected) {
            FAIL_IF_UNEXPECTED_FWD(
                stopCmd,
                Totem::LedDisplay::makeStopAnimationCommand(
                    config.menu.requestId),
                "Failed to build radial menu stop command");
            ret.combine(Totem::LedDisplay::publishAnimationCommand(stopCmd));
        } else {
            const auto itemIndex = static_cast<int64_t>(event.position) -
                                   config.menu.minimumPosition;
            if (itemIndex < 0 ||
                itemIndex >= config.menu.indicator.itemCount) {
                _log_w("Ignoring radial menu position outside configured "
                       "range: %ld",
                       static_cast<long>(event.position));
            } else {
                auto menuConfig = config.menu.indicator;
                menuConfig.selectedItem = static_cast<uint8_t>(itemIndex);
                FAIL_IF_UNEXPECTED_FWD(
                    menuCmd,
                    Totem::LedDisplay::Animations::RadialMenuCommand::
                        makeCommand(menuConfig, config.menu.requestId),
                    "Failed to build orchestrated radial menu command");
                ret.combine(
                    Totem::LedDisplay::publishAnimationPlayCommand(menuCmd));
            }
        }
    }

    if (event.event == Totem::Data::MenuEventType::Selected) {
        switch (event.item) {
        case Totem::Data::MenuItem::Next:
            ret.combine(detail::stageNextFftVisual(nowMs));
            break;
        case Totem::Data::MenuItem::Debug:
            ret.combine(detail::toggleDebugMode());
            break;
        default:
            break;
        }
    }

    if (!config.batteryGauge.publish ||
        event.event != Totem::Data::MenuEventType::Selected ||
        event.item != Totem::Data::MenuItem::Battery) {
        return ret;
    }
    if (!detail::hasBatteryStatus ||
        !Totem::BatteryMonitor::hasUsableStateOfCharge(
            detail::latestBatteryStatus)) {
        _log_w("Battery menu selected without a fresh usable estimate");
        return ret;
    }

    auto gaugeConfig = config.batteryGauge.indicator;
    gaugeConfig.value =
        detail::latestBatteryStatus.stateOfChargePartsPerThousand;
    FAIL_IF_UNEXPECTED_FWD(
        gaugeCmd,
        Totem::LedDisplay::Animations::RadialGaugeCommand::makeCommand(
            gaugeConfig, config.batteryGauge.requestId,
            config.batteryGauge.lifetimeMs),
        "Failed to build orchestrated battery gauge command");
    ret.combine(Totem::LedDisplay::publishAnimationPlayCommand(gaugeCmd));

    const auto charge =
        detail::latestBatteryStatus.stateOfChargePartsPerThousand;
    _log_i("Published battery gauge=%u.%u%%",
           static_cast<unsigned>(charge / 10U),
           static_cast<unsigned>(charge % 10U));
    return ret;
}

inline ReturnCode handleTotalEnergyWave(const Totem::AudioFft::FftFrame &frame,
                                        uint32_t nowMs) {
    if (!config.totalEnergyWave.publishCenterWave) {
        return OK();
    }

    const auto energy = detail::scaledTotalFrameEnergy(frame);
    if (energy < config.totalEnergyWave.triggerEnergy) {
        return OK();
    }

    const auto lastMs = detail::lastTotalEnergyWaveMs;
    const auto elapsed = nowMs - lastMs;
    if (lastMs != 0 && elapsed < config.totalEnergyWave.minIntervalMs) {
        return OK();
    }

    detail::lastTotalEnergyWaveMs = nowMs;
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::Animations::CenterWaveCommand::makeCommand(
            {.hue = config.totalEnergyWave.hue,
             .saturation = config.totalEnergyWave.saturation,
             .value = config.totalEnergyWave.value,
             .rise = config.totalEnergyWave.rise,
             .peak = config.totalEnergyWave.peak,
             .wake = config.totalEnergyWave.wake},
            0, config.totalEnergyWave.lifetimeMs),
        "Failed to build total-energy center wave command");
    _log_i("Published total-energy center wave energy=%u",
           static_cast<unsigned>(energy));
    return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
}

inline ReturnCode handlePeakWave(const Totem::AudioFft::PeakEvent &event,
                                 uint32_t nowMs) {
    if (!config.peakWave.publishCenterWave) {
        return OK();
    }
    const auto groupIndex = Totem::AudioFft::peakGroupIndex(event.group);
    if (groupIndex >= Totem::AudioFft::peakGroupCount) {
        return OK();
    }

    if (config.peakWave.valueByGroup[groupIndex].second == 0 ||
        config.peakWave.saturationByGroup[groupIndex].second == 0) {
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
    return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
}

inline ReturnCode handleDropWave(const Totem::AudioFft::PeakEvent &event,
                                 uint32_t nowMs) {
    if (!config.dropWave.publishCenterWave) {
        detail::lastAnyPeakMs = nowMs;
        return OK();
    }

    const auto groupIndex = Totem::AudioFft::peakGroupIndex(event.group);
    if (groupIndex >= Totem::AudioFft::peakGroupCount) {
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
    return Totem::LedDisplay::publishAnimationPlayCommand(cmd);
}

inline ReturnCode handleIoPeak(const Totem::AudioFft::PeakEvent &event,
                               uint32_t nowMs) {
    if (!config.ioLed.publishPeakFlicker) {
        return OK();
    }

    const auto groupIndex = Totem::AudioFft::peakGroupIndex(event.group);
    if (groupIndex >= Totem::AudioFft::peakGroupCount) {
        return OK();
    }

    const BulbPulseProfile *profile = nullptr;
    switch (event.group) {
    case Totem::AudioFft::PeakGroup::Bass:
        profile = &config.ioLed.bassPulse;
        break;
    case Totem::AudioFft::PeakGroup::Mid:
        profile = &config.ioLed.midPulse;
        break;
    case Totem::AudioFft::PeakGroup::High:
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
    if (event.group == Totem::AudioFft::PeakGroup::Bass ||
        event.group == Totem::AudioFft::PeakGroup::High) {
        ret.combine(
            detail::publishIoCommand(Totem::LedPwm::CommandEvent::startPulse(
                PeripheralLed::Bulb1,
                detail::makePulse(*profile, event, nowMs, 0xB11BU))));
    }
    if (event.group == Totem::AudioFft::PeakGroup::Mid ||
        event.group == Totem::AudioFft::PeakGroup::High) {
        ret.combine(
            detail::publishIoCommand(Totem::LedPwm::CommandEvent::startPulse(
                PeripheralLed::Bulb2,
                detail::makePulse(*profile, event, nowMs, 0xB22BU))));
    }
    return ret;
}

inline ReturnCode handleBeat(const Totem::AudioFft::BeatEvent &event) {
    detail::lastBeatSequence = event.sequence;
    const bool transition = event.kind != detail::lastBeatKind;
    detail::lastBeatKind = event.kind;
    if (config.beat.logStateTransitions &&
        (transition || event.kind == Totem::AudioFft::BeatEventKind::Reacquired ||
         event.kind == Totem::AudioFft::BeatEventKind::Lost)) {
        _log_i("Beat %s: bpm=%u confidence=%u energy=%u sequence=%lu",
               detail::beatKindName(event.kind), event.bpm, event.confidence,
               event.energy, static_cast<unsigned long>(event.sequence));
    }
    return OK();
}

inline ReturnCode handlePeak(const Totem::AudioFft::PeakEvent &event,
                             uint32_t nowMs) {
    auto ret = OK();
    ret.combine(handleDropWave(event, nowMs));
    ret.combine(handlePeakWave(event, nowMs));
    ret.combine(handleIoPeak(event, nowMs));
    return ret;
}

inline ReturnCode handleButton(const Totem::Data::ButtonEvent &event,
                               uint32_t nowMs) {
    if (event.event != Totem::Button::Event::Pressed) {
        return OK();
    }

    if (event.button == PeripheralButton::Calibration) {
        _log_i("Audio calibration button press observed");
        return OK();
    }

    if (!config.bell.publish || event.button != PeripheralButton::Bell) {
        return OK();
    }

    const auto elapsed = nowMs - detail::lastBellAnimationMs;
    if (detail::lastBellAnimationMs != 0 &&
        elapsed < config.bell.minIntervalMs) {
        return OK();
    }

    detail::lastBellAnimationMs = nowMs;
    return detail::publishBellAnimation(nowMs);
}

inline ReturnCode work(uint32_t nowMs, bool allowNormalOperation = true) {
    if (detail::batteryStatusQueue != nullptr) {
        Totem::BatteryMonitor::BatteryStatusEvent event{};
        while (Totem::Queue::Platform::receive(detail::batteryStatusQueue,
                                               &event, 0)
                   .ok()) {
            detail::latestBatteryStatus = event;
            detail::hasBatteryStatus = true;
        }
    }

    if (detail::menuEventQueue != nullptr) {
        Totem::Data::MenuEvent event{};
        while (
            Totem::Queue::Platform::receive(detail::menuEventQueue, &event, 0)
                .ok()) {
            if (allowNormalOperation) {
                FAIL_IF_ERR_FWD(handleMenu(event, nowMs),
                                "Failed to handle queued menu event");
            }
        }
    }

    if (detail::dialEventQueue != nullptr) {
        Totem::Data::DialEvent event{};
        Totem::Data::DialEvent latest{};
        bool hasEvent = false;
        while (
            Totem::Queue::Platform::receive(detail::dialEventQueue, &event, 0)
                .ok()) {
            latest = event;
            hasEvent = true;
        }
        if (allowNormalOperation && hasEvent) {
            FAIL_IF_ERR_FWD(handleDial(latest),
                            "Failed to handle queued dial event");
        }
    }

    if (detail::beatEventQueue != nullptr) {
        Totem::AudioFft::BeatEvent beat{};
        while (Totem::Queue::Platform::receive(detail::beatEventQueue, &beat, 0)
                   .ok()) {
            if (allowNormalOperation) {
                FAIL_IF_ERR_FWD(handleBeat(beat),
                                "Failed to handle queued beat event");
            }
        }
    }

    if (detail::fftFrameQueue != nullptr) {
        Totem::AudioFft::FftFrame frame{};
        Totem::AudioFft::FftFrame latest{};
        bool hasFrame = false;
        while (Totem::Queue::Platform::receive(detail::fftFrameQueue, &frame, 0)
                   .ok()) {
            latest = frame;
            hasFrame = true;
        }
        if (allowNormalOperation && hasFrame) {
            FAIL_IF_ERR_FWD(handleTotalEnergyWave(latest, nowMs),
                            "Failed to handle queued FFT frame");
        }
    }

    if (detail::peakEventQueue != nullptr) {
        Totem::AudioFft::PeakEvent peak{};
        while (Totem::Queue::Platform::receive(detail::peakEventQueue, &peak, 0)
                   .ok()) {
            if (allowNormalOperation) {
                FAIL_IF_ERR_FWD(handlePeak(peak, nowMs),
                                "Failed to handle queued peak event");
            }
        }
    }

    if (detail::buttonEventQueue != nullptr) {
        Totem::Data::ButtonEvent button{};
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
