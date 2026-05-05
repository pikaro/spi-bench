#pragma once

#include "LedDisplay/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/Codec.hpp"
#include "PubSubBackend/detail/Pool.hpp"
#include "Services/PubSub.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include "Types/Error.hpp"
#include <algorithm>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::LedDisplay {

struct AnimationCommandOptions {
    AnimationKind kind = AnimationKind::CenterWave;
    PrimitiveKind primitive = PrimitiveKind::Explosion;
    uint16_t requestId = 0;
    uint16_t lifetimeMs = 1200;
    uint8_t hue = 144;
    uint8_t saturation = 255;
    uint8_t value = 180;
    uint8_t param = 5;
};

template <typename T>
inline ReturnCode encodeAnimationPayload(AnimationCommand &cmd,
                                         const T &payload) {
    constexpr size_t size = Totem::PubSubBackend::detail::Codec<
        T>::encodedSize();
    static_assert(size <= LedDisplayConfig::animationCommandPayloadBytes,
                  "Animation config does not fit command payload");
    cmd.payloadSize = static_cast<uint8_t>(size);
    return Totem::PubSubBackend::detail::Codec<T>::encode(
        payload, std::span<std::byte>(cmd.payload).first(size));
}

inline std::expected<AnimationCommand, ReturnCode>
makeAnimationCommand(AnimationCommandOptions options) {
    auto cmd = AnimationCommand{
        .type = AnimationCommandType::Play,
        .kind = options.kind,
        .requestId = options.requestId,
        .layer = Layer::Main,
        .lifetimeMs = options.lifetimeMs,
    };

    switch (options.kind) {
    case AnimationKind::DiagnosticFill:
        FAIL_IF_ERR_FWD_UNEXPECTED(encodeAnimationPayload(
                                       cmd, DiagnosticFillConfig{
                                                .hue = options.hue,
                                                .saturation = options.saturation,
                                                .value = options.value,
                                            }),
                                   "Failed to encode diagnostic fill "
                                   "animation");
        break;
    case AnimationKind::CenterWave:
        FAIL_IF_ERR_FWD_UNEXPECTED(encodeAnimationPayload(
                                       cmd, CenterWaveConfig{
                                                .hue = options.hue,
                                                .saturation = options.saturation,
                                                .value = options.value,
                                                .width = std::max<uint8_t>(
                                                    options.param, 1U),
                                            }),
                                   "Failed to encode center wave animation");
        break;
    case AnimationKind::FftReactive:
        cmd.lifetimeMs = options.lifetimeMs;
        FAIL_IF_ERR_FWD_UNEXPECTED(encodeAnimationPayload(
                                       cmd, FftReactiveConfig{
                                                .baseHue = options.hue,
                                                .saturation = options.saturation,
                                                .valueScale = options.value,
                                            }),
                                   "Failed to encode FFT reactive animation");
        break;
    case AnimationKind::PrimitiveDemo:
        FAIL_IF_ERR_FWD_UNEXPECTED(encodeAnimationPayload(
                                       cmd, PrimitiveDemoConfig{
                                                .primitive = options.primitive,
                                                .hue = options.hue,
                                                .saturation = options.saturation,
                                                .value = options.value,
                                                .width = std::max<uint8_t>(
                                                    options.param, 1U),
                                                .density = 48,
                                                .speed = 128,
                                            }),
                                   "Failed to encode primitive demo animation");
        break;
    default:
        FAIL(std::unexpected(ERR(CoreError, InvalidArgument)),
             "Unknown animation kind");
    }

    return cmd;
}

inline AnimationCommand makeStopAnimationCommand(uint16_t requestId = 0) {
    return AnimationCommand{
        .type = AnimationCommandType::Stop,
        .kind = AnimationKind::CenterWave,
        .requestId = requestId,
        .layer = Layer::Main,
        .lifetimeMs = 0,
        .payloadSize = 0,
    };
}

inline ReturnCode publishAnimationCommand(AnimationCommand cmd) {
    using Pool = Totem::PubSubBackend::Pool<
        AnimationCommand, LedDisplayConfig::animationPublishPoolSize>;
    static Pool pool{};

    FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                "PubSub backend is not configured");

    auto &pubSub = PubSubService::get();
    const auto messageId = pubSub.nextMessageId();
    FAIL_IF(messageId == 0, ERR(CoreError, InvalidState),
            "PubSub returned message ID 0");

    if (cmd.requestId == 0) {
        cmd.requestId = static_cast<uint16_t>(messageId & 0xFFFFU);
        if (cmd.requestId == 0) {
            cmd.requestId = 1;
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

inline ReturnCode publishAnimation(AnimationCommandOptions options) {
    FAIL_IF_UNEXPECTED_FWD(cmd, makeAnimationCommand(options),
                           "Failed to build animation command");
    return publishAnimationCommand(cmd);
}

} // namespace Totem::LedDisplay
