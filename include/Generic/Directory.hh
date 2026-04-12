#pragma once

#include "Common.hh"

#include "Base/HasMutex.hh"
#include "Types/Collection.hh"
#include "Types/Error.hh"
#include <array>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <optional>

template <typename Entry> struct DirectoryHooks {
    void *self = nullptr;

    ReturnCode (*beforeAddHook)(void *, const char *, const Entry &) = nullptr;
    ReturnCode (*beforeRemoveHook)(void *, const char *,
                                   const Entry &) = nullptr;

    ReturnCode onBeforeAdd(const char *name, const Entry &entry) const {
        return (beforeAddHook != nullptr) ? beforeAddHook(self, name, entry)
                                          : OK(CoreError);
    }
    ReturnCode onBeforeRemove(const char *name, const Entry &entry) const {
        return (beforeRemoveHook != nullptr)
                   ? beforeRemoveHook(self, name, entry)
                   : OK(CoreError);
    }

    [[nodiscard]] bool validate() const {
        auto anyHookSet =
            beforeAddHook != nullptr || beforeRemoveHook != nullptr;
        return self != nullptr || !anyHookSet;
    }
};

template <typename Entry, size_t N, size_t IdLen = 32>
class Directory : HasMutex<Directory<Entry, N, IdLen>> {
    static_assert(std::is_nothrow_move_constructible_v<Entry>,
                  "Directory entries must be nothrow move constructible");
    static_assert(std::is_nothrow_move_assignable_v<Entry>,
                  "Directory entries must be nothrow move assignable");
    static_assert(std::is_default_constructible_v<Entry>,
                  "Directory entries must be default constructible");
    static_assert(std::movable<Entry>, "Directory entries must be movable");

    static_assert(IdLen > 0, "IdLen must be greater than 0");
    static_assert(N > 0, "Directory size N must be greater than 0");

  public:
    static constexpr const char *name = "Directory";

    using EntryNameKey = NameKey<IdLen>;

    using EntryKeyArray = std::array<EntryNameKey, N>;
    using EntryArray = std::array<Entry, N>;

    struct EntryKeySnapshot {
        EntryKeyArray keys;
        size_t count = 0;
    };

    struct EntryExtract {
        EntryArray entries;
        size_t count = 0;
    };

    using SlottedMapType = SlottedMap<Entry, N, IdLen>;

    explicit Directory(const char *ownerName) : _ownerName(ownerName) {}

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
    ReturnCode withEntry(const char *entryName, Fn &&fn) {
        FAIL_IF_NULL(entryName, ERR(InvalidArgument),
                     "Entry name for %s cannot be null", _ownerName);
        return withEntry(EntryNameKey::fromCharPtr(entryName),
                         std::forward<Fn>(fn));
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, Entry &>)
    ReturnCode withEntry(const EntryNameKey &entryNameKey, Fn &&fn) {
        auto nameKeyCref = std::cref(entryNameKey);
        auto lambda = [this](const EntryNameKey &nameKey_,
                             auto &&fn_) -> ReturnCode {
            return _withEntryImpl(_entries, nameKey_,
                                  std::forward<decltype(fn_)>(fn_));
        };
        return this->_locked("Directory::withEntry", ERR(Timeout), lambda,
                             nameKeyCref, std::forward<Fn>(fn));
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryNameKey &,
                                       Entry &>)
    ReturnCode withAll(Fn &&fn) {
        return withAll(
            std::forward<Fn>(fn),
            [](const EntryNameKey &, const Entry &) { return true; });
    }

