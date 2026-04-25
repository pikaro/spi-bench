#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasMutex.hpp"
#include "Macros/Facade.hpp"
#include "Types/Collection.hpp"
#include "Types/Error.hpp"
#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>
#include <type_traits>

namespace Totem::Generic::detail {

inline std::string_view directory_string_view_or_empty(std::string_view value) {
    return value;
}

inline std::string_view directory_string_view_or_empty(const char *value) {
    return value != nullptr ? std::string_view{value} : std::string_view{};
}

template <typename Key, typename Entry> struct DirectoryRepresentation {
    static std::string_view keyName(const Key &key) {
        if constexpr (requires {
                          {
                              key.view()
                          } -> std::convertible_to<std::string_view>;
                      }) {
            return key.view();
        } else {
            return "<entry>";
        }
    }

    static std::string_view entryName(const Key &key, const Entry &entry) {
        if constexpr (requires {
                          {
                              entry.displayName()
                          } -> std::convertible_to<std::string_view>;
                      }) {
            auto name = entry.displayName();
            if (!name.empty()) {
                return name;
            }
        }

        if constexpr (requires { entry.identity; }) {
            if (entry.identity != nullptr && !entry.identity->name.empty()) {
                return entry.identity->name;
            }
        }

        if constexpr (requires { entry.displayName; }) {
            auto name = directory_string_view_or_empty(entry.displayName);
            if (!name.empty()) {
                return name;
            }
        }

        if constexpr (requires { entry.name; }) {
            auto name = directory_string_view_or_empty(entry.name);
            if (!name.empty()) {
                return name;
            }
        }

        return keyName(key);
    }
};

template <class T>
concept HasBeforeRemove = requires(T &cls, std::string_view name,
                                   const typename T::EntryType &entry) {
    { cls.beforeRemove(name, entry) } -> std::same_as<ReturnCode>;
};

template <class T>
concept HasBeforeAdd = requires(T &cls, std::string_view name,
                                const typename T::EntryType &entry) {
    { cls.beforeAdd(name, entry) } -> std::same_as<ReturnCode>;
};

} // namespace Totem::Generic::detail

template <class Derived, typename Key, typename Entry, size_t N,
          class Representation =
              Totem::Generic::detail::DirectoryRepresentation<Key, Entry>>
