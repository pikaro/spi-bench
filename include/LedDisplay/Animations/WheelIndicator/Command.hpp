#pragma once

#include "LedDisplay/Animations/WheelIndicator/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include <cstdint>
#include <expected>

namespace Totem::LedDisplay::Animations {

struct WheelIndicatorCommand : WheelIndicatorSpec {
    static std::expected<AnimationCommand, ReturnCode>
    makeCommand(WheelIndicatorConfig commandConfig = {}, uint16_t requestId = 0,
                uint16_t lifetimeMs = defaultLifetimeMs,
                Layer layer = defaultLayer) {
        auto cmd = AnimationCommand{.type = AnimationCommandType::Play,
                                    .kind = kind,
                                    .requestId = requestId,
                                    .layer = layer,
                                    .lifetimeMs = lifetimeMs};
        FAIL_IF_ERR_FWD_UNEXPECTED(
            ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
            "Failed to encode wheel indicator config");
        return cmd;
    }

    static std::expected<AnimationCommand, ReturnCode>
    makeUpdateCommand(WheelIndicatorConfig commandConfig = {},
                      uint16_t requestId = 0, Layer layer = defaultLayer) {
        auto cmd = AnimationCommand{.type = AnimationCommandType::Update,
                                    .kind = kind,
                                    .requestId = requestId,
                                    .layer = layer,
                                    .lifetimeMs = defaultLifetimeMs};
        FAIL_IF_ERR_FWD_UNEXPECTED(
            ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
            "Failed to encode wheel indicator update config");
        return cmd;
    }
};

} // namespace Totem::LedDisplay::Animations
