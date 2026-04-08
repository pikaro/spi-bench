#pragma once

#include "Base/Namespaces.hh" // IWYU pragma: export
#include "Macros/Facade.hh"
#include "Support/Basic.hh"
#include "Types/Basic.hh"
#include "Types/Error.hh"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <functional>
#include <strings.h>

template <size_t N> struct NameKey {
    std::array<char, N> name{};
    SmallestUintType<N> len = 0;

    bool operator==(const NameKey &other) const {
        return len == other.len &&
               std::memcmp(name.data(), other.name.data(), len) == 0;
    }

    operator bool() const { return len > 0; }

    static NameKey fromCharPtr(const char *str) {
        if (str == nullptr) [[unlikely]] {
            ABORT("NameKey cannot be created from null pointer");
        }
        size_t len = strnlen(str, N + 1);
        if (len == N + 1) [[unlikely]] {
            size_t actualLen = strlen(str);
            ABORT("NameKey length overflow: %s (%zu > %zu)", str, actualLen, N);
        }
        NameKey out{};
        std::memcpy(out.name.data(), str, len);
        ABORT_IF(len == 0, "NameKey cannot be created from empty string");
        out.len = len;
        return out;
    }
};

// FIXME: Switch to variadic template for friends when C++26 in Espidf 6.0
template <typename Tag, size_t N, class Friend> class StrongHandle {
    using KeyType = NameKey<N>;

  public:
    StrongHandle() = delete;
    bool operator==(const StrongHandle &other) const {
        return _key == other._key;
    }

    const KeyType &key() const { return _key; }

  private:
    friend Friend;

    StrongHandle(const KeyType &key) : _key(key) {}

    static StrongHandle make(const char *str) {
        return StrongHandle{NameKey<N>::fromCharPtr(str)};
    }
    static StrongHandle make(const KeyType &key) { return StrongHandle{key}; }

    KeyType _key;
};

