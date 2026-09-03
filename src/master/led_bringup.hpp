#pragma once

#include "LedDisplay/Animations/SpokeSweep/Command.hpp"
#include "LedDisplay/Animations/SpokeSweep/Config.hpp"
#include "LedDisplay/Animations/WheelIndicator/Command.hpp"
#include "LedDisplay/Animations/WheelIndicator/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include "LedDisplay/Interfaces/OutputStartup.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "Services/PubSub.hpp"
#include "Types/Error.hpp"
#include <atomic>
#include <cstdint>

namespace MasterLedBringup {

struct SpokeProbeConfig {
    bool enabled = true;
    uint16_t lifetimeMs = 2000;
    uint16_t requestId =
        Totem::LedDisplay::Animations::SpokeSweepCommand::defaultRequestId;
    Totem::LedDisplay::Animations::SpokeSweepConfig animation{
        .hueStride = 16,
        .cycles = 3,
    };
};

struct WheelIndicatorConfig {
    bool enabled = false;
    uint32_t publishDelayMs = 0;
    uint16_t requestId =
        Totem::LedDisplay::Animations::WheelIndicatorCommand::defaultRequestId;
    Totem::LedDisplay::Animations::WheelIndicatorConfig animation{};
};

struct Config {
    SpokeProbeConfig spokeProbe{};
    WheelIndicatorConfig wheelIndicator{};
};

inline constexpr Config config{};

namespace detail {

inline constexpr uint16_t requiredGpuMask =
    static_cast<uint16_t>(Totem::Data::PubSub::NodeId::GPUNode0) |
    static_cast<uint16_t>(Totem::Data::PubSub::NodeId::GPUNode1);

inline std::atomic<uint16_t> readyGpuMask{0};
inline Totem::PubSubBackend::SubscriberKey readySubscription = 0;
inline uint32_t bootAnimationPublishedMs = 0;
inline bool outputEnablePublished = false;
inline bool spokeProbePublished = false;
inline bool wheelIndicatorPublished = false;

inline ReturnCode
onOutputReadyEnvelope(void * /*unused*/,
                      const Totem::PubSubBackend::Envelope &envelope) {
    FAIL_IF_UNEXPECTED_FWD(
        event, envelope.getPayloadAs<Totem::LedDisplay::OutputReadyEvent>(),
        "Failed to decode GPU LED output-readiness event");
    (void)event;

    const auto source = envelope.header.source;
    FAIL_IF(source != static_cast<uint16_t>(
                          Totem::Data::PubSub::NodeId::GPUNode0) &&
                source != static_cast<uint16_t>(
                              Totem::Data::PubSub::NodeId::GPUNode1),
            ERR(CoreError, InvalidArgument),
            "LED output-readiness event did not come from a GPU");

    const auto previous =
        readyGpuMask.fetch_or(source, std::memory_order_acq_rel);
    if ((previous & source) == 0) {
        _log_i("GPU LED output ready: node=0x%04x readyMask=0x%04x",
               static_cast<unsigned>(source),
               static_cast<unsigned>(previous | source));
    }
    return OK();
}

inline bool allGpuOutputsReady() {
    return (readyGpuMask.load(std::memory_order_acquire) & requiredGpuMask) ==
           requiredGpuMask;
}

inline bool bootAnimationFinished(uint32_t nowMs) {
    if (!spokeProbePublished) {
        return false;
    }
    if (!config.spokeProbe.enabled) {
        return true;
    }
    return static_cast<uint32_t>(nowMs - bootAnimationPublishedMs) >=
           config.spokeProbe.lifetimeMs;
}

inline ReturnCode publishOutputEnable() {
    FAIL_IF_ERR_FWD(
        PubSubService::publish(PubSubService::Topic::LedOutputEnable,
                               Totem::LedDisplay::OutputEnableCommand{}),
        "Failed to publish master LED output-enable command");
    _log_i("Published master LED output-enable command");
    return OK();
}

inline ReturnCode publishSpokeProbe() {
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::Animations::SpokeSweepCommand::makeCommand(
            config.spokeProbe.animation, config.spokeProbe.requestId,
            config.spokeProbe.lifetimeMs),
        "Failed to build master LED bringup spoke probe");
    FAIL_IF_ERR_FWD(Totem::LedDisplay::publishAnimationPlayCommand(cmd),
                    "Failed to publish master LED bringup spoke probe");
    _log_i("Published master LED boot animation request=%u lifetime=%ums",
           config.spokeProbe.requestId, config.spokeProbe.lifetimeMs);
    return OK();
}

inline ReturnCode publishWheelIndicator() {
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::Animations::WheelIndicatorCommand::makeCommand(
            config.wheelIndicator.animation, config.wheelIndicator.requestId),
        "Failed to build master LED bringup wheel indicator");
    FAIL_IF_ERR_FWD(Totem::LedDisplay::publishAnimationPlayCommand(cmd),
                    "Failed to publish master LED bringup wheel indicator");
    _log_i("Published master LED bringup wheel indicator request=%u",
           config.wheelIndicator.requestId);
    return OK();
}

} // namespace detail

inline ReturnCode begin() {
    detail::readyGpuMask.store(0, std::memory_order_release);
    detail::bootAnimationPublishedMs = 0;
    detail::outputEnablePublished = false;
    detail::spokeProbePublished = false;
    detail::wheelIndicatorPublished = false;

    FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                "PubSub backend is not configured for master LED bringup");
    if (detail::readySubscription == 0) {
        FAIL_IF_UNEXPECTED_FWD(
            subscription,
            PubSubService::get().subscribe(
                "master-led-ready",
                {.subscriber = nullptr,
                 .callback = detail::onOutputReadyEnvelope},
                PubSubService::Topic::LedOutputReady),
            "Failed to subscribe to GPU LED output readiness");
        detail::readySubscription = subscription;
    }
    _log_i("Master LED bringup waiting for GPU0 and GPU1 output readiness");
    return OK();
}

inline bool normalOperationAllowed(uint32_t nowMs) {
    return detail::bootAnimationFinished(nowMs);
}

inline ReturnCode work(uint32_t nowMs) {
    if (!detail::outputEnablePublished && detail::allGpuOutputsReady()) {
        FAIL_IF_ERR_FWD(detail::publishOutputEnable(),
                        "Failed to enable GPU LED outputs");
        detail::outputEnablePublished = true;
    }

    if (detail::outputEnablePublished && !detail::spokeProbePublished) {
        if (!config.spokeProbe.enabled) {
            detail::bootAnimationPublishedMs = nowMs;
            detail::spokeProbePublished = true;
        } else {
            FAIL_IF_ERR_FWD(detail::publishSpokeProbe(),
                            "Failed to publish master LED boot animation");
            detail::bootAnimationPublishedMs = nowMs;
            detail::spokeProbePublished = true;
        }
    }

    if (config.wheelIndicator.enabled && !detail::wheelIndicatorPublished &&
        detail::bootAnimationFinished(nowMs) &&
        static_cast<uint32_t>(nowMs - detail::bootAnimationPublishedMs) >=
            (config.spokeProbe.enabled ? config.spokeProbe.lifetimeMs : 0U) +
                config.wheelIndicator.publishDelayMs) {
        FAIL_IF_ERR_FWD(detail::publishWheelIndicator(),
                        "Failed to run master LED bringup wheel indicator");
        detail::wheelIndicatorPublished = true;
    }

    return OK();
}

} // namespace MasterLedBringup