class BaseDirectory
    : HasMutex<BaseDirectory<Derived, Key, Entry, N, Representation>> {
    static_assert(std::is_nothrow_move_constructible_v<Entry>,
                  "Directory entries must be nothrow move constructible");
    static_assert(std::is_nothrow_move_assignable_v<Entry>,
                  "Directory entries must be nothrow move assignable");
    static_assert(std::is_default_constructible_v<Entry>,
                  "Directory entries must be default constructible");
    static_assert(std::movable<Entry>, "Directory entries must be movable");
    static_assert(N > 0, "Directory size N must be greater than 0");

  public:
    static constexpr const char *name = "Directory";

    using EntryKey = Key;

    using EntryKeyArray = std::array<EntryKey, N>;
    using EntryArray = std::array<Entry, N>;

    struct EntryKeySnapshot {
        EntryKeyArray keys;
        size_t count = 0;
    };

    struct EntryExtract {
        EntryArray entries;
        size_t count = 0;
    };

    struct MinMax {
        size_t min = 0;
        size_t max = N;
    };

    using SlottedMapType = SlottedMap<EntryKey, Entry, N>;

    explicit BaseDirectory(const char *ownerName, LogComponent logComponent)
        : _ownerName(ownerName), logComponent(logComponent) {}

    [[nodiscard]] const char *ownerName() const { return _ownerName; }

    void disableRegistration() {
        _log_i("Disabling directory registration for %s", _ownerName);
        _permitRegistration.store(false);
    }

    void enableRegistration() {
        _log_i("Enabling directory registration for %s", _ownerName);
        _permitRegistration.store(true);
    }

    template <typename Fn>
    ReturnCode withEntry(const char *entryName, Fn &&fn)
        requires requires { EntryKey::fromCharPtr(entryName); }
    {
        FAIL_IF_NULL(entryName, ERR(InvalidArgument),
                     "Entry name for %s cannot be null", _ownerName);
        return withEntry(EntryKey::fromCharPtr(entryName),
                         std::forward<Fn>(fn));
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, Entry &>)
    ReturnCode withEntry(const EntryKey &entryKey, Fn &&fn) {
        auto keyCref = std::cref(entryKey);
        auto lambda = [this](const EntryKey &key_, auto &&fn_) -> ReturnCode {
            return _withEntryImpl(
                _entries, key_, std::forward<decltype(fn_)>(fn_), logComponent);
        };
        auto fnRef = std::ref(fn);
        return this->_locked("Directory::withEntry", ERR(Timeout), lambda,
                             keyCref, fnRef);
    }

    template <typename Fn>
        requires(
            std::is_invocable_r_v<ReturnCode, Fn, const EntryKey &, Entry &>)
    ReturnCode withAll(Fn &&fn) {
        return withAll(std::forward<Fn>(fn),
                       [](const EntryKey &, const Entry &) { return true; });
    }

    template <typename Fn, typename Filter>
        requires(
            std::is_invocable_r_v<ReturnCode, Fn, const EntryKey &, Entry &>)
    ReturnCode withAll(Fn &&fn, Filter &&filter) {
        auto lambda = [this](auto &&fn_, auto &&filter_) -> ReturnCode {
            return _withAllImpl(_entries, std::forward<decltype(fn_)>(fn_),
                                filter_, logComponent);
        };
        auto fnRef = std::ref(fn);
        auto filterRef = std::ref(filter);
        return this->_locked("Directory::withAll", ERR(Timeout), lambda, fnRef,
                             filterRef);
    }

    template <typename Fn>
    ReturnCode withEntryConst(const char *entryName, Fn &&fn) const
        requires requires { EntryKey::fromCharPtr(entryName); }
    {
        FAIL_IF_NULL(entryName, ERR(InvalidArgument),
                     "Entry name for %s cannot be null", _ownerName);
        return withEntryConst(EntryKey::fromCharPtr(entryName),
                              std::forward<Fn>(fn));
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const Entry &>)
    ReturnCode withEntryConst(const EntryKey &entryKey, Fn &&fn) const {
        auto keyCref = std::cref(entryKey);
        auto lambda = [this](const EntryKey &key_, auto &&fn_) -> ReturnCode {
            return _withEntryImpl(
                _entries, key_, std::forward<decltype(fn_)>(fn_), logComponent);
        };
        return this->_lockedConst("Directory::withEntryConst", ERR(Timeout),
                                  lambda, keyCref, std::forward<Fn>(fn));
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryKey &,
                                       const Entry &>)
    ReturnCode withAllConst(Fn &&fn) const {
        return withAllConst(
            std::forward<Fn>(fn),
            [](const EntryKey &, const Entry &) { return true; });
    }

    template <typename Fn, typename Filter>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryKey &,
                                       const Entry &>)
    ReturnCode withAllConst(Fn &&fn, Filter &&filter) const {
        auto lambda = [this](auto &&fn_, auto &&filter_) -> ReturnCode {
            return _withAllImpl(_entries, std::forward<decltype(fn_)>(fn_),
                                filter_, logComponent);
        };
        auto fnRef = std::cref(fn);
        auto filterRef = std::cref(filter);
        return this->_lockedConst("Directory::withAllConst", ERR(Timeout),
                                  lambda, fnRef, filterRef);
    }

    template <typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const EntryKey &,
                                       const Entry &>)
    [[nodiscard]] bool any(Filter &&filter) const {
        bool found = false;
        auto ret = withAllConst(
            [&found](const EntryKey &, const Entry &) -> ReturnCode {
                found = true;
                return OK();
            },
            filter);
        return ret.ok() && found;
    }

    std::expected<Entry, ReturnCode> extractOne(const char *entryName)
        requires requires { EntryKey::fromCharPtr(entryName); }
    {
        FAIL_IF_NULL(entryName, std::unexpected(ERR(InvalidArgument)),
                     "Entry name for %s cannot be null", _ownerName);
        return extractOne(EntryKey::fromCharPtr(entryName));
    }

    std::expected<Entry, ReturnCode> extractOne(const EntryKey &entryKey) {
        auto keyCref = std::cref(entryKey);
        std::optional<Entry> out;

        auto lambda = [this](const EntryKey &key_,
                             std::optional<Entry> &out_) -> ReturnCode {
            auto ret = _entries.extract(key_);
            FAIL_IF_UNEXPECTED(item, ret, ret.error(),
                               "Failed to extract directory entry %s->" SV_FMT,
                               _ownerName, SV_ARG(_keyName(key_)));
            out_.emplace(std::move(item));
            return OK();
        };

        auto ret = this->_locked("Directory::extract", ERR(Timeout), lambda,
                                 keyCref, std::ref(out));
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "Failed to extract directory entry %s->" SV_FMT, _ownerName,
                    SV_ARG(_keyName(entryKey)));
        FAIL_IF(!out.has_value(), std::unexpected(ERR(NotFound)),
                "Directory entry %s->" SV_FMT " not found for extraction",
                _ownerName, SV_ARG(_keyName(entryKey)));
        return std::move(*out);
    }

    template <typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const EntryKey &,
                                       const Entry &>)
    std::expected<EntryExtract, ReturnCode> extract(Filter filter) {
        EntryExtract out{};

        auto lambda = [this, &filter, &out]() -> ReturnCode {
            EntryKeySnapshot snap{};

            auto snapRet = _entries.forEach(
                [&snap, &filter](const EntryKey &key_,
                                 const Entry &entry_) -> ReturnCode {
                    if (std::invoke(filter, key_, entry_)) {
                        snap.keys[snap.count++] = key_;
                    }
                    return OK();
                });
            FAIL_IF_ERR(snapRet, snapRet,
                        "Failed to snapshot entries for filtered extract on %s",
                        _ownerName);

            for (size_t i = 0; i < snap.count; ++i) {
                auto extractRet = _entries.extract(snap.keys[i]);
                FAIL_IF(!extractRet, extractRet.error(),
                        "Failed to extract directory entry %s->" SV_FMT
                        " during filtered extract",
                        _ownerName, SV_ARG(_keyName(snap.keys[i])));
                out.entries[out.count++] = std::move(extractRet.value());
            }

            return OK();
        };

        auto ret =
            this->_locked("Directory::extract(filter)", ERR(Timeout), lambda);
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "Failed to extract directory entries for %s", _ownerName);
        return out;
    }

    ReturnCode remove(const char *entryName)
        requires requires { EntryKey::fromCharPtr(entryName); }
    {
        FAIL_IF_NULL(entryName, ERR(InvalidArgument),
                     "Entry name for %s cannot be null", _ownerName);
        return remove(EntryKey::fromCharPtr(entryName));
    }

    ReturnCode remove(const EntryKey &entryKey) {
        auto keyCref = std::cref(entryKey);
        auto lambda = [this](const EntryKey &key_) -> ReturnCode {
            auto ret = _entries.get(key_);
            FAIL_IF_UNEXPECTED(item, ret, ret.error(),
                               "Failed to get directory entry %s->" SV_FMT
                               " for removal",
                               _ownerName, SV_ARG(_keyName(key_)));
            auto displayName = _entryName(key_, item.get());
            if constexpr (Totem::Generic::detail::HasBeforeRemove<Derived>) {
                FAIL_IF_ERR(this->beforeRemove(displayName, item.get()),
                            ERR(OperationFailed),
                            "BeforeRemove hook failed for %s->" SV_FMT,
                            _ownerName, SV_ARG(displayName));
            }
            return _entries.remove(key_);
        };

        return this->_locked("Directory::remove", ERR(Timeout), lambda,
                             keyCref);
    }

    [[nodiscard]] std::expected<bool, ReturnCode> empty() const {
        enum class Result_ : uint8_t { Timeout, NotEmpty, Empty };
        auto lambda = [this]() -> Result_ {
            return _entries.empty() ? Result_::Empty : Result_::NotEmpty;
        };
        auto ret =
            this->_lockedConst("Directory::empty", Result_::Timeout, lambda);
        FAIL_IF(ret == Result_::Timeout, std::unexpected(ERR(Timeout)),
                "Failed to check if directory for %s is empty", _ownerName);
        return ret == Result_::Empty;
    }

    [[nodiscard]] std::expected<size_t, ReturnCode> size() const {
        struct Result_ {
            size_t value;
            bool timeout = false;
        };
        auto lambda = [this]() -> Result_ {
            return Result_{.value = _entries.size(), .timeout = false};
        };
        auto ret = this->_lockedConst(
            "Directory::size", Result_{.value = 0, .timeout = true}, lambda);
        FAIL_IF(ret.timeout, std::unexpected(ERR(Timeout)),
                "Failed to get directory size for %s", _ownerName);
        return ret.value;
    }

    [[nodiscard]] std::expected<bool, ReturnCode>
    contains(const char *entryName) const
        requires requires { EntryKey::fromCharPtr(entryName); }
    {
        FAIL_IF_NULL(entryName, std::unexpected(ERR(InvalidArgument)),
                     "Entry name for %s cannot be null", _ownerName);
        return contains(EntryKey::fromCharPtr(entryName));
    }

    [[nodiscard]] std::expected<bool, ReturnCode>
    contains(const EntryKey &entryKey) const {
        enum class Result_ : uint8_t { Timeout, NotFound, Found };
        auto keyCref = std::cref(entryKey);
        auto lambda = [this](const EntryKey &key_) -> Result_ {
            return _entries.contains(key_) ? Result_::Found : Result_::NotFound;
        };
        auto ret = this->_lockedConst("Directory::contains", Result_::Timeout,
                                      lambda, keyCref);
        FAIL_IF(ret == Result_::Timeout, std::unexpected(ERR(Timeout)),
                "Failed to check if directory for %s contains " SV_FMT,
                _ownerName, SV_ARG(_keyName(entryKey)));
        return ret == Result_::Found;
    }

    [[nodiscard]] bool isRegistrationLocked() const {
        return !_permitRegistration.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::expected<EntryKeySnapshot, ReturnCode>
    snapshotKeys(MinMax minMax = {}) const {
        return snapshotKeys(
            [](const EntryKey &, const Entry &) { return true; }, minMax);
    }

    template <typename Fn>
        requires(
            std::is_invocable_r_v<bool, Fn, const EntryKey &, const Entry &>)
    [[nodiscard]] std::expected<EntryKeySnapshot, ReturnCode>
    snapshotKeys(Fn &&filter, MinMax minMax = {}) const {
        EntryKeySnapshot out{};
        auto filterWrap = std::forward<Fn>(filter);
        auto lambda = [this, &out](auto &&filter_) -> ReturnCode {
            _snapshotKeysImpl(out, _entries, filter_);
            return OK();
        };
        auto ret = this->_lockedConst("Directory::snapshotKeys", ERR(Timeout),
                                      lambda, filterWrap);
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "Failed to snapshot directory entry keys for %s",
                    _ownerName);
        FAIL_IF(
            out.count > minMax.max, std::unexpected(ERR(Overflow)),
            "Snapshot of directory entry keys for %s exceeds maximum of %zu",
            _ownerName, minMax.max);
        FAIL_IF(out.count < minMax.min, std::unexpected(ERR(Underflow)),
                "Snapshot of directory entry keys for %s has count %zu below "
                "minimum of %zu",
                _ownerName, out.count, minMax.min);
        return out;
    }

  protected:
    std::expected<EntryKey, ReturnCode> _addImpl(const EntryKey &entryKey,
                                                 Entry entry) {
        auto keyCref = std::cref(entryKey);
        auto lambda = [this](const EntryKey &key_,
                             Entry &entry_) -> ReturnCode {
            FAIL_IF(!_permitRegistration.load(std::memory_order_acquire),
                    ERR(LifecycleError, InvalidState),
                    "%s: Registration is currently not permitted", _ownerName);
            auto displayName = _entryName(key_, entry_);
            if constexpr (Totem::Generic::detail::HasBeforeAdd<Derived>) {
                FAIL_IF_ERR(this->beforeAdd(displayName, entry_),
                            ERR(OperationFailed),
                            "BeforeAdd hook failed for %s->" SV_FMT, _ownerName,
                            SV_ARG(displayName));
            }
            auto ret = _entries.insert(key_, std::move(entry_));
            FAIL_IF_ERR(ret, ret,
                        "Failed to add directory entry %s->" SV_FMT " for %s",
                        _ownerName, SV_ARG(displayName), _ownerName);
            return OK();
        };
        auto ret = this->_locked("Directory::add", ERR(Timeout), lambda,
                                 keyCref, std::ref(entry));
        FAIL_IF_ERR(ret, std::unexpected(ret), "Error adding entry %s->" SV_FMT,
                    _ownerName, SV_ARG(_entryName(entryKey, entry)));
        return entryKey;
    }

    template <typename MapT, typename Filter>
    static void _snapshotKeysImpl(EntryKeySnapshot &snap, const MapT &entries,
                                  Filter &&filter)
        requires(std::is_invocable_r_v<bool, Filter, const EntryKey &,
                                       const Entry &>)
    {
        auto lambda = [&snap, &filter](const EntryKey &key_,
                                       const Entry &entry_) -> ReturnCode {
            if (std::invoke(filter, key_, entry_)) {
                snap.keys[snap.count++] = key_;
            }
            return OK();
        };
        (void)entries.forEach(lambda);
    }

    template <typename MapT, typename Fn>
    static ReturnCode _withEntryImpl(MapT &entries, const EntryKey &key,
                                     Fn &&fn, LogComponent logComponent) {
        auto ret = entries.get(key);
        FAIL_IF_UNEXPECTED(item, ret, ret.error(),
                           "Directory entry " SV_FMT " not found for withEntry",
                           SV_ARG(_keyName(key)));
        return std::invoke(std::forward<Fn>(fn), item.get());
    }

    template <typename MapT, typename Fn, typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const EntryKey &,
                                       const Entry &>)
    static ReturnCode _withAllImpl(MapT &entries, Fn &&fn, Filter &&filter,
                                   LogComponent logComponent) {
        auto result = OK();
        EntryKeySnapshot snap{};
        _snapshotKeysImpl(snap, entries, std::forward<Filter>(filter));

        for (size_t i = 0; i < snap.count; ++i) {
            auto key = snap.keys[i];
            auto ret = entries.get(key);
            if (!ret.has_value()) {
                _log_w("Entry " SV_FMT " disappeared during withAll iteration",
                       SV_ARG(_keyName(key)));
                continue;
            }

            auto &entry = ret.value().get();
            auto fnRet = std::invoke(fn, key, entry);
            if (!fnRet.ok()) {
                result = fnRet;
                _log_e("Error while iterating directory entry " SV_FMT
                       ": " ERR_FMT,
                       SV_ARG(_entryName(key, entry)), ERR_ARG(fnRet));
            }
        }

        return result;
    }

    [[nodiscard]] static std::string_view _keyName(const EntryKey &key) {
        return Representation::keyName(key);
    }

    [[nodiscard]] static std::string_view _entryName(const EntryKey &key,
                                                     const Entry &entry) {
        return Representation::entryName(key, entry);
    }

    std::atomic<bool> _permitRegistration{false};
    SlottedMapType _entries;
    const char *_ownerName;
    LogComponent logComponent;
};

