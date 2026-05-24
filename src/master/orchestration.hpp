#pragma once

#include "Audio/Interfaces/Wire.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Queue/Facade.hpp"
#include "Services/PubSub.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include "Wheel/Interfaces/Wire.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace MasterOrchestration {

struct WheelMapping {
    bool publishHueOffset = true;
    bool publishRotationOffset = true;
    float hueTurnsPerWheelTurn = 1.0F;
    float rotationTurnsPerWheelTurn = 1.0F;
    uint32_t publishMinIntervalMs = 20;
};

struct BeatWaveMapping {
    bool publishCenterWave = true;
    std::array<uint8_t, Totem::Audio::beatGroupCount> hueByGroup{
        {144, 96, 32}};
    uint16_t lifetimeMs = 900;
    uint8_t value = 180;
    uint8_t width = 5;
    uint32_t minIntervalMs = 120;
};

struct Config {
    WheelMapping wheel{};
    BeatWaveMapping beatWave{};
};

inline constexpr Config config{};
inline constexpr size_t wheelEventQueueSize = 8;
inline constexpr size_t beatEventQueueSize = 8;

namespace detail {

inline Totem::Queue::Platform::Storage<Totem::Wheel::WheelState,
                                       wheelEventQueueSize>
    wheelEventQueueStorage{};
inline Totem::Queue::Platform::Storage<Totem::Audio::BeatEvent,
                                       beatEventQueueSize>
    beatEventQueueStorage{};
inline Totem::Queue::Handle wheelEventQueue = nullptr;
inline Totem::Queue::Handle beatEventQueue = nullptr;
inline Totem::PubSubBackend::SubscriberKey wheelSubscription = 0;
inline Totem::PubSubBackend::SubscriberKey beatSubscription = 0;
inline Angle<uint16_t> wheelOffset{};
inline bool wheelOffsetDirty = false;
inline uint32_t lastWheelPublishMs = 0;
inline std::array<uint32_t, Totem::Audio::beatGroupCount> lastBeatWaveMs{};

inline Angle<uint8_t> scaleToCommandAngle(Angle<uint16_t> angle,
                                          float turnsPerTurn) {
    return Angle<uint8_t>::fromTurns(angle.turns() * turnsPerTurn);
}

inline ReturnCode publishWheelOffsets() {
    if (config.wheel.publishHueOffset) {
        FAIL_IF_UNEXPECTED_FWD(
            hueCmd,
            Totem::LedDisplay::makeHueOffsetCommand(scaleToCommandAngle(
                wheelOffset, config.wheel.hueTurnsPerWheelTurn)),
            "Failed to build orchestrated hue offset command");
        FAIL_IF_ERR_FWD(
            Totem::LedDisplay::publishAnimationCommand(hueCmd),
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

    return OK();
}

inline ReturnCode onWheelEnvelope(
    void * /*unused*/, const Totem::PubSubBackend::Envelope &envelope) {
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

inline ReturnCode onBeatEnvelope(
    void * /*unused*/, const Totem::PubSubBackend::Envelope &envelope) {
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
    _log_i("Master orchestration subscribed to wheel and beat events");
    return OK();
}

inline ReturnCode handleWheel(const Totem::Wheel::WheelState &state) {
    if (state.delta.value == 0) {
        return OK();
    }
    detail::wheelOffset += state.delta;
    detail::wheelOffsetDirty = true;
    return OK();
}

inline ReturnCode handleBeat(const Totem::Audio::BeatEvent &event,
                             uint32_t nowMs) {
    if (!config.beatWave.publishCenterWave) {
        return OK();
    }
    const auto groupIndex = Totem::Audio::beatGroupIndex(event.group);
    if (groupIndex >= Totem::Audio::beatGroupCount) {
        return OK();
    }

    const auto lastMs = detail::lastBeatWaveMs[groupIndex];
    const auto elapsed = nowMs - lastMs;
    if (lastMs != 0 && elapsed < config.beatWave.minIntervalMs) {
        return OK();
    }

    detail::lastBeatWaveMs[groupIndex] = nowMs;
    return Totem::LedDisplay::publishAnimation({
        .kind = Totem::LedDisplay::AnimationKind::CenterWave,
        .lifetimeMs = config.beatWave.lifetimeMs,
        .hue = config.beatWave.hueByGroup[groupIndex],
        .value = config.beatWave.value,
        .param = config.beatWave.width,
    });
}

inline ReturnCode work(uint32_t nowMs) {
    if (detail::beatEventQueue != nullptr) {
        Totem::Audio::BeatEvent beat{};
        while (Totem::Queue::Platform::receive(detail::beatEventQueue, &beat, 0)
                   .ok()) {
            FAIL_IF_ERR_FWD(handleBeat(beat, nowMs),
                            "Failed to handle queued beat event");
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

    if (!detail::wheelOffsetDirty) {
        return OK();
    }

    const auto elapsed = nowMs - detail::lastWheelPublishMs;
    if (detail::lastWheelPublishMs != 0 &&
        elapsed < config.wheel.publishMinIntervalMs) {
        return OK();
    }

    detail::lastWheelPublishMs = nowMs;
    FAIL_IF_ERR_FWD(detail::publishWheelOffsets(),
                    "Failed to publish orchestrated wheel offsets");
    detail::wheelOffsetDirty = false;
    return OK();
}

} // namespace MasterOrchestration
