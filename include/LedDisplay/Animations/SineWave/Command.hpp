#pragma once

#include "LedDisplay/Animations/SineWave/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include <cstdint>
#include <expected>

namespace Totem::LedDisplay::Animations {

struct SineWaveCommand : SineWaveSpec {
    static std::expected<AnimationPlayCommand, ReturnCode>
    makeCommand(SineWaveConfig commandConfig = {}, uint16_t requestId = 0,
                uint16_t lifetimeMs = 0, Layer layer = defaultLayer) {
        const auto resolvedLifetime =
            lifetimeMs == 0 ? projectedLifetimeMs(commandConfig) : lifetimeMs;
        auto cmd = AnimationPlayCommand{.kind = kind,
                                    .requestId = requestId,
                                    .layer = layer,
                                    .lifetimeMs = resolvedLifetime};
        FAIL_IF_ERR_FWD_UNEXPECTED(
            ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
            "Failed to encode sine wave config");
        return cmd;
    }
};

} // namespace Totem::LedDisplay::Animations
