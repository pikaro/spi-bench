#pragma once

#include "LedDisplay/Animations/SpectralWeave/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include <cstdint>
#include <expected>

namespace Totem::LedDisplay::Animations {

struct SpectralWeaveCommand : SpectralWeaveSpec {
    static std::expected<AnimationCommand, ReturnCode>
    makeCommand(SpectralWeaveConfig commandConfig = {}, uint16_t requestId = 0,
                uint16_t lifetimeMs = defaultLifetimeMs,
                Layer layer = defaultLayer) {
        auto cmd = AnimationCommand{.type = AnimationCommandType::Play,
                                    .kind = kind,
                                    .requestId = requestId,
                                    .layer = layer,
                                    .lifetimeMs = lifetimeMs};
        FAIL_IF_ERR_FWD_UNEXPECTED(
            ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
            "Failed to encode spectral weave config");
        return cmd;
    }
};

} // namespace Totem::LedDisplay::Animations
