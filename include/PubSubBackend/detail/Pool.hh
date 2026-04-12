#pragma once

#include "Base/HasMutex.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Frame.hh"
#include "PubSubBackend/detail/Codec.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include <array>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <expected>
#include <functional>
#include <optional>
#include <span>

namespace Totem::PubSubBackend::detail {

template <class C, class T>
concept CodecLike = requires(std::span<const std::byte> bytes,
                             std::span<std::byte> out, const T &value) {
    { C::decode(bytes) } -> std::same_as<std::expected<T, ReturnCode>>;
    { C::encode(value, out) } -> std::same_as<ReturnCode>;
};

template <typename T, size_t Capacity, typename CodecT = Codec<T>>
    requires CodecLike<CodecT, T>
class Pool : public HasMutex<Pool<T, Capacity, CodecT>> {
    struct Slot {
        MessageId messageId{};
        std::optional<T> value;
    };

  public:
    explicit Pool(void *pubSubNode, NextMessageIdCallback nextMessageIdCallback)
        : _pubSubNode(pubSubNode),
          _nextMessageIdCallback(nextMessageIdCallback) {}

    DELETE_COPY(Pool)
    DELETE_MOVE(Pool)

    static constexpr const char *name = "PubSub::Pool";

    std::expected<MessageId, ReturnCode> store(const T &value) {
        for (auto &slot : _storage) {
            if (!slot.value) {
                slot.value.emplace(value);
                slot.messageId = _nextMessageIdCallback(_pubSubNode);
                return slot.messageId;
            }
        }

        return std::unexpected(ERR(Overflow));
    }

    static ReturnCode release(void *owner, const PublishRequest &req) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->release(req);
    }

    ReturnCode release(const PublishRequest &req) {
        for (auto &slot : _storage) {
            if (slot.value && slot.messageId == req.messageId) {
                slot.value.reset();
                return OK();
            }
        }
        return ERR(NotFound);
    }

    [[nodiscard]] static std::expected<std::reference_wrapper<const T>,
                                       ReturnCode>
    get(void *owner, const PublishRequest &req) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->get(req);
    }

    [[nodiscard]] std::expected<std::reference_wrapper<const T>, ReturnCode>
    get(const PublishRequest &req) const {
        for (const auto &slot : _storage) {
            if (slot.value && slot.messageId == req.messageId) {
                return std::cref(*slot.value);
            }
        }
        return std::unexpected(ERR(NotFound));
    }

    [[nodiscard]] static ReturnCode
    getRaw(void *owner, const PublishRequest &req, std::span<std::byte> out) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->getRaw(req, out);
    }

    [[nodiscard]] ReturnCode getRaw(const PublishRequest &req,
                                    std::span<std::byte> out) const {
        auto valueResult = get(req);
        FAIL_IF_UNEXPECTED_FWD(value, valueResult,
                               "Failed to get value for "
                               "messageId %u",
                               req.messageId);
        auto encodeResult = CodecT::encode(value.get(), out);
        FAIL_IF_UNEXPECTED_FWD(encoded, encodeResult,
                               "Failed to encode value for messageId %u: %s",
                               req.messageId, encodeResult.error().format());
        return OK();
    }

  private:
    void *_pubSubNode;
    std::array<Slot, Capacity> _storage;
    NextMessageIdCallback _nextMessageIdCallback;

    using DefaultError = CoreError;
};

inline constexpr MutexContract<Pool<int, 0>> _pool_mutex_contract;

} // namespace Totem::PubSubBackend::detail
