#pragma once

#include "LedDisplay/Animations/CenterWave.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"

namespace Totem::LedDisplay::Animations {

inline std::expected<AnimationCommand, ReturnCode>
CenterWave::makeCommand(CenterWaveConfig commandConfig, uint16_t requestId,
                        uint16_t lifetimeMs, Layer layer) {
    auto cmd = AnimationCommand{.type = AnimationCommandType::Play,
                                .kind = kind,
                                .requestId = requestId,
                                .layer = layer,
                                .lifetimeMs = lifetimeMs};
    FAIL_IF_ERR_FWD_UNEXPECTED(
        ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
        "Failed to encode center wave config");
    return cmd;
}

} // namespace Totem::LedDisplay::Animations
