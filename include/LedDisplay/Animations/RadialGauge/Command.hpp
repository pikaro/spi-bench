#pragma once

#include "LedDisplay/Animations/RadialGauge/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include <cstdint>
#include <expected>

namespace Totem::LedDisplay::Animations {

struct RadialGaugeCommand : RadialGaugeSpec {
    static std::expected<AnimationPlayCommand, ReturnCode>
    makeCommand(RadialGaugeConfig commandConfig = {},
                uint16_t requestId = defaultRequestId,
                uint16_t lifetimeMs = defaultLifetimeMs,
                Layer layer = defaultLayer) {
        auto cmd = AnimationPlayCommand{.kind = kind,
                                        .requestId = requestId,
                                        .layer = layer,
                                        .lifetimeMs = lifetimeMs};
        FAIL_IF_ERR_FWD_UNEXPECTED(
            ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
            "Failed to encode radial gauge config");
        return cmd;
    }
};

} // namespace Totem::LedDisplay::Animations
