#pragma once

#include "LedDisplay/Animations/All.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include "Macros/Facade.hpp"
#include <expected>

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

inline std::expected<AnimationCommand, ReturnCode>
DiagnosticFill::makeCommand(DiagnosticFillConfig commandConfig,
                            uint16_t requestId, uint16_t lifetimeMs,
                            Layer layer) {
    auto cmd = AnimationCommand{.type = AnimationCommandType::Play,
                                .kind = kind,
                                .requestId = requestId,
                                .layer = layer,
                                .lifetimeMs = lifetimeMs};
    FAIL_IF_ERR_FWD_UNEXPECTED(
        ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
        "Failed to encode diagnostic fill config");
    return cmd;
}

inline std::expected<AnimationCommand, ReturnCode>
FftReactive::makeCommand(FftReactiveConfig commandConfig, uint16_t requestId,
                         uint16_t lifetimeMs, Layer layer) {
    auto cmd = AnimationCommand{.type = AnimationCommandType::Play,
                                .kind = kind,
                                .requestId = requestId,
                                .layer = layer,
                                .lifetimeMs = lifetimeMs};
    FAIL_IF_ERR_FWD_UNEXPECTED(
        ::Totem::LedDisplay::encodeCommandPayload(cmd, commandConfig),
        "Failed to encode FFT reactive config");
    return cmd;
}

} // namespace Totem::LedDisplay::Animations
