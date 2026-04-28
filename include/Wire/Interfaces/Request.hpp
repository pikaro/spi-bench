#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

namespace Totem::Wire::detail {

struct WriteTag {};
struct ReadTag {};
struct ExchangeTag {};

template <typename T> using RequestHandle = uintptr_t;

enum class PayloadType : uint8_t {
    Raw = 0,
    PubSub = 1,
    Clock = 2,
};

template <typename Tag> struct Result {
    void *owner;
    RequestHandle<Tag> handle;
    ReturnCode result;
    uint16_t length;
    int64_t completedAtUs = 0;
};

struct WriteRequest {
    void *owner;
    PayloadType payloadType = PayloadType::Raw;
    std::span<const std::byte> data;
    ReturnCode (*onComplete)(Result<WriteTag> result);

    [[nodiscard]] constexpr bool validate() const {
        return onComplete != nullptr && owner != nullptr &&
               data.size() <= std::numeric_limits<uint16_t>::max();
    }

    ReturnCode ack(RequestHandle<WriteTag> handle, uint16_t written,
                   int64_t completedAtUs = 0) const {
        return onComplete({
            .owner = owner,
            .handle = handle,
            .result = OK(),
            .length = written,
            .completedAtUs = completedAtUs,
        });
    }

    ReturnCode nack(RequestHandle<WriteTag> handle, ReturnCode error) const {
        return onComplete({
            .owner = owner,
            .handle = handle,
            .result = error,
            .length = 0,
        });
    }
};

struct ReadRequest {
    void *owner;
    PayloadType payloadType = PayloadType::Raw;
    std::span<std::byte> data;
    ReturnCode (*onComplete)(Result<ReadTag> result);

    [[nodiscard]] constexpr bool validate() const {
        return onComplete != nullptr && owner != nullptr &&
               data.size() <= std::numeric_limits<uint16_t>::max();
    }

    ReturnCode ack(RequestHandle<ReadTag> handle, uint16_t read,
                   int64_t completedAtUs = 0) const {
        return onComplete({
            .owner = owner,
            .handle = handle,
            .result = OK(),
            .length = read,
            .completedAtUs = completedAtUs,
        });
    }

    ReturnCode nack(RequestHandle<ReadTag> handle, ReturnCode error) const {
        return onComplete({
            .owner = owner,
            .handle = handle,
            .result = error,
            .length = 0,
        });
    }
};

struct ExchangeRequest {
    void *owner;
    PayloadType payloadType = PayloadType::Raw;
    std::span<const std::byte> request;
    std::span<std::byte> response;
    ReturnCode (*onComplete)(Result<ExchangeTag> result);

    [[nodiscard]] constexpr bool validate() const {
        return onComplete != nullptr && owner != nullptr &&
               request.size() <= std::numeric_limits<uint16_t>::max() &&
               response.size() <= std::numeric_limits<uint16_t>::max();
    }

    ReturnCode ack(RequestHandle<ExchangeTag> handle, uint16_t responseLength,
                   int64_t completedAtUs = 0) const {
        return onComplete({
            .owner = owner,
            .handle = handle,
            .result = OK(),
            .length = responseLength,
            .completedAtUs = completedAtUs,
        });
    }

    ReturnCode nack(RequestHandle<ExchangeTag> handle, ReturnCode error) const {
        return onComplete({
            .owner = owner,
            .handle = handle,
            .result = error,
            .length = 0,
        });
    }
};

struct FrameHandler {
    void *owner = nullptr;
    PayloadType payloadType = PayloadType::Raw;
    std::span<std::byte> response;
    ReturnCode (*onData)(void *owner, PayloadType payloadType,
                         std::span<const std::byte> payload,
                         int64_t receivedAtUs) = nullptr;
    std::expected<uint16_t, ReturnCode> (*onRequest)(
        void *owner, PayloadType payloadType,
        std::span<const std::byte> request, std::span<std::byte> response,
        int64_t receivedAtUs) = nullptr;

    [[nodiscard]] constexpr bool validate() const {
        return owner != nullptr &&
               (onData != nullptr || onRequest != nullptr) &&
               response.size() <= std::numeric_limits<uint16_t>::max();
    }
};

} // namespace Totem::Wire::detail

namespace Totem::Wire {

using ReadRequestHandle = detail::RequestHandle<detail::ReadTag>;
using WriteRequestHandle = detail::RequestHandle<detail::WriteTag>;
using ExchangeRequestHandle = detail::RequestHandle<detail::ExchangeTag>;

using PayloadType = detail::PayloadType;
using WriteRequest = detail::WriteRequest;
using ReadRequest = detail::ReadRequest;
using ExchangeRequest = detail::ExchangeRequest;
using FrameHandler = detail::FrameHandler;

using WriteResult = detail::Result<detail::WriteTag>;
using ReadResult = detail::Result<detail::ReadTag>;
using ExchangeResult = detail::Result<detail::ExchangeTag>;

} // namespace Totem::Wire
