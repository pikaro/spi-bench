#pragma once

#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "LedDisplay/Interfaces/AnimationCommandCodec.hpp"
#include "LedDisplay/Interfaces/LayerControl.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/Pool.hpp"
#include "Services/PubSub.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <expected>
#include <limits>

namespace Totem::LedDisplay {

inline AnimationCommand makeStopAnimationCommand(uint16_t requestId = 0) {
    return AnimationCommand{
        .type = AnimationCommandType::Stop,
        .kind = AnimationKind::None,
        .requestId = requestId,
        .layer = Layer::Effect,
        .lifetimeMs = 0,
        .payloadSize = 0,
    };
}

inline std::expected<AnimationCommand, ReturnCode>
makeHueOffsetCommand(Angle<uint8_t> offset, uint16_t requestId = 0) {
    auto cmd = AnimationCommand{
        .type = AnimationCommandType::SetHueOffset,
        .kind = AnimationKind::None,
        .requestId = requestId,
        .layer = Layer::Effect,
        .lifetimeMs = 0,
    };
    FAIL_IF_ERR_FWD_UNEXPECTED(encodeCommandPayload(cmd, offset),
                               "Failed to encode LED hue offset");
    return cmd;
}

inline std::expected<AnimationCommand, ReturnCode>
makeRotationOffsetCommand(Angle<uint8_t> offset, uint16_t requestId = 0) {
    auto cmd = AnimationCommand{
        .type = AnimationCommandType::SetRotationOffset,
        .kind = AnimationKind::None,
        .requestId = requestId,
        .layer = Layer::Effect,
        .lifetimeMs = 0,
    };
    FAIL_IF_ERR_FWD_UNEXPECTED(encodeCommandPayload(cmd, offset),
                               "Failed to encode LED rotation offset");
    return cmd;
}

inline std::expected<AnimationCommand, ReturnCode>
makeBrightnessCommand(uint8_t brightness, uint16_t requestId = 0) {
    auto cmd = AnimationCommand{
        .type = AnimationCommandType::SetBrightness,
        .kind = AnimationKind::None,
        .requestId = requestId,
        .layer = Layer::Effect,
        .lifetimeMs = 0,
    };
    FAIL_IF_ERR_FWD_UNEXPECTED(
        encodeCommandPayload(cmd, DisplayBrightness{.value = brightness}),
        "Failed to encode LED brightness");
    return cmd;
}

inline std::expected<AnimationCommand, ReturnCode>
makeLayerActiveCommand(Layer layer, bool active, uint16_t requestId = 0) {
    auto cmd = AnimationCommand{
        .type = AnimationCommandType::SetLayerActive,
        .kind = AnimationKind::None,
        .requestId = requestId,
        .layer = Layer::Effect,
        .lifetimeMs = 0,
    };
    FAIL_IF_ERR_FWD_UNEXPECTED(
        encodeCommandPayload(cmd,
                             LayerActive{.layer = layer, .active = active}),
        "Failed to encode LED layer active state");
    return cmd;
}

inline std::expected<AnimationCommand, ReturnCode>
makeLayerOpacityCommand(Layer layer, uint8_t opacity, uint16_t requestId = 0) {
    auto cmd = AnimationCommand{
        .type = AnimationCommandType::SetLayerOpacity,
        .kind = AnimationKind::None,
        .requestId = requestId,
        .layer = Layer::Effect,
        .lifetimeMs = 0,
    };
    FAIL_IF_ERR_FWD_UNEXPECTED(
        encodeCommandPayload(cmd,
                             LayerOpacity{.layer = layer, .opacity = opacity}),
        "Failed to encode LED layer opacity");
    return cmd;
}

inline std::expected<AnimationCommand, ReturnCode>
makeLayerFadeSwapCommand(Layer first, Layer second, uint16_t durationMs,
                         uint16_t requestId = 0) {
    auto cmd = AnimationCommand{
        .type = AnimationCommandType::FadeLayerSwap,
        .kind = AnimationKind::None,
        .requestId = requestId,
        .layer = Layer::Effect,
        .lifetimeMs = 0,
    };
    FAIL_IF_ERR_FWD_UNEXPECTED(
        encodeCommandPayload(cmd,
                             LayerFadeSwap{
                                 .first = first,
                                 .second = second,
                                 .durationMs = durationMs,
                             }),
        "Failed to encode LED layer fade swap");
    return cmd;
}

inline ReturnCode publishAnimationCommand(AnimationCommand cmd) {
    using Pool =
        Totem::PubSubBackend::Pool<AnimationCommand,
                                   LedDisplayConfig::animationPublishPoolSize>;
    static Pool pool{};

    FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                "PubSub backend is not configured");

    auto &pubSub = PubSubService::get();
    const auto messageId = pubSub.nextMessageId();
    FAIL_IF(messageId == 0, ERR(CoreError, InvalidState),
            "PubSub returned message ID 0");

    if (cmd.requestId == 0 && cmd.type != AnimationCommandType::Stop) {
        constexpr uint16_t firstNonzeroRequestId = 1;
        constexpr uint32_t requestIdMask = std::numeric_limits<uint16_t>::max();
        cmd.requestId = static_cast<uint16_t>(messageId & requestIdMask);
        if (cmd.requestId == 0) {
            cmd.requestId = firstNonzeroRequestId;
        }
    }

    auto stored = pool.store(cmd, messageId);
    if (!stored) {
        return stored.error();
    }

    auto envelopeResult =
        Totem::PubSubBackend::Envelope::make<AnimationCommand>({
            .owner = static_cast<void *>(&pool),
            .topic = NodeData::PubSub::Topic::Animation,
            .messageId = messageId,
            .getPayloadPtr = Pool::getPtr,
            .encodePayload = Pool::encodePayload,
            .release = Pool::release,
            .requireSyncedClock = false,
        });
    if (!envelopeResult) {
        (void)pool.release({.header = {.messageId = messageId}});
        return envelopeResult.error();
    }

    auto publishResult = pubSub.publish(*envelopeResult);
    if (!publishResult.ok()) {
        (void)pool.release(*envelopeResult);
        return publishResult;
    }

    return OK();
}

} // namespace Totem::LedDisplay
