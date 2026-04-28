#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace Totem::Wire::detail {

struct WriteTag {};
struct ReadTag {};

template <typename T> using RequestHandle = uintptr_t;

template <typename Tag> struct Result {
    void *owner;
    RequestHandle<Tag> handle;
    ReturnCode result;
    uint16_t length;
};

template <typename Tag> struct Request {
    void *owner;

    std::span<std::byte> data;

    ReturnCode (*onComplete)(Result<Tag> result);
    ReturnCode (*onResponse)(Result<ReadTag> result);

    [[nodiscard]] constexpr bool validate() const {
        auto valid = (onComplete != nullptr && owner != nullptr &&
                      data.size() <= std::numeric_limits<uint16_t>::max());
        if constexpr (std::same_as<Tag, WriteTag>) {
            valid = valid && onResponse != nullptr;
        } else {
            valid = valid && onResponse == nullptr;
        }
        return valid;
    }

    ReturnCode ack(RequestHandle<Tag> handle, uint16_t written) const {
        return onComplete({
            .owner = owner,
            .handle = handle,
            .result = OK(),
            .length = written,
        });
    }

    ReturnCode nack(RequestHandle<Tag> handle, ReturnCode error) const {
        return onComplete({
            .owner = owner,
            .handle = handle,
            .result = error,
            .length = 0,
        });
    }
};

} // namespace Totem::Wire::detail

namespace Totem::Wire {

using ReadRequestHandle = detail::RequestHandle<detail::ReadTag>;
using WriteRequestHandle = detail::RequestHandle<detail::WriteTag>;

using WriteRequest = detail::Request<detail::WriteTag>;
using ReadRequest = detail::Request<detail::ReadTag>;

using WriteResult = detail::Result<detail::WriteTag>;
using ReadResult = detail::Result<detail::ReadTag>;

} // namespace Totem::Wire
