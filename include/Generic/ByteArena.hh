#pragma once

#include "Base/HasMutex.hh"
#include "Macros/Facade.hh"
#include "Types/Error.hh"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>

template <class Derived, class T, typename Config>
class ByteArena : public HasMutex<ByteArena<Derived, T, Config>> {
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
    ByteArena()
        : _spans{Span{
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

    ReturnCode store(const T &stored, std::span<const std::byte> data) {
        FAIL_IF(data.size() > Config::bufferSize, ERR(InvalidArgument),
                "Payload size exceeds maximum allowed size");
        auto lambda = [this, &stored, &data]() -> ReturnCode {
            FAIL_IF(_hasRecord(stored), ERR(AlreadyExists),
                    "Record already exists for stored item");
            size_t spanIdx;
            FAIL_IF_NOT_OPT(spanIdx, _findFreeSpan(data.size()), ERR(Overflow),
                            "No free span available to store new record");
            auto &span = _spans[spanIdx];
            size_t slotIdx;
            FAIL_IF_NOT_OPT(slotIdx, _findFreeSlot(), ERR(Overflow),
                            "No free slot available to store new record");
            auto &slot = _slots[slotIdx];
            FAIL_IF_ERR_FWD(_storeRecord(spanIdx, slot, span, stored, data),
                            "Failed to store record for stored item");
            slot.timestampMs = ::platform::get_time();
            return OK();
        };
        FAIL_IF_ERR_FWD(
            this->_locked("ByteArena::store", ERR(OperationFailed), lambda),
            "Failed to store record for stored item");
        return OK();
    }

    static ReturnCode getRaw(void *opaque, const T &stored, size_t offset,
                             std::span<std::byte> out) {
        auto *arena = static_cast<ByteArena *>(opaque);
        return arena->getRaw(stored, offset, out);
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

    static ReturnCode release(void *opaque, const T &stored) {
        auto *arena = static_cast<ByteArena *>(opaque);
        return arena->release(stored);
    }

    ReturnCode release(const T &stored) {
        auto lambda = [this, &stored]() -> ReturnCode {
            for (auto &slot : _slots) {
                if (slot.occupied && slot.stored == stored) {
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

    using DefaultError = CoreError;
};
