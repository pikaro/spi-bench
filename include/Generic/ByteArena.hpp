#pragma once

#include "Base/HasMutex.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <optional>
#include <span>

template <class Derived, class T, typename Config>
class ByteArena : public HasMutex<Derived> {
    struct Slot {
        uint32_t timestampMs;
        T stored;
        size_t offset;
        bool occupied;
    };

    struct Span {
        size_t offset;
        size_t size;
        bool occupied;
    };

  public:
    using StoreResult = std::expected<bool, ReturnCode>;

    explicit ByteArena(LogComponent logComponent)
        : logComponent(logComponent),
          _spans{Span{
              .offset = 0, .size = Config::bufferSize, .occupied = false}} {}

    ReturnCode clean() {
        if (Config::maxRecordAgeMs == 0) {
            return OK();
        }
        auto cutoff = ::platform::get_time() - Config::maxRecordAgeMs;
        auto lambda = [this, cutoff]() -> ReturnCode {
            for (auto &slot : _slots) {
                if (slot.occupied && slot.timestampMs < cutoff) {
                    _log_w(
                        "Cleaning up expired item from ByteArena at offset %zu",
                        slot.offset);
                    FAIL_IF_ERR_FWD(_releaseSlot(slot),
                                    "Failed to release expired item from "
                                    "ByteArena at offset %zu",
                                    slot.offset);
                }
            }
            return OK();
        };
        FAIL_IF_ERR_FWD(
            this->_locked("ByteArena::clean", ERR(OperationFailed), lambda),
            "Failed to clean buffer");
        return OK();
    }

    StoreResult store(const T &stored, std::span<const std::byte> data) {
        FAIL_IF(data.size() > Config::bufferSize,
                std::unexpected(ERR(InvalidArgument)),
                "Payload size exceeds maximum allowed size");
        bool wasStored = false;
        auto lambda = [this, &stored, &data, &wasStored]() -> ReturnCode {
            FAIL_IF(_hasRecord(stored), ERR(AlreadyExists),
                    "Record already exists for stored item");
            while (true) {
                auto spanIdx = _findFreeSpan(data.size());
                auto slotIdx = _findFreeSlot();
                if (spanIdx.has_value() && slotIdx.has_value()) {
                    auto &span = _spans[*spanIdx];
                    auto &slot = _slots[*slotIdx];
                    auto storeRet =
                        _storeRecord(*spanIdx, slot, span, stored, data);
                    if (storeRet.ok()) {
                        slot.timestampMs = ::platform::get_time();
                        _log_d("%s: stored %zu bytes at offset %zu",
                               Derived::name, data.size(), slot.offset);
                        wasStored = true;
                        return OK();
                    }
                    FAIL_IF(storeRet != ERR(Overflow), storeRet,
                            "Failed to store record for stored item");
                }

                if (!_isCritical(stored)) {
                    _log_w("%s: dropping noncritical record under arena "
                           "pressure",
                           Derived::name);
                    FAIL_IF_ERR_FWD(_onDropNoncritical(stored),
                                    "Failed to record noncritical arena drop");
                    wasStored = false;
                    return OK();
                }

                auto evictIdx = _findOldestDroppableSlot();
                if (!evictIdx.has_value()) {
                    _log_w("%s: rejecting critical record because no "
                           "noncritical arena record can be evicted",
                           Derived::name);
                    FAIL_IF_ERR_FWD(_onRejectCritical(stored),
                                    "Failed to record critical arena "
                                    "rejection");
                    FAIL(ERR(Overflow), "No space available for critical "
                                        "record");
                }

                const auto evicted = _slots[*evictIdx].stored;
                _log_w("%s: evicting oldest noncritical record at offset %zu "
                       "to admit critical record",
                       Derived::name, _slots[*evictIdx].offset);
                FAIL_IF_ERR_FWD(_releaseSlot(_slots[*evictIdx]),
                                "Failed to evict noncritical record for "
                                "critical store");
                FAIL_IF_ERR_FWD(_onEvictNoncritical(evicted),
                                "Failed to record noncritical arena "
                                "eviction");
            }
        };
        FAIL_IF_ERR_FWD_UNEXPECTED(
            this->_locked("ByteArena::store", ERR(OperationFailed), lambda),
            "Failed to store record for stored item");
        return wasStored;
    }

