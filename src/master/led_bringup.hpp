#pragma once

#include "LedDisplay/Animations/SpokeSweepCommands.hpp"
#include "LedDisplay/Animations/WheelIndicatorCommands.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace MasterLedBringup {

struct SpokeProbeConfig {
    bool enabled = true;
    uint32_t publishDelayMs = 5000;
    uint16_t lifetimeMs =
        Totem::LedDisplay::Animations::SpokeSweep::defaultLifetimeMs;
    uint16_t requestId =
        Totem::LedDisplay::Animations::SpokeSweep::defaultRequestId;
    Totem::LedDisplay::Animations::SpokeSweepConfig animation{};
};

struct WheelIndicatorConfig {
    bool enabled = true;
    uint32_t publishDelayMs = 5500;
    uint16_t requestId =
        Totem::LedDisplay::Animations::WheelIndicator::defaultRequestId;
    Totem::LedDisplay::Animations::WheelIndicatorConfig animation{};
};

struct Config {
    SpokeProbeConfig spokeProbe{};
    WheelIndicatorConfig wheelIndicator{};
};

inline constexpr Config config{};

namespace detail {

inline uint32_t startedMs = 0;
inline bool spokeProbePublished = false;
inline bool wheelIndicatorPublished = false;

inline bool readyFor(uint32_t nowMs, uint32_t delayMs) {
    return (nowMs - startedMs) >= delayMs;
}

inline ReturnCode publishSpokeProbe() {
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::Animations::SpokeSweep::makeCommand(
            config.spokeProbe.animation, config.spokeProbe.requestId,
            config.spokeProbe.lifetimeMs),
        "Failed to build master LED bringup spoke probe");
    FAIL_IF_ERR_FWD(Totem::LedDisplay::publishAnimationCommand(cmd),
                    "Failed to publish master LED bringup spoke probe");
    _log_i("Published master LED bringup spoke probe request=%u "
           "lifetime=%ums",
           config.spokeProbe.requestId, config.spokeProbe.lifetimeMs);
    return OK();
}

inline ReturnCode publishWheelIndicator() {
    FAIL_IF_UNEXPECTED_FWD(
        cmd,
        Totem::LedDisplay::Animations::WheelIndicator::makeCommand(
            config.wheelIndicator.animation, config.wheelIndicator.requestId),
        "Failed to build master LED bringup wheel indicator");
    FAIL_IF_ERR_FWD(Totem::LedDisplay::publishAnimationCommand(cmd),
                    "Failed to publish master LED bringup wheel indicator");
    _log_i("Published master LED bringup wheel indicator request=%u",
           config.wheelIndicator.requestId);
    return OK();
}

} // namespace detail

inline ReturnCode begin(uint32_t nowMs) {
    detail::startedMs = nowMs;
    detail::spokeProbePublished = false;
    detail::wheelIndicatorPublished = false;
    return OK();
}

inline ReturnCode work(uint32_t nowMs) {
    if (config.spokeProbe.enabled && !detail::spokeProbePublished &&
        detail::readyFor(nowMs, config.spokeProbe.publishDelayMs)) {
        FAIL_IF_ERR_FWD(detail::publishSpokeProbe(),
                        "Failed to run master LED bringup spoke probe");
        detail::spokeProbePublished = true;
    }

    if (config.wheelIndicator.enabled && !detail::wheelIndicatorPublished &&
        detail::readyFor(nowMs, config.wheelIndicator.publishDelayMs)) {
        FAIL_IF_ERR_FWD(detail::publishWheelIndicator(),
                        "Failed to run master LED bringup wheel indicator");
        detail::wheelIndicatorPublished = true;
    }

    return OK();
}

} // namespace MasterLedBringup
