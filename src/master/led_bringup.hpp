#pragma once

#include "LedDisplay/Animations/SpokeSweep/Command.hpp"
#include "LedDisplay/Animations/SpokeSweep/Config.hpp"
#include "LedDisplay/Animations/WheelIndicator/Command.hpp"
#include "LedDisplay/Animations/WheelIndicator/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommandFactory.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstdint>

namespace MasterLedBringup {

struct SpokeProbeConfig {
    bool enabled = true;
    // Give high-speed SPI/PubSub time to recover after a full-system reset.
    uint32_t publishDelayMs = 9000;
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

inline uint32_t startedMs = 0;
inline bool spokeProbePublished = false;
inline bool wheelIndicatorPublished = false;

inline bool readyFor(uint32_t nowMs, uint32_t delayMs) {
    return (nowMs - startedMs) >= delayMs;
}

inline uint32_t spokeProbeDoneAtMs() {
    if (!config.spokeProbe.enabled) {
        return 0;
    }
    return config.spokeProbe.publishDelayMs + config.spokeProbe.lifetimeMs;
}

inline uint32_t wheelIndicatorDelayMs() {
    if (config.wheelIndicator.publishDelayMs != 0) {
        return config.wheelIndicator.publishDelayMs;
    }
    return spokeProbeDoneAtMs();
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
    _log_i("Published master LED bringup spoke probe request=%u "
           "lifetime=%ums",
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

inline ReturnCode begin(uint32_t nowMs) {
    detail::startedMs = nowMs;
    detail::spokeProbePublished = false;
    detail::wheelIndicatorPublished = false;
    return OK();
}

inline bool normalOperationAllowed(uint32_t nowMs) {
    return detail::readyFor(nowMs, detail::spokeProbeDoneAtMs());
}

inline ReturnCode work(uint32_t nowMs) {
    if (config.spokeProbe.enabled && !detail::spokeProbePublished &&
        detail::readyFor(nowMs, config.spokeProbe.publishDelayMs)) {
        FAIL_IF_ERR_FWD(detail::publishSpokeProbe(),
                        "Failed to run master LED bringup spoke probe");
        detail::spokeProbePublished = true;
    }

    if (config.wheelIndicator.enabled && !detail::wheelIndicatorPublished &&
        detail::readyFor(nowMs, detail::wheelIndicatorDelayMs())) {
        FAIL_IF_ERR_FWD(detail::publishWheelIndicator(),
                        "Failed to run master LED bringup wheel indicator");
        detail::wheelIndicatorPublished = true;
    }

    return OK();
}

} // namespace MasterLedBringup