    static ReturnCode getRaw(void *arena, const T &stored, size_t offset,
                             std::span<std::byte> out) {
        auto *self = static_cast<ByteArena *>(arena);
        return self->getRaw(stored, offset, out);
    }

    ReturnCode getRaw(const T &stored, size_t offset,
                      std::span<std::byte> out) const {
        auto lambda = [this, &stored, &out, offset]() -> ReturnCode {
            for (const auto &slot : _slots) {
                if (slot.occupied && slot.stored == stored) {
                    std::memcpy(out.data(),
                                _buffer.data() + slot.offset + offset,
                                out.size());
                    return OK();
                }
            }
            return ERR(NotFound);
        };
        FAIL_IF_ERR_FWD(this->_lockedConst("ByteArena::getRaw",
                                           ERR(OperationFailed), lambda),
                        "Failed to get raw payload for stored item");
        return OK();
    }

    static ReturnCode release(void *arena, const T &stored) {
        auto *self = static_cast<ByteArena *>(arena);
        return self->release(stored);
    }

    ReturnCode release(const T &stored) {
        auto lambda = [this, &stored]() -> ReturnCode {
            for (auto &slot : _slots) {
                if (slot.occupied && slot.stored == stored) {
                    _log_d("%s: releasing record at offset %zu", Derived::name,
                           slot.offset);
                    return _releaseSlot(slot);
                }
            }
            return ERR(NotFound);
        };
        FAIL_IF_ERR_FWD(
            this->_locked("ByteArena::release", ERR(OperationFailed), lambda),
            "Failed to release record for stored item");
        return OK();
    }

    /**
     * Check whether a record for the given key is currently retained.
     *
     * Unlike getRaw(), this is a pure existence query and therefore does not
     * log a NotFound error when the record has already been released.
     */
    [[nodiscard]] bool contains(const T &stored) const {
        bool found = false;
        auto lambda = [this, &stored, &found]() -> ReturnCode {
            found = _hasRecord(stored);
            return OK();
        };
        auto ret =
            this->_lockedConst("ByteArena::contains", ERR(OperationFailed),
                               lambda);
        if (!ret.ok()) {
            _log_e("Failed to check ByteArena record existence: " ERR_FMT,
                   ERR_ARG(ret));
            return false;
        }
        return found;
    }

  protected:
    LogComponent logComponent;

  private:
    [[nodiscard]] bool _hasRecord(const T &stored) const {
        return std::ranges::any_of(_slots, [&stored](const auto &slot) {
            return slot.occupied && slot.stored == stored;
        });
    }

