#pragma once

#include "Base/Namespaces.hh" // IWYU pragma: export
#include "Macros/Facade.hh"
#include "Types/Error.hh"
#include <array>
#include <cstdio>
#include <cstring>
#include <expected>
#include <functional>

template <size_t N> struct NameKey {
    size_t len{};
    std::array<char, N> name{};

    bool operator==(const NameKey &other) const {
        return len == other.len &&
               std::memcmp(name.data(), other.name.data(), len) == 0;
    }

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
        out.len = len;
        return out;
    }
};

template <typename Entry, size_t N, size_t IdLen = 32> struct SlottedMap {
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
        _slots[_size++] = Slot{.nameKey = nameKey, .entry = std::move(entry)};
        return OK();
    }

    ReturnCode remove(const NameKey<IdLen> &nameKey) {
        auto index = _findIndex(nameKey);
        if (index < 0) {
            return ERR(NotFound);
        }
        auto i = static_cast<size_t>(index);
        if (i != _size - 1) {
            _slots[i] = std::move(_slots[_size - 1]);
        }
        _slots[_size - 1] = Slot{};
        --_size;
        return OK();
    }

    ReturnCode clear() {
        for (size_t i = 0; i < _size; ++i) {
            _slots[i] = Slot{};
        }
        _size = 0;
        return OK();
    }

    std::expected<Entry, ReturnCode> extract(const NameKey<IdLen> &nameKey) {
        auto index = _findIndex(nameKey);
        if (index < 0) {
            return std::unexpected(ERR(NotFound));
        }
        auto i = static_cast<size_t>(index);
        auto ret = std::move(_slots[i].entry);
        if (i != _size - 1) {
            _slots[i] = std::move(_slots[_size - 1]);
        }
        _slots[_size - 1] = Slot{};
        --_size;
        return std::move(ret);
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
    [[nodiscard]] ssize_t _findIndex(const NameKey<IdLen> &nameKey) const {
        for (size_t i = 0; i < _size; ++i) {
            if (_slots[i].nameKey == nameKey) {
                return static_cast<ssize_t>(i);
            }
        }
        return -1;
    }

    std::array<Slot, N> _slots{};
    size_t _size = 0;

    using DefaultError = CoreError;
};