    template <typename Fn, typename Filter>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryNameKey &,
                                       Entry &>)
    ReturnCode withAll(Fn &&fn, Filter &&filter) {
        auto lambda = [this](auto &&fn_, auto &&filter_) -> ReturnCode {
            return _withAllImpl(_entries, std::forward<decltype(fn_)>(fn_),
                                filter_);
        };
        return this->_locked("Directory::withAll", ERR(Timeout), lambda,
                             std::forward<Fn>(fn),
                             std::forward<Filter>(filter));
    }

    template <typename Fn>
    ReturnCode withEntryConst(const char *entryName, Fn &&fn) const {
        FAIL_IF_NULL(entryName, ERR(InvalidArgument),
                     "Entry name for %s cannot be null", _ownerName);
        return withEntryConst(EntryNameKey::fromCharPtr(entryName),
                              std::forward<Fn>(fn));
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const Entry &>)
    ReturnCode withEntryConst(const EntryNameKey &nameKey, Fn &&fn) const {
        auto nameKeyCref = std::cref(nameKey);
        auto lambda = [this](const EntryNameKey &nameKey_,
                             auto &&fn_) -> ReturnCode {
            return _withEntryImpl(_entries, nameKey_,
                                  std::forward<decltype(fn_)>(fn_));
        };
        return this->_lockedConst("Directory::withEntryConst", ERR(Timeout),
                                  lambda, nameKeyCref, std::forward<Fn>(fn));
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryNameKey &,
                                       const Entry &>)
    ReturnCode withAllConst(Fn &&fn) const {
        return withAllConst(
            std::forward<Fn>(fn),
            [](const EntryNameKey &, const Entry &) { return true; });
    }

    template <typename Fn, typename Filter>
        requires(std::is_invocable_r_v<ReturnCode, Fn, const EntryNameKey &,
                                       const Entry &>)
    ReturnCode withAllConst(Fn &&fn, Filter &&filter) const {
        auto lambda = [this](auto &&fn_, auto &&filter_) -> ReturnCode {
            return _withAllImpl(_entries, std::forward<decltype(fn_)>(fn_),
                                filter_);
        };
        return this->_lockedConst("Directory::withAllConst", ERR(Timeout),
                                  lambda, std::forward<Fn>(fn),
                                  std::forward<Filter>(filter));
    }

    template <typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const EntryNameKey &,
                                       const Entry &>)
    [[nodiscard]] bool any(Filter &&filter) const {
        auto ret = withAllConst(
            [](const EntryNameKey & /*unused*/,
               const Entry & /*unused*/) -> ReturnCode { return OK(); },
            filter);
        return ret.ok();
    }

    std::expected<Entry, ReturnCode> extractOne(const char *entryName) {
        FAIL_IF_NULL(entryName, std::unexpected(ERR(InvalidArgument)),
                     "Entry name for %s cannot be null", _ownerName);
        return extractOne(EntryNameKey::fromCharPtr(entryName));
    }

    std::expected<Entry, ReturnCode> extractOne(const EntryNameKey &nameKey) {
        auto nameKeyCref = std::cref(nameKey);
        std::optional<Entry> out;

        auto lambda = [this](const EntryNameKey &nameKey_,
                             std::optional<Entry> &out_) -> ReturnCode {
            auto ret = _entries.extract(nameKey_);
            FAIL_IF_UNEXPECTED(item, ret, ret.error(),
                               "Failed to extract directory entry %s->%s",
                               _ownerName, nameKey_.name.data());
            out_.emplace(std::move(item));
            return OK();
        };

        auto ret = this->_locked("Directory::extract", ERR(Timeout), lambda,
                                 nameKeyCref, std::ref(out));
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "Failed to extract directory entry %s->%s", _ownerName,
                    nameKey.name.data());
        FAIL_IF(!out.has_value(), std::unexpected(ERR(NotFound)),
                "Directory entry %s->%s not found for extraction", _ownerName,
                nameKey.name.data());
        return std::move(*out);
    }

    template <typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const EntryNameKey &,
                                       const Entry &>)
    std::expected<EntryExtract, ReturnCode> extract(Filter filter) {
        EntryExtract out{};

        auto lambda = [this, &filter, &out]() -> ReturnCode {
            EntryKeySnapshot snap{};

            auto snapRet = _entries.forEach(
                [&snap, &filter](const EntryNameKey &nameKey_,
                                 const Entry &entry_) -> ReturnCode {
                    if (std::invoke(filter, nameKey_, entry_)) {
                        snap.keys[snap.count++] = nameKey_;
                    }
                    return OK();
                });
            FAIL_IF_ERR(snapRet, snapRet,
                        "Failed to snapshot entries for filtered extract on %s",
                        _ownerName);

            for (size_t i = 0; i < snap.count; ++i) {
                auto extractRet = _entries.extract(snap.keys[i]);
                FAIL_IF(!extractRet, extractRet.error(),
                        "Failed to extract directory entry %s->%s "
                        "during filtered extract",
                        _ownerName, snap.keys[i].name.data());
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

    ReturnCode remove(const char *entryName) {
        FAIL_IF_NULL(entryName, ERR(InvalidArgument),
                     "Entry name for %s cannot be null", _ownerName);
        return remove(EntryNameKey::fromCharPtr(entryName));
    }

    ReturnCode remove(const EntryNameKey &nameKey) {
        auto nameKeyCref = std::cref(nameKey);
        auto lambda = [this](const EntryNameKey &nameKey_) -> ReturnCode {
            auto ret = _entries.get(nameKey_);
            FAIL_IF_UNEXPECTED(
                item, ret, ret.error(),
                "Failed to get directory entry %s->%s for removal", _ownerName,
                nameKey_.name.data());
            FAIL_IF_ERR(_hooks.onBeforeRemove(nameKey_.name.data(), item.get()),
                        ERR(OperationFailed),
                        "BeforeRemove hook failed for %s->%s during removal",
                        _ownerName, nameKey_.name.data());
            return _entries.remove(nameKey_);
        };

        return this->_locked("Directory::remove", ERR(Timeout), lambda,
                             nameKeyCref);
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
    contains(const char *entryName) const {
        FAIL_IF_NULL(entryName, std::unexpected(ERR(InvalidArgument)),
                     "Entry name for %s cannot be null", _ownerName);
        return contains(EntryNameKey::fromCharPtr(entryName));
    }

    [[nodiscard]] std::expected<bool, ReturnCode>
    contains(const EntryNameKey &nameKey) const {
        enum class Result_ : uint8_t { Timeout, NotFound, Found };
        auto nameKeyCref = std::cref(nameKey);
        auto lambda = [this](const EntryNameKey &nameKey_) -> Result_ {
            return _entries.contains(nameKey_) ? Result_::Found
                                               : Result_::NotFound;
        };
        auto ret = this->_lockedConst("Directory::contains", Result_::Timeout,
                                      lambda, nameKeyCref);
        FAIL_IF(ret == Result_::Timeout, std::unexpected(ERR(Timeout)),
                "Failed to check if directory for %s contains %s", _ownerName,
                nameKey.name.data());
        return ret == Result_::Found;
    }

    [[nodiscard]] bool isRegistrationLocked() const {
        return !_permitRegistration.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::expected<EntryKeySnapshot, ReturnCode>
    snapshotKeys() const {
        return snapshotKeys(
            [](const EntryNameKey &, const Entry &) { return true; });
    }

    template <typename Fn>
        requires(std::is_invocable_r_v<bool, Fn, const EntryNameKey &,
                                       const Entry &>)
    [[nodiscard]] std::expected<EntryKeySnapshot, ReturnCode>
    snapshotKeys(Fn &&filter) const {
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
        return out;
    }

  protected:
    void _setHooks(DirectoryHooks<Entry> hooks) {
        ABORT_IF_NOT(hooks.validate(), "Invalid directory hooks for %s",
                     _ownerName);
        _hooks = hooks;
    }

    std::expected<EntryNameKey, ReturnCode>
    _addImpl(const EntryNameKey &entryNameKey, Entry entry) {
        auto nameKeyCref = std::cref(entryNameKey);
        auto lambda = [this](const EntryNameKey &nameKey_,
                             Entry &entry_) -> ReturnCode {
            FAIL_IF(!_permitRegistration.load(std::memory_order_acquire),
                    ERR(LifecycleError, InvalidState),
                    "%s: Registration is currently not permitted", _ownerName);
            FAIL_IF_ERR(_hooks.onBeforeAdd(nameKey_.name.data(), entry_),
                        ERR(OperationFailed),
                        "BeforeAdd hook failed for %s->%s", _ownerName,
                        nameKey_.name.data());
            auto ret = _entries.insert(nameKey_, std::move(entry_));
            FAIL_IF_ERR(ret, ret, "Failed to add directory entry %s->%s for %s",
                        _ownerName, nameKey_.name.data(), _ownerName);
            return OK();
        };
        auto ret = this->_locked("Directory::add", ERR(Timeout), lambda,
                                 nameKeyCref, std::ref(entry));
        FAIL_IF_ERR(ret, std::unexpected(ret), "Error adding entry %s->%s",
                    _ownerName, entryNameKey.name.data());
        return entryNameKey;
    }

    template <typename MapT, typename Filter>
    static void _snapshotKeysImpl(EntryKeySnapshot &snap, const MapT &entries,
                                  Filter &&filter)
        requires(std::is_invocable_r_v<bool, Filter, const EntryNameKey &,
                                       const Entry &>)
    {
        auto lambda = [&snap, &filter](const EntryNameKey &nameKey_,
                                       const Entry &entry_) -> ReturnCode {
            if (std::invoke(filter, nameKey_, entry_)) {
                snap.keys[snap.count++] = nameKey_;
            }
            return OK();
        };
        (void)entries.forEach(lambda);
    }

    template <typename MapT, typename Fn>
    static ReturnCode _withEntryImpl(MapT &entries, const EntryNameKey &nameKey,
                                     Fn &&fn) {
        auto ret = entries.get(nameKey);
        FAIL_IF_UNEXPECTED(item, ret, ret.error(),
                           "Directory entry %s not found for withEntry",
                           nameKey.name.data());
        return std::invoke(std::forward<Fn>(fn), item.get());
    }

    template <typename MapT, typename Fn, typename Filter>
        requires(std::is_invocable_r_v<bool, Filter, const EntryNameKey &,
                                       const Entry &>)
    static ReturnCode _withAllImpl(MapT &entries, Fn &&fn, Filter &&filter) {
        auto result = OK();
        EntryKeySnapshot snap{};
        _snapshotKeysImpl(snap, entries, std::forward<Filter>(filter));

        for (size_t i = 0; i < snap.count; ++i) {
            auto key = snap.keys[i];
            auto ret = entries.get(key);
            if (!ret.has_value()) {
                _log_w("Entry %s disappeared during withAll iteration",
                       key.name.data());
                continue;
            }

            auto &entry = ret.value().get();
            auto fnRet = std::invoke(fn, key, entry);
            if (!fnRet.ok()) {
                result = fnRet;
                _log_e("Error while iterating directory entry %s: %s",
                       key.name.data(), fnRet.format());
            }
        }

        return result;
    }

    std::atomic<bool> _permitRegistration{false};
    SlottedMapType _entries;
    const char *_ownerName;
    DirectoryHooks<Entry> _hooks{};

    using DefaultError = CoreError;
};

template <typename Entry, size_t N, size_t IdLen = 32>
class GettableDirectory : public Directory<Entry, N, IdLen> {
  public:
    using Base = Directory<Entry, N, IdLen>;
    using typename Base::EntryNameKey;

    using Base::Base;

    std::expected<Entry, ReturnCode> getCopy(const EntryNameKey &key) const
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

  private:
    using DefaultError = CoreError;
};

inline constexpr MutexContract<Directory<void *, 1>> directory_mutex_contract;
