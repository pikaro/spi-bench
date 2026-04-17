#pragma once

#include "Base/HasMutex.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/Codec.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
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
                _log_d("%s: stored messageId %u", name, slot.messageId);
                return slot.messageId;
            }
        }

        return std::unexpected(ERR(Overflow));
    }

    static ReturnCode release(void *owner, const Envelope &envelope) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->release(envelope);
    }

    ReturnCode release(const Envelope &envelope) {
        for (auto &slot : _storage) {
            if (slot.value && slot.messageId == envelope.header.messageId) {
                _log_d("%s: release messageId %u", name,
                       envelope.header.messageId);
                slot.value.reset();
                return OK();
            }
        }
        return ERR(NotFound);
    }

    [[nodiscard]] static std::expected<const void *, ReturnCode>
    getPtr(void *owner, const Envelope &envelope) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->getPtr(envelope);
    }

    [[nodiscard]] std::expected<const void *, ReturnCode>
    getPtr(const Envelope &envelope) const {
        for (const auto &slot : _storage) {
            if (slot.value && slot.messageId == envelope.header.messageId) {
                return static_cast<const void *>(std::addressof(*slot.value));
            }
        }
        return std::unexpected(ERR(NotFound));
    }

    [[nodiscard]] static std::expected<std::reference_wrapper<const T>,
                                       ReturnCode>
    get(void *owner, const Envelope &envelope) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->get(envelope);
    }

    [[nodiscard]] std::expected<std::reference_wrapper<const T>, ReturnCode>
    get(const Envelope &envelope) const {
        for (const auto &slot : _storage) {
            if (slot.value && slot.messageId == envelope.header.messageId) {
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
};

inline constexpr MutexContract<Pool<int, 0>> _pool_mutex_contract;
} // namespace Totem::PubSubBackend::detail