    [[nodiscard]] std::optional<size_t> _findFreeSlot() const {
        for (size_t i = 0; i < _slots.size(); ++i) {
            if (!_slots[i].occupied) {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<size_t> _findFreeSpan(size_t size) {
        std::optional<size_t> idx;

        for (size_t i = 0; i < _spanCount; ++i) {
            const auto &span = _spans[i];
            if (span.occupied || span.size < size) {
                continue;
            }
            if (span.size == size) {
                idx.emplace(i);
                break;
            }
            if (idx.has_value()) {
                if (span.size >= _spans[*idx].size) {
                    continue;
                }
            }
            idx.emplace(i);
        }

        return idx;
    }

    [[nodiscard]] std::optional<size_t> _findOldestDroppableSlot() const {
        std::optional<size_t> idx;
        for (size_t i = 0; i < _slots.size(); ++i) {
            const auto &slot = _slots[i];
            if (!slot.occupied || _isCritical(slot.stored)) {
                continue;
            }
            if (!idx.has_value() ||
                slot.timestampMs < _slots[*idx].timestampMs) {
                idx.emplace(i);
            }
        }
        return idx;
    }

    [[nodiscard]] static bool _isCritical(const T &stored) {
        if constexpr (requires { Config::isCritical(stored); }) {
            return Config::isCritical(stored);
        }
        return true;
    }

    static ReturnCode _onEvictNoncritical(const T &stored) {
        if constexpr (requires { Config::onEvictNoncritical(stored); }) {
            return Config::onEvictNoncritical(stored);
        }
        return OK();
    }

    static ReturnCode _onDropNoncritical(const T &stored) {
        if constexpr (requires { Config::onDropNoncritical(stored); }) {
            return Config::onDropNoncritical(stored);
        }
        return OK();
    }

    static ReturnCode _onRejectCritical(const T &stored) {
        if constexpr (requires { Config::onRejectCritical(stored); }) {
            return Config::onRejectCritical(stored);
        }
        return OK();
    }

    ReturnCode _releaseSlot(Slot &slot) {
        if (!slot.occupied) {
            return ERR(InvalidState);
        }
        for (size_t i = 0; i < _spanCount; ++i) {
            auto &span = _spans[i];
            if (span.offset == slot.offset) {
                FAIL_IF(!span.occupied, ERR(InvalidState),
                        "Span already unoccupied for occupied slot");
                FAIL_IF_ERR_FWD(_releaseSpan(i, span),
                                "Failed to release span for slot");
                slot.occupied = false;
                return OK();
            }
        }
        return ERR(NotFound);
    }

    ReturnCode _releaseSpan(size_t idx, Span &span) {
        if (!span.occupied) {
            return ERR(InvalidState);
        }
        span.occupied = false;
        if (idx + 1 < _spanCount && !_spans[idx + 1].occupied) {
            span.size += _spans[idx + 1].size;
            for (size_t i = idx + 1; i < _spanCount - 1; ++i) {
                _spans[i] = _spans[i + 1];
            }
            _spanCount--;
        }
        if (idx > 0 && !_spans[idx - 1].occupied) {
            _spans[idx - 1].size += span.size;
            for (size_t i = idx; i < _spanCount - 1; ++i) {
                _spans[i] = _spans[i + 1];
            }
            _spanCount--;
        }
        return OK();
    }

    ReturnCode _storeRecord(size_t idx, Slot &slot, Span &span, const T &stored,
                            std::span<const std::byte> payload) {
        bool needsSplit = span.size > payload.size();
        FAIL_IF(needsSplit && _spanCount >= Config::spanCount, ERR(Overflow),
                "No more spans available to split for new record");
        FAIL_IF(slot.occupied, ERR(InvalidState), "Slot is already occupied");
        FAIL_IF(span.occupied, ERR(InvalidState), "Span is already occupied");
        FAIL_IF(span.size < payload.size(), ERR(InvalidState),
                "Span size is smaller than payload size for new record");
        std::memcpy(_buffer.data() + span.offset, payload.data(),
                    payload.size());
        if (needsSplit) {
            for (size_t i = _spanCount; i > idx + 1; --i) {
                _spans[i] = _spans[i - 1];
            }
            _spans[idx + 1] = Span{.offset = span.offset + payload.size(),
                                   .size = span.size - payload.size(),
                                   .occupied = false};
            span.size = payload.size();
            span.occupied = true;
            _spanCount++;
        } else {
            span.occupied = true;
        }
        slot.stored = stored;
        slot.offset = span.offset;
        slot.occupied = true;
        return OK();
    }

    std::array<std::byte, Config::bufferSize> _buffer;
    std::array<Slot, Config::slotCount> _slots;
    std::array<Span, Config::spanCount> _spans;
    size_t _spanCount = 1;
};
