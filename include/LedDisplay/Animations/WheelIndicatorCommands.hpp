#pragma once

#include "LedDisplay/Animations/WheelIndicator.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"

namespace Totem::LedDisplay::Animations {

inline std::expected<AnimationCommand, ReturnCode>
WheelIndicator::makeCommand(WheelIndicatorConfig commandConfig,
                            uint16_t requestId, uint16_t lifetimeMs,
                            Layer layer) {
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

inline std::expected<AnimationCommand, ReturnCode>
WheelIndicator::makeUpdateCommand(WheelIndicatorConfig commandConfig,
                                  uint16_t requestId, Layer layer) {
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

} // namespace Totem::LedDisplay::Animations
