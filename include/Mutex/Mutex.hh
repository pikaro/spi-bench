#pragma once

#include "Common.hh"

#include "Concepts/Performance.hh"
#include "Mutex/MutexGuard.hh"
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Totem::Core {

class Mutex {
  public:
    Mutex() : _handle{xSemaphoreCreateMutex()} {
        ABORT_IF_NULL(_handle, "Failed to create mutex");
    }

    ~Mutex() {
        if (_handle != nullptr) {
            vSemaphoreDelete(_handle);
        }
    }

    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;

    Mutex(Mutex &&) = delete;
    Mutex &operator=(Mutex &&) = delete;

    [[nodiscard]] SemaphoreHandle_t get() const { return _handle; }

  private:
    SemaphoreHandle_t _handle{};
};

template <class T, std::size_t MaxBytes = 8>
concept IsMutexArg = IsRefWrap<T> || IsTinyTrivialValue<T, MaxBytes>;

template <std::size_t MaxBytes = 8, class... Ts>
concept AreMutexArgs = (IsMutexArg<Ts, MaxBytes> && ...);

template <class R, std::size_t MaxBytes = 8>
concept IsMutexReturn =
    IsTinyTrivialValue<R, MaxBytes> && IsDefaultConstructibleValue<R>;

template <class T>
concept IsConstRefWrap = IsRefWrap<T> && std::is_const_v<typename T::type>;

template <class T, std::size_t MaxBytes = 8>
concept IsMutexArgConst =
    IsConstRefWrap<T> || (!IsRefWrap<T> && IsTinyTrivialValue<T, MaxBytes>);

template <class F, class... Args> struct MutexExecSpec {
    using Fn = std::decay_t<F>;
    using R = std::invoke_result_t<Fn, std::decay_t<Args>...>;

    // Max 4x the size of a pointer
    static_assert(IsTinyTrivialValue<Fn, 16>,
                  "MutexExecSpec callable must be tiny + trivially copyable");
    static_assert(std::is_trivially_destructible_v<Fn>,
                  "MutexExecSpec callable must be trivially destructible");
    static_assert(AreMutexArgs<8, Args...>,
                  "MutexExecSpec arguments must be tiny, trivial, or "
                  "std::ref/wrap");
    static_assert(
        IsMutexReturn<R, 8>,
        "MutexExecSpec return must be tiny, trivial, default-constructible");

    std::optional<uint32_t> timeoutMs;
    const char *name;
    R failValue;
    Fn fun;
    std::tuple<std::decay_t<Args>...> args;
    SemaphoreHandle_t mutex;
};

template <class F, class... Args>
    requires(IsMutexArgConst<std::decay_t<Args>> && ...)
struct MutexExecSpecConst {
    using Fn = std::decay_t<F>;
    using R = std::invoke_result_t<Fn, std::decay_t<Args>...>;

    // Max 4x the size of a pointer
    static_assert(
        IsTinyTrivialValue<Fn, 16>,
        "MutexExecSpecConst callable must be tiny + trivially copyable");
    static_assert(std::is_trivially_destructible_v<Fn>,
                  "MutexExecSpecConst callable must be trivially destructible");
    static_assert(AreMutexArgs<8, Args...>,
                  "MutexExecSpecConst arguments must be tiny, trivial, or "
                  "std::ref/wrap const");
    static_assert(IsMutexReturn<R, 8>, "MutexExecSpecConst return must be "
                                       "tiny, trivial, default-constructible");

    std::optional<uint32_t> timeoutMs;
    const char *name;
    R failValue;
    Fn fun;
    std::tuple<std::decay_t<Args>...> args;
    SemaphoreHandle_t mutex;
};

// Pass a std::ref(big) to avoid copying large objects
template <class F, class... Args>
MutexExecSpec<std::decay_t<F>, std::decay_t<Args>...>
make_mutex_exec_spec(F &&fun, Args &&...args) {
    using Spec = MutexExecSpec<std::decay_t<F>, std::decay_t<Args>...>;
    return Spec{
        .timeoutMs = std::nullopt,
        .name = std::decay_t<const char *>("mtx::unknown"),
        .failValue = typename Spec::R{},
        .fun = std::forward<F>(fun),
        .args = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...),
        .mutex = nullptr,
    };
}

template <class F, class... Args>
    requires(IsMutexArgConst<std::decay_t<Args>> && ...)
MutexExecSpecConst<std::decay_t<F>, std::decay_t<Args>...>
make_mutex_exec_spec_const(F &&fun, Args &&...args) {
    using Spec = MutexExecSpecConst<std::decay_t<F>, std::decay_t<Args>...>;
    return Spec{
        .timeoutMs = std::nullopt,
        .name = std::decay_t<const char *>("mtx::unknown"),
        .failValue = typename Spec::R{},
        .fun = std::forward<F>(fun),
        .args = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...),
        .mutex = nullptr,
    };
}

template <class F, class... Args>
typename MutexExecSpec<F, Args...>::R
execute_mutex_exec_spec(MutexExecSpec<F, Args...> &&req) {
    FAIL_IF_NULL(req.mutex, req.failValue, "MutexRequest %s", req.name);
    MutexGuard guard(req.mutex,
                     pdMS_TO_TICKS(req.timeoutMs.value_or(MS_MAX_DELAY)));
    if (!guard.locked()) {
        _log_e("Failed to acquire mutex %s", req.name);
        return std::move(req.failValue);
    }
    return std::apply(std::move(req.fun), std::move(req.args));
}

template <class F, class... Args>
typename MutexExecSpec<F, Args...>::R
execute_mutex_exec_spec_const(MutexExecSpecConst<F, Args...> &&req) {
    FAIL_IF_NULL(req.mutex, req.failValue, "MutexRequest %s", req.name);
    MutexGuard guard(req.mutex,
                     pdMS_TO_TICKS(req.timeoutMs.value_or(MS_MAX_DELAY)));
    if (!guard.locked()) {
        _log_e("Failed to acquire mutex %s", req.name);
        return std::move(req.failValue);
    }
    return std::apply(std::move(req.fun), std::move(req.args));
}

} // namespace Totem::Core

using Totem::Core::execute_mutex_exec_spec;
using Totem::Core::make_mutex_exec_spec;
using Totem::Core::make_mutex_exec_spec_const;
using Totem::Core::MutexExecSpec;
