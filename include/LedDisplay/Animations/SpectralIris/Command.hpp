#pragma once

#include "LedDisplay/Animations/SpectralIris/Config.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include <cstdint>
#include <expected>

namespace Totem::LedDisplay::Animations {

struct SpectralIrisCommand : SpectralIrisSpec {
    static std::expected<AnimationCommand, ReturnCode>
    makeCommand(SpectralIrisConfig commandConfig = {}, uint16_t requestId = 0,
                uint16_t lifetimeMs = defaultLifetimeMs,
                Layer layer = defaultLayer) {
        auto cmd = AnimationCommand{.type = AnimationCommandType::Play,
                                    .kind = kind,
                                    .requestId = requestId,
                                    .layer = layer,
                                    .lifetimeMs = lifetimeMs};
        FAIL_IF_ERR_FWD_UNEXPECTED(
            ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
            "Failed to encode spectral iris config");
        return cmd;
    }
};

} // namespace Totem::LedDisplay::Animations
