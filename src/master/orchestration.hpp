#pragma once

#include "Audio/Interfaces/Types.hpp"
#include "Audio/Interfaces/Wire.hpp"
#include "Buttons/Interfaces/EventFactory.hpp"
#include "Buttons/Interfaces/Wire.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "Data/Peripherals.hpp"
#include "LedDisplay/Animations/CenterWave/Command.hpp"
#include "LedDisplay/Animations/SineWave/Command.hpp"
#include "LedDisplay/Animations/SineWave/Config.hpp"
#include "LedDisplay/Animations/WheelIndicator/Command.hpp"
#include "LedDisplay/Animations/WheelIndicator/Config.hpp"
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
    bool publishCenterWave = true;
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

struct Config {
    WheelMapping wheel{};
    PeakWaveMapping peakWave{};
    BeatMapping beat{};
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
inline bool calibrateAudioCommandRegistered = false;
inline Angle<uint16_t> wheelOffset{};
inline bool wheelOffsetDirty = false;
inline bool wheelIndicatorDirty = false;
inline bool ioStartupPublished = false;
inline uint32_t lastWheelPublishMs = 0;
inline uint32_t lastIoStartupPublishMs = 0;
inline uint32_t lastBellSinelonMs = 0;
inline uint32_t lastBeatSequence = 0;
inline Totem::Audio::BeatEventKind lastBeatKind = Totem::Audio::BeatEventKind::Lost;
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
    if (!detail::calibrateAudioCommandRegistered) {
        FAIL_IF_UNEXPECTED_FWD(
            commandKey,
            CommandRegistrarService::get().registerCommand(
                detail::calibrateAudioCmd),
            "Failed to register /calibrate-audio command");
        (void)commandKey;
        detail::calibrateAudioCommandRegistered = true;
    }
    _log_i("Master orchestration subscribed to wheel, beat, peak, and button "
           "events");
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