template <typename Entry, size_t N, size_t IdLen = 32> struct SlottedMap {
    static_assert(N > 0, "SlottedMap size N must be greater than 0");

    using SlotIndex = SmallestUintType<N>;

    struct Slot {
        NameKey<IdLen> nameKey{};
        Entry entry{};
    };

    [[nodiscard]] size_t size() const { return _size; }
    [[nodiscard]] bool empty() const { return _size == 0; }
    [[nodiscard]] bool full() const { return _size >= N; }

    std::expected<std::reference_wrapper<Entry>, ReturnCode>
    get(const NameKey<IdLen> &nameKey) {
        auto index = _findIndex(nameKey);
        if (index < 0) {
            return std::unexpected(ERR(NotFound));
        }
        return std::ref(_slots[static_cast<size_t>(index)].entry);
    }

    std::expected<std::reference_wrapper<const Entry>, ReturnCode>
    get(const NameKey<IdLen> &nameKey) const {
        auto index = _findIndex(nameKey);
        if (index < 0) {
            return std::unexpected(ERR(NotFound));
        }
        return std::cref(_slots[static_cast<size_t>(index)].entry);
    }

    [[nodiscard]] bool contains(const NameKey<IdLen> &nameKey) const {
        return _findIndex(nameKey) >= 0;
    }

    ReturnCode insert(const NameKey<IdLen> &nameKey, Entry entry) {
        if (_findIndex(nameKey) >= 0) {
            return ERR(AlreadyExists);
        }
        if (_size >= N) {
            return ERR(OutOfMemory);
        }

        auto slotIndex = _size;
        _slots[_size++] = Slot{.nameKey = nameKey, .entry = std::move(entry)};

        auto ret = _indexInsert(nameKey, slotIndex);
        if (!ret.ok()) {
            _slots[--_size] = Slot{};
            return ret;
        }

        return OK();
    }

    ReturnCode remove(const NameKey<IdLen> &nameKey) {
        auto index = _findIndex(nameKey);
        if (index < 0) {
            return ERR(NotFound);
        }

        auto i = static_cast<SlotIndex>(index);
        auto last = _size - 1;

        auto ret = _indexErase(nameKey);
        if (!ret.ok()) {
            return ret;
        }

        if (i != last) {
            _slots[i] = std::move(_slots[last]);

            ret = _indexErase(_slots[i].nameKey);
            if (!ret.ok()) {
                ABORT("SlottedMap index corruption during remove");
            }

            ret = _indexInsert(_slots[i].nameKey, i);
            if (!ret.ok()) {
                ABORT("SlottedMap index reinsertion failed during remove");
            }
        }

        _slots[last] = Slot{};
        --_size;
        return OK();
    }

    ReturnCode clear() {
        for (size_t i = 0; i < _size; ++i) {
            _slots[i] = Slot{};
        }
        for (size_t i = 0; i < IndexSize; ++i) {
            _index[i] = IndexSlot{};
        }
        _size = 0;
        return OK();
    }

    std::expected<Entry, ReturnCode> extract(const NameKey<IdLen> &nameKey) {
        auto index = _findIndex(nameKey);
        if (index < 0) {
            return std::unexpected(ERR(NotFound));
        }

        auto i = static_cast<SlotIndex>(index);
        auto last = _size - 1;
        auto ret = std::move(_slots[i].entry);

        auto rc = _indexErase(nameKey);
        if (!rc.ok()) {
            ABORT("SlottedMap index corruption during extract");
        }

        if (i != last) {
            _slots[i] = std::move(_slots[last]);

            rc = _indexErase(_slots[i].nameKey);
            if (!rc.ok()) {
                ABORT("SlottedMap index corruption during extract move");
            }

            rc = _indexInsert(_slots[i].nameKey, i);
            if (!rc.ok()) {
                ABORT("SlottedMap index reinsertion failed during extract");
            }
        }

        _slots[last] = Slot{};
        --_size;
        return ret;
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const NameKey<IdLen> &,
                                       Entry &>)
    ReturnCode forEach(Fn &&fn) {
        for (size_t i = 0; i < _size; ++i) {
            auto ret = std::invoke(fn, _slots[i].nameKey, _slots[i].entry);
            if (!ret.ok()) {
                return ret;
            }
        }
        return OK();
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const NameKey<IdLen> &,
                                       const Entry &>)
    ReturnCode forEach(Fn &&fn) const {
        for (size_t i = 0; i < _size; ++i) {
            auto ret = std::invoke(fn, _slots[i].nameKey, _slots[i].entry);
            if (!ret.ok()) {
                return ret;
            }
        }
        return OK();
    }

  private:
    enum class IndexState : uint8_t {
        Empty,
        Occupied,
        Tombstone,
    };

    struct IndexSlot {
        IndexState state = IndexState::Empty;
        SlotIndex slotIndex = 0;
    };

    // TODO: Benchmark load factor
    static constexpr size_t IndexSize = (N * 2) + 1;

    [[nodiscard]] ssize_t _findIndex(const NameKey<IdLen> &nameKey) const {
        auto start = hash(nameKey.name.data(), nameKey.len) % IndexSize;
        for (size_t probe = 0; probe < IndexSize; ++probe) {
            auto pos = (start + probe) % IndexSize;
            auto &indexSlot = _index[pos];

            if (indexSlot.state == IndexState::Empty) {
                return -1;
            }

            if (indexSlot.state == IndexState::Occupied) {
                auto slotIndex = static_cast<size_t>(indexSlot.slotIndex);
                if (_slots[slotIndex].nameKey == nameKey) {
                    return static_cast<ssize_t>(slotIndex);
                }
            }
        }
        return -1;
    }

    ReturnCode _indexInsert(const NameKey<IdLen> &nameKey,
                            SlotIndex slotIndex) {
        auto start = hash(nameKey.name.data(), nameKey.len) % IndexSize;
        ssize_t firstTombstone = -1;

        for (size_t probe = 0; probe < IndexSize; ++probe) {
            auto pos = (start + probe) % IndexSize;
            auto &indexSlot = _index[pos];

            if (indexSlot.state == IndexState::Occupied) {
                auto existingSlotIndex =
                    static_cast<size_t>(indexSlot.slotIndex);
                if (_slots[existingSlotIndex].nameKey == nameKey) {
                    return ERR(AlreadyExists);
                }
                continue;
            }

            if (indexSlot.state == IndexState::Tombstone) {
                if (firstTombstone < 0) {
                    firstTombstone = static_cast<ssize_t>(pos);
                }
                continue;
            }

            auto insertPos =
                firstTombstone >= 0 ? static_cast<size_t>(firstTombstone) : pos;
            _index[insertPos] = IndexSlot{
                .state = IndexState::Occupied,
                .slotIndex = slotIndex,
            };
            return OK();
        }

        if (firstTombstone >= 0) {
            auto insertPos = static_cast<size_t>(firstTombstone);
            _index[insertPos] = IndexSlot{
                .state = IndexState::Occupied,
                .slotIndex = slotIndex,
            };
            return OK();
        }

        return ERR(OutOfMemory);
    }

    ReturnCode _indexErase(const NameKey<IdLen> &nameKey) {
        auto start = hash(nameKey.name.data(), nameKey.len) % IndexSize;
        for (size_t probe = 0; probe < IndexSize; ++probe) {
            auto pos = (start + probe) % IndexSize;
            auto &indexSlot = _index[pos];

            if (indexSlot.state == IndexState::Empty) {
                return ERR(NotFound);
            }
            if (indexSlot.state == IndexState::Occupied) {
                auto existingSlotIndex =
                    static_cast<size_t>(indexSlot.slotIndex);
                if (_slots[existingSlotIndex].nameKey == nameKey) {
                    indexSlot = IndexSlot{.state = IndexState::Tombstone};
                    return OK();
                }
            }
        }
        return ERR(NotFound);
    }

    std::array<Slot, N> _slots{};
    std::array<IndexSlot, IndexSize> _index{};
    SlotIndex _size = 0;

    using DefaultError = CoreError;
};
