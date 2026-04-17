#pragma once

#include "Macros/Facade.hpp"
#include "Support/Basic.hpp"
#include "Types/Basic.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <functional>
#include <string_view>
#include <strings.h>
#include <type_traits>

template <size_t N> struct NameKey {
    std::array<char, N> name{};
    SmallestUintType<N> len = 0;

    [[nodiscard]] std::string_view view() const {
        return std::string_view{name.data(), len};
    }

    bool operator==(const NameKey &other) const {
        return len == other.len &&
               std::memcmp(name.data(), other.name.data(), len) == 0;
    }

    operator bool() const { return len > 0; }

    static NameKey fromCharPtr(const char *str) {
        if (str == nullptr) [[unlikely]] {
            ABORT("NameKey cannot be created from null pointer");
        }
        size_t len = bounded_strlen(str, N + 1);
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

    static NameKey fromStringView(std::string_view str) {
        if (str.empty()) [[unlikely]] {
            ABORT("NameKey cannot be created from empty string_view");
        }
        if (str.size() > N) [[unlikely]] {
            ABORT("NameKey length overflow: %zu > %zu", str.size(), N);
        }
        NameKey out{};
        std::memcpy(out.name.data(), str.data(), str.size());
        out.len = static_cast<SmallestUintType<N>>(str.size());
        return out;
    }
};

template <typename Key>
[[nodiscard]] inline size_t collection_key_hash(const Key &key) {
    if constexpr (requires {
                      key.name;
                      key.len;
                  }) {
        return hash(key.name.data(), key.len);
    } else if constexpr (std::is_pointer_v<Key>) {
        auto value = reinterpret_cast<uintptr_t>(key);
        return hash(reinterpret_cast<const char *>(&value), sizeof(value));
    } else if constexpr (std::is_enum_v<Key>) {
        auto value = static_cast<std::underlying_type_t<Key>>(key);
        return hash(reinterpret_cast<const char *>(&value), sizeof(value));
    } else if constexpr (std::is_integral_v<Key>) {
        return hash(reinterpret_cast<const char *>(&key), sizeof(key));
    } else {
        static_assert(sizeof(Key) == 0, "Unsupported collection key type");
    }
}

template <typename Key>
[[nodiscard]] inline bool collection_key_equal(const Key &lhs, const Key &rhs) {
    return lhs == rhs;
}

// FIXME: Switch to variadic template for friends when C++26 in Espidf 6.0
template <typename Tag, class KeyT, class Friend> class StrongHandle {
    using KeyType = KeyT;

  public:
    StrongHandle() = delete;
    bool operator==(const StrongHandle &other) const {
        return _key == other._key;
    }

    const KeyType &key() const { return _key; }

  private:
    friend Friend;

    StrongHandle(const KeyType &key) : _key(key) {}

    static StrongHandle make(const KeyType &key) { return StrongHandle{key}; }

    static StrongHandle make(const char *str)
        requires requires { KeyType::fromCharPtr(str); }
    {
        return StrongHandle{KeyType::fromCharPtr(str)};
    }

    KeyType _key;
};

template <typename Key, typename Entry, size_t N> struct SlottedMap {
    static_assert(N > 0, "SlottedMap size N must be greater than 0");
    static_assert(std::is_default_constructible_v<Key>,
                  "SlottedMap keys must be default constructible");

    using SlotIndex = SmallestUintType<N>;

    struct Slot {
        Key key{};
        Entry entry{};
    };

    [[nodiscard]] size_t size() const { return _size; }
    [[nodiscard]] bool empty() const { return _size == 0; }
    [[nodiscard]] bool full() const { return _size >= N; }

    std::expected<std::reference_wrapper<Entry>, ReturnCode>
    get(const Key &key) {
        auto index = _findIndex(key);
        if (index < 0) {
            return std::unexpected(ERR(NotFound));
        }
        return std::ref(_slots[static_cast<size_t>(index)].entry);
    }

    std::expected<std::reference_wrapper<const Entry>, ReturnCode>
    get(const Key &key) const {
        auto index = _findIndex(key);
        if (index < 0) {
            return std::unexpected(ERR(NotFound));
        }
        return std::cref(_slots[static_cast<size_t>(index)].entry);
    }

    [[nodiscard]] bool contains(const Key &key) const {
        return _findIndex(key) >= 0;
    }

    ReturnCode insert(const Key &key, Entry entry) {
        if (_findIndex(key) >= 0) {
            return ERR(AlreadyExists);
        }
        if (_size >= N) {
            return ERR(OutOfMemory);
        }

        auto slotIndex = _size;
        _slots[_size++] = Slot{.key = key, .entry = std::move(entry)};

        auto ret = _indexInsert(key, slotIndex);
        if (!ret.ok()) {
            _slots[--_size] = Slot{};
            return ret;
        }

        return OK();
    }

    ReturnCode remove(const Key &key) {
        auto index = _findIndex(key);
        if (index < 0) {
            return ERR(NotFound);
        }

        auto i = static_cast<SlotIndex>(index);
        auto last = _size - 1;

        auto ret = _indexErase(key);
        if (!ret.ok()) {
            return ret;
        }

        if (i != last) {
            _slots[i] = std::move(_slots[last]);

            ret = _indexErase(_slots[i].key);
            if (!ret.ok()) {
                ABORT("SlottedMap index corruption during remove");
            }

            ret = _indexInsert(_slots[i].key, i);
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

    std::expected<Entry, ReturnCode> extract(const Key &key) {
        auto index = _findIndex(key);
        if (index < 0) {
            return std::unexpected(ERR(NotFound));
        }

        auto i = static_cast<SlotIndex>(index);
        auto last = _size - 1;
        auto ret = std::move(_slots[i].entry);

        auto rc = _indexErase(key);
        if (!rc.ok()) {
            ABORT("SlottedMap index corruption during extract");
        }

        if (i != last) {
            _slots[i] = std::move(_slots[last]);

            rc = _indexErase(_slots[i].key);
            if (!rc.ok()) {
                ABORT("SlottedMap index corruption during extract move");
            }

            rc = _indexInsert(_slots[i].key, i);
            if (!rc.ok()) {
                ABORT("SlottedMap index reinsertion failed during extract");
            }
        }

        _slots[last] = Slot{};
        --_size;
        return ret;
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const Key &, Entry &>)
    ReturnCode forEach(Fn &&fn) {
        for (size_t i = 0; i < _size; ++i) {
            auto ret = std::invoke(fn, _slots[i].key, _slots[i].entry);
            if (!ret.ok()) {
                return ret;
            }
        }
        return OK();
    }

    template <typename Fn>
        requires(
            std::is_invocable_r_v<ReturnCode, Fn, const Key &, const Entry &>)
    ReturnCode forEach(Fn &&fn) const {
        for (size_t i = 0; i < _size; ++i) {
            auto ret = std::invoke(fn, _slots[i].key, _slots[i].entry);
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

    [[nodiscard]] ptrdiff_t _findIndex(const Key &key) const {
        auto start = collection_key_hash(key) % IndexSize;
        for (size_t probe = 0; probe < IndexSize; ++probe) {
            auto pos = (start + probe) % IndexSize;
            auto &indexSlot = _index[pos];

            if (indexSlot.state == IndexState::Empty) {
                return -1;
            }

            if (indexSlot.state == IndexState::Occupied) {
                auto slotIndex = static_cast<size_t>(indexSlot.slotIndex);
                if (collection_key_equal(_slots[slotIndex].key, key)) {
                    return static_cast<ptrdiff_t>(slotIndex);
                }
            }
        }
        return -1;
    }

    ReturnCode _indexInsert(const Key &key, SlotIndex slotIndex) {
        auto start = collection_key_hash(key) % IndexSize;
        ptrdiff_t firstTombstone = -1;

        for (size_t probe = 0; probe < IndexSize; ++probe) {
            auto pos = (start + probe) % IndexSize;
            auto &indexSlot = _index[pos];

            if (indexSlot.state == IndexState::Occupied) {
                auto existingSlotIndex =
                    static_cast<size_t>(indexSlot.slotIndex);
                if (collection_key_equal(_slots[existingSlotIndex].key, key)) {
                    return ERR(AlreadyExists);
                }
                continue;
            }

            if (indexSlot.state == IndexState::Tombstone) {
                if (firstTombstone < 0) {
                    firstTombstone = static_cast<ptrdiff_t>(pos);
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

    ReturnCode _indexErase(const Key &key) {
        auto start = collection_key_hash(key) % IndexSize;
        for (size_t probe = 0; probe < IndexSize; ++probe) {
            auto pos = (start + probe) % IndexSize;
            auto &indexSlot = _index[pos];

            if (indexSlot.state == IndexState::Empty) {
                return ERR(NotFound);
            }
            if (indexSlot.state == IndexState::Occupied) {
                auto existingSlotIndex =
                    static_cast<size_t>(indexSlot.slotIndex);
                if (collection_key_equal(_slots[existingSlotIndex].key, key)) {
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
};