template <class Derived, typename Key, typename Entry, size_t N,
          class Representation =
              Totem::Generic::detail::DirectoryRepresentation<Key, Entry>>
class BaseGettableDirectory
    : public BaseDirectory<Derived, Key, Entry, N, Representation> {
    using Base = BaseDirectory<Derived, Key, Entry, N, Representation>;

  public:
    explicit BaseGettableDirectory(const char *ownerName,
                                   LogComponent component)
        : Base(ownerName, component) {}

    using typename Base::EntryArray;
    using typename Base::EntryKey;
    using typename Base::EntryKeySnapshot;

    struct EntrySnapshot {
        EntryArray entries;
        size_t count = 0;
    };

    std::expected<Entry, ReturnCode> getCopy(const EntryKey &key) const
        requires(std::copy_constructible<Entry>)
    {
        std::optional<Entry> out;

        auto rc =
            this->withEntryConst(key, [&out](const Entry &entry) -> ReturnCode {
                out = entry;
                return OK();
            });

        if (!rc.ok()) {
            return std::unexpected(rc);
        }
        if (!out.has_value()) {
            return std::unexpected(ERR(NotFound));
        }
        return std::move(*out);
    }

    template <typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const EntryKey &,
                                       const Entry &>)
    std::expected<EntrySnapshot, ReturnCode> snapshot(Filter filter) const {
        EntrySnapshot out{};

        const auto &self = *this;

        auto ret = self.withAllConst(
            [&out](const EntryKey & /*unused*/,
                   const Entry &entry) -> ReturnCode {
                if (out.count >= out.entries.size()) {
                    return ERR(Overflow);
                }
                out.entries[out.count++] = entry;
                return OK();
            },
            filter);

        FAIL_IF_ERR_FWD_UNEXPECTED(
            ret, "Failed to get snapshot of directory entries for %s",
            this->ownerName());

        return out;
    }

    std::expected<EntrySnapshot, ReturnCode> snapshot() const {
        return snapshot([](const EntryKey &, const Entry &) { return true; });
    }
};

inline constexpr MutexContract<BaseDirectory<NoConfig, void *, void *, 1>>
    directory_mutex_contract;
