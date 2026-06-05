#pragma once

#include "LedDisplay/Animations/Shutter/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include <cstdint>
#include <expected>

namespace Totem::LedDisplay::Animations {

struct ShutterCommand : ShutterSpec {
    static std::expected<AnimationPlayCommand, ReturnCode>
    makeCommand(ShutterConfig commandConfig = {}, uint16_t requestId = 0,
                uint16_t lifetimeMs = defaultLifetimeMs,
                Layer layer = defaultLayer) {
        auto cmd = AnimationPlayCommand{.kind = kind,
                                    .requestId = requestId,
                                    .layer = layer,
                                    .lifetimeMs = lifetimeMs};
        FAIL_IF_ERR_FWD_UNEXPECTED(
            ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
            "Failed to encode shutter config");
        return cmd;
    }
};

} // namespace Totem::LedDisplay::Animations
