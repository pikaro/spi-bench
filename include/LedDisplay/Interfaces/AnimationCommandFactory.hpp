#pragma once

#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/Interfaces/Types.hpp"
#include "PubSubBackend/detail/Pool.hpp"
#include "Services/PubSub.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>

namespace Totem::LedDisplay {

namespace detail {

inline std::expected<Totem::PubSubBackend::MessageId, ReturnCode>
nextAnimationMessageId() {
    FAIL_IF_NOT(PubSubService::configured(),
                std::unexpected(ERR(CoreError, InvalidState)),
                "PubSub backend is not configured");

    auto &pubSub = PubSubService::get();
    const auto messageId = pubSub.nextMessageId();
    FAIL_IF(messageId == 0, std::unexpected(ERR(CoreError, InvalidState)),
            "PubSub returned message ID 0");
    return messageId;
}

template <typename Command>
inline ReturnCode
publishAnimationPayload(Command cmd, NodeData::PubSub::Topic topic,
                        Totem::PubSubBackend::MessageId messageId) {
    using Pool =
        Totem::PubSubBackend::Pool<Command,
                                   LedDisplayConfig::animationPublishPoolSize>;
    static Pool pool{};

    FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                "PubSub backend is not configured");

    auto stored = pool.store(cmd, messageId);
    if (!stored) {
        return stored.error();
    }

    auto envelopeResult = Totem::PubSubBackend::Envelope::make<Command>({
        .owner = static_cast<void *>(&pool),
        .topic = topic,
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

    auto publishResult = PubSubService::get().publish(*envelopeResult);
    if (!publishResult.ok()) {
        (void)pool.release(*envelopeResult);
        return publishResult;
    }

    return OK();
}

} // namespace detail

inline std::expected<AnimationStopCommand, ReturnCode>
makeStopAnimationCommand(uint16_t requestId = 0) {
    return AnimationStopCommand{.requestId = requestId};
}

inline std::expected<AnimationSetHueOffsetCommand, ReturnCode>
makeHueOffsetCommand(Angle<uint8_t> offset) {
    return AnimationSetHueOffsetCommand{.offset = offset.value};
}

inline std::expected<AnimationSetRotationOffsetCommand, ReturnCode>
makeRotationOffsetCommand(Angle<uint8_t> offset) {
    return AnimationSetRotationOffsetCommand{.offset = offset.value};
}

inline std::expected<AnimationSetBrightnessCommand, ReturnCode>
makeBrightnessCommand(uint8_t brightness) {
    return AnimationSetBrightnessCommand{.value = brightness};
}

inline std::expected<AnimationSetLayerActiveCommand, ReturnCode>
makeLayerActiveCommand(Layer layer, bool active) {
    return AnimationSetLayerActiveCommand{.layer = layer, .active = active};
}

inline std::expected<AnimationSetLayerOpacityCommand, ReturnCode>
makeLayerOpacityCommand(Layer layer, uint8_t opacity) {
    return AnimationSetLayerOpacityCommand{.layer = layer, .opacity = opacity};
}

inline std::expected<AnimationFadeLayerSwapCommand, ReturnCode>
makeLayerFadeSwapCommand(Layer first, Layer second, uint16_t durationMs) {
    return AnimationFadeLayerSwapCommand{
        .first = first,
        .second = second,
        .durationMs = durationMs,
    };
}

inline ReturnCode publishAnimationPlayCommand(AnimationPlayCommand cmd) {
    FAIL_IF_UNEXPECTED_FWD(messageId, detail::nextAnimationMessageId(),
                           "Failed to allocate LED animation play message ID");
    if (cmd.requestId == 0) {
        constexpr uint16_t firstNonzeroRequestId = 1;
        constexpr uint32_t requestIdMask = std::numeric_limits<uint16_t>::max();
        cmd.requestId = static_cast<uint16_t>(messageId & requestIdMask);
        if (cmd.requestId == 0) {
            cmd.requestId = firstNonzeroRequestId;
        }
    }
    return detail::publishAnimationPayload(
        cmd, NodeData::PubSub::Topic::AnimationPlay, messageId);
}

inline ReturnCode publishAnimationUpdateCommand(AnimationUpdateCommand cmd) {
    FAIL_IF_UNEXPECTED_FWD(messageId, detail::nextAnimationMessageId(),
                           "Failed to allocate LED animation update message ID");
    return detail::publishAnimationPayload(
        cmd, NodeData::PubSub::Topic::AnimationUpdate, messageId);
}

template <typename Command>
inline ReturnCode publishAnimationCommand(Command cmd) {
    FAIL_IF_UNEXPECTED_FWD(messageId, detail::nextAnimationMessageId(),
                           "Failed to allocate LED animation event message ID");
    if constexpr (std::is_same_v<Command, AnimationStopCommand>) {
        return detail::publishAnimationPayload(
            cmd, NodeData::PubSub::Topic::AnimationStop, messageId);
    } else if constexpr (std::is_same_v<Command,
                                        AnimationSetHueOffsetCommand>) {
        return detail::publishAnimationPayload(
            cmd, NodeData::PubSub::Topic::AnimationSetHueOffset, messageId);
    } else if constexpr (std::is_same_v<Command,
                                        AnimationSetRotationOffsetCommand>) {
        return detail::publishAnimationPayload(
            cmd, NodeData::PubSub::Topic::AnimationSetRotationOffset, messageId);
    } else if constexpr (std::is_same_v<Command,
                                        AnimationSetBrightnessCommand>) {
        return detail::publishAnimationPayload(
            cmd, NodeData::PubSub::Topic::AnimationSetBrightness, messageId);
    } else if constexpr (std::is_same_v<Command,
                                        AnimationSetLayerActiveCommand>) {
        return detail::publishAnimationPayload(
            cmd, NodeData::PubSub::Topic::AnimationSetLayerActive, messageId);
    } else if constexpr (std::is_same_v<Command,
                                        AnimationSetLayerOpacityCommand>) {
        return detail::publishAnimationPayload(
            cmd, NodeData::PubSub::Topic::AnimationSetLayerOpacity, messageId);
    } else if constexpr (std::is_same_v<Command,
                                        AnimationFadeLayerSwapCommand>) {
        return detail::publishAnimationPayload(
            cmd, NodeData::PubSub::Topic::AnimationFadeLayerSwap, messageId);
    } else {
        static_assert(!std::is_same_v<Command, Command>,
                      "Unsupported LED animation event command");
    }
}

} // namespace Totem::LedDisplay
