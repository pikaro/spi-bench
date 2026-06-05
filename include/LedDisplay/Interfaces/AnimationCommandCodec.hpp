#pragma once

#include "LedDisplay/Interfaces/AnimationCommand.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <expected>
#include <span>
#include <tuple>
#include <type_traits>

namespace Totem::LedDisplay {

template <typename Command, typename T>
ReturnCode encodeCommandPayload(Command &cmd, const T &payload);

template <typename T, typename Command>
std::expected<T, ReturnCode> decodeCommandPayload(const Command &cmd);

} // namespace Totem::LedDisplay

#include "PubSubBackend/detail/Codec.hpp"

namespace Totem::LedDisplay {

template <typename Command, typename T>
inline ReturnCode encodeCommandPayload(Command &cmd, const T &payload) {
    constexpr size_t size =
        Totem::PubSubBackend::detail::Codec<T>::encodedSize();
    using Payload =
        std::remove_cvref_t<decltype(std::declval<Command &>().payload)>;
    static_assert(size <= std::tuple_size_v<Payload>,
                  "Animation command payload does not fit wire envelope");
    cmd.payloadSize = static_cast<uint8_t>(size);
    return Totem::PubSubBackend::detail::Codec<T>::encode(
        payload, std::span<std::byte>(cmd.payload).first(size));
}

template <typename T, typename Command>
inline std::expected<T, ReturnCode>
decodeCommandPayload(const Command &cmd) {
    constexpr size_t size =
        Totem::PubSubBackend::detail::Codec<T>::encodedSize();
    FAIL_IF(cmd.payloadSize != size,
            std::unexpected(ERR(CoreError, InvalidSize)),
            "Unexpected animation command payload size");
    return Totem::PubSubBackend::detail::Codec<T>::decode(
        std::span<const std::byte>(cmd.payload).first(size));
}

} // namespace Totem::LedDisplay
