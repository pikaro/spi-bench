#pragma once

#include "Macros/Facade.hpp"
#include "Mutex/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "PubSubBackend/detail/Codec.hpp"
#include "PubSubBackend/detail/Types.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <optional>
#include <span>

namespace Totem::PubSubBackend::detail {

template <typename T, size_t Capacity>
class Pool {
    struct Slot {
        MessageId messageId{};
        std::optional<T> value;
    };

  public:
    Pool() = default;

    explicit Pool(NextMessageIdCallback nextMessageIdCallback)
        : _nextMessageIdCallback(nextMessageIdCallback) {}

    DELETE_COPY(Pool)
    DELETE_MOVE(Pool)

    static constexpr const char *name = "PubSub::Pool";

    std::expected<MessageId, ReturnCode> store(const T &value) {
        FAIL_IF_NULL(_nextMessageIdCallback,
                     std::unexpected(ERR(InvalidState)),
                     "PubSub pool has no next message ID callback");
        return store(value, _nextMessageIdCallback());
    }

    /**
     * Store a value with a caller-owned message ID.
     *
     * This keeps Pool independent of owners that need contextual message ID
     * sources, such as node-local PubSub control-plane publishers.
     */
    std::expected<MessageId, ReturnCode> store(const T &value,
                                               MessageId messageId) {
        FAIL_IF(messageId == 0, std::unexpected(ERR(InvalidArgument)),
                "Cannot store PubSub pool message with ID 0");
        bool stored = false;
        {
            Mutex::ScopedSpinlockGuard guard{_lock};
            for (auto &slot : _storage) {
                if (!slot.value) {
                    slot.value.emplace(value);
                    slot.messageId = messageId;
                    stored = true;
                    break;
                }
            }
        }
        if (stored) {
            _log_d("%s: stored messageId %u", name, messageId);
            return messageId;
        }
        return std::unexpected(ERR(Overflow));
    }

    static ReturnCode release(void *owner, const Envelope &envelope) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->release(envelope);
    }

    ReturnCode release(const Envelope &envelope) {
        bool released = false;
        {
            Mutex::ScopedSpinlockGuard guard{_lock};
            for (auto &slot : _storage) {
                if (slot.value &&
                    slot.messageId == envelope.header.messageId) {
                    slot.value.reset();
                    released = true;
                    break;
                }
            }
        }
        if (released) {
            _log_d("%s: release messageId %u", name,
                   envelope.header.messageId);
            return OK();
        }
        return ERR(NotFound);
    }

    /**
     * Check whether a message slot is still occupied for the given message ID.
     *
     * This is intended for integration tests that need to verify that the
     * original envelope owner has released its pool entry after transport
     * ownership handoff and fanout complete.
     */
    [[nodiscard]] bool contains(MessageId messageId) const {
        Mutex::ScopedSpinlockGuard guard{_lock};
        for (const auto &slot : _storage) {
            if (slot.value && slot.messageId == messageId) {
                return true;
            }
        }
        return false;
    }

    /**
     * Convenience inverse of contains() for ownership-release checks.
     */
    [[nodiscard]] bool wasFreed(MessageId messageId) const {
        return !contains(messageId);
    }

    [[nodiscard]] static std::expected<const void *, ReturnCode>
    getPtr(void *owner, const Envelope &envelope) {
        auto *pool = static_cast<Pool *>(owner);
        return pool->getPtr(envelope);
    }

    [[nodiscard]] std::expected<const void *, ReturnCode>
    getPtr(const Envelope &envelope) const {
        Mutex::ScopedSpinlockGuard guard{_lock};
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
        Mutex::ScopedSpinlockGuard guard{_lock};
        for (const auto &slot : _storage) {
            if (slot.value && slot.messageId == envelope.header.messageId) {
                return std::cref(*slot.value);
            }
        }
        return std::unexpected(ERR(NotFound));
    }

    static ReturnCode encodePayload(void *owner, const Envelope &env,
                                    std::span<std::byte> out) {
        auto *self = static_cast<Pool *>(owner);
        std::optional<T> value;
        {
            Mutex::ScopedSpinlockGuard guard{self->_lock};
            for (const auto &slot : self->_storage) {
                if (slot.value && slot.messageId == env.header.messageId) {
                    value.emplace(*slot.value);
                    break;
                }
            }
        }
        if (value) {
            return Codec<T>::encode(*value, out);
        }
        return ERR(NotFound);
    }

  private:
    std::array<Slot, Capacity> _storage;
    NextMessageIdCallback _nextMessageIdCallback = nullptr;
    mutable ::platform::Spinlock _lock = ::platform::create_spinlock();
};
} // namespace Totem::PubSubBackend::detail
