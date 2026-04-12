#pragma once

#include "Base/HasMutex.hh"
#include "Macros/Facade.hh"
#include "PubSubBackend/Interfaces/Envelope.hh"
#include "PubSubBackend/detail/Codec.hh"
#include "PubSubBackend/detail/Types.hh"
#include "Types/Error.hh"
#include <array>
#include <cstddef>
#include <cstring>
#include <expected>
#include <functional>
#include <optional>
#include <span>

namespace Totem::PubSubBackend::detail {

template <typename T, size_t Capacity>
class Pool : public HasMutex<Pool<T, Capacity>> {
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

    static ReturnCode release(void *owner, const Envelope &req) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->release(req);
    }

    ReturnCode release(const Envelope &req) {
        for (auto &slot : _storage) {
            if (slot.value && slot.messageId == req.header.messageId) {
                slot.value.reset();
                return OK();
            }
        }
        return ERR(NotFound);
    }

    [[nodiscard]] static std::expected<const void *, ReturnCode>
    getPtr(void *owner, const Envelope &req) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->getPtr(req);
    }

    [[nodiscard]] std::expected<const void *, ReturnCode>
    getPtr(const Envelope &req) const {
        for (const auto &slot : _storage) {
            if (slot.value && slot.messageId == req.header.messageId) {
                return static_cast<const void *>(std::addressof(*slot.value));
            }
        }
        return std::unexpected(ERR(NotFound));
    }

    [[nodiscard]] static std::expected<std::reference_wrapper<const T>,
                                       ReturnCode>
    get(void *owner, const Envelope &req) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->get(req);
    }

    [[nodiscard]] std::expected<std::reference_wrapper<const T>, ReturnCode>
    get(const Envelope &req) const {
        for (const auto &slot : _storage) {
            if (slot.value && slot.messageId == req.header.messageId) {
                return std::cref(*slot.value);
            }
        }
        return std::unexpected(ERR(NotFound));
    }

    static ReturnCode encodePayload(void *owner, const Envelope &env,
                                    std::span<std::byte> out) {
        auto *pool = static_cast<Pool *>(owner);
        auto value = pool->get(env);
        if (!value) {
            return value.error();
        }
        return Codec<T>::encode(value->get(), out);
    }

  private:
    void *_pubSubNode;
    std::array<Slot, Capacity> _storage;
    NextMessageIdCallback _nextMessageIdCallback;

    using DefaultError = CoreError;
};

inline constexpr MutexContract<Pool<int, 0>> _pool_mutex_contract;
} // namespace Totem::PubSubBackend::detail
