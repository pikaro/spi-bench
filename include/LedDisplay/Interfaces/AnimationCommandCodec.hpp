#pragma once

#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <expected>
#include <span>

namespace Totem::LedDisplay {

template <typename T>
ReturnCode encodeCommandPayload(AnimationCommand &cmd, const T &payload);

template <typename T>
std::expected<T, ReturnCode> decodeCommandPayload(const AnimationCommand &cmd);

} // namespace Totem::LedDisplay

#include "PubSubBackend/detail/Codec.hpp"

namespace Totem::LedDisplay {

template <typename T>
inline ReturnCode encodeCommandPayload(AnimationCommand &cmd, const T &payload) {
    constexpr size_t size =
        Totem::PubSubBackend::detail::Codec<T>::encodedSize();
    static_assert(size <= LedDisplayConfig::animationCommandPayloadBytes,
                  "Animation command payload does not fit wire envelope");
    cmd.payloadSize = static_cast<uint8_t>(size);
    return Totem::PubSubBackend::detail::Codec<T>::encode(
        payload, std::span<std::byte>(cmd.payload).first(size));
}

template <typename T>
inline std::expected<T, ReturnCode>
decodeCommandPayload(const AnimationCommand &cmd) {
    constexpr size_t size =
        Totem::PubSubBackend::detail::Codec<T>::encodedSize();
    FAIL_IF(cmd.payloadSize != size,
            std::unexpected(ERR(CoreError, InvalidSize)),
            "Unexpected animation command payload size");
    return Totem::PubSubBackend::detail::Codec<T>::decode(
        std::span<const std::byte>(cmd.payload).first(size));
}

} // namespace Totem::LedDisplay
