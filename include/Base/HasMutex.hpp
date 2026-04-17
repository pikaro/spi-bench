#pragma once

#include "Concepts/Base.hh"
#include "Macros/Facade.hh"
#include "Mutex/Facade.hh"
#include <cstdint>
#include <type_traits>

namespace Totem::Core {

constexpr uint32_t globalDefaultMutexTimeoutMs = 100;

template <class T> struct MutexContract;

template <class Derived> class HasMutex {
  public:
    HasMutex() : _name{Derived::name} {}

  protected:
    void _setDefaultMutexTimeout(uint32_t timeoutMs) {
        defaultMutexTimeoutMs = timeoutMs;
    }

    template <class F, class... Args>
    auto _withMutex(Mutex::MutexExecSpec<F, Args...> spec) ->
        typename Mutex::MutexExecSpec<F, Args...>::R {
        auto mtx = spec.mutex ? spec.mutex : _mutex.get();
        FAIL_IF_NULL(mtx, spec.failValue, "Mutex for %s cannot be null", _name);
        spec.mutex = mtx;
        spec.timeoutMs = spec.timeoutMs.value_or(defaultMutexTimeoutMs);
        return Mutex::execute_mutex_exec_spec(std::move(spec));
    }

    template <class F, class... Args>
    typename Mutex::MutexExecSpecConst<F, Args...>::R
    _withMutexConst(Mutex::MutexExecSpecConst<F, Args...> spec) const {
        using Spec = Mutex::MutexExecSpecConst<F, Args...>;
        using Fn = typename Spec::Fn;
        using R = typename Spec::R;

        static_assert(
            std::is_invocable_r_v<R, const Fn &, std::decay_t<Args>...>,
            "_withMutexConst requires a const-callable functor "
            "(rejects mutable lambdas / non-const operator())");

        auto mtx = spec.mutex ? spec.mutex : _mutex.get();
        FAIL_IF_NULL(mtx, spec.failValue, "Mutex for %s cannot be null", _name);
        spec.mutex = mtx;
        spec.timeoutMs = spec.timeoutMs.value_or(defaultMutexTimeoutMs);
        return Mutex::execute_mutex_exec_spec_const(std::move(spec));
    }

    template <typename FailT, typename Fn, typename... Args>
    auto _locked(const char *mutexName, FailT &&failValue, Fn &&fn,
                 Args &&...args) {
        auto spec = Mutex::make_mutex_exec_spec(std::forward<Fn>(fn),
                                                std::forward<Args>(args)...);
        spec.name = mutexName;
        spec.failValue = std::forward<FailT>(failValue);
        spec.timeoutMs = defaultMutexTimeoutMs;
        return _withMutex(spec);
    }

    template <typename FailT, typename Fn, typename... Args>
    auto _lockedConst(const char *mutexName, FailT &&failValue, Fn &&fn,
                      Args &&...args) const {
        auto spec = Mutex::make_mutex_exec_spec_const(
            std::forward<Fn>(fn), std::forward<Args>(args)...);
        spec.name = mutexName;
        spec.failValue = std::forward<FailT>(failValue);
        spec.timeoutMs = defaultMutexTimeoutMs;
        return _withMutexConst(spec);
    }

  private:
    const char *_name;
    uint32_t defaultMutexTimeoutMs = globalDefaultMutexTimeoutMs;
    Mutex::Mutex _mutex;
};

template <class T> struct MutexContract {
    static_assert(IsNamedEntity<T>, "Type must have a name");
};

} // namespace Totem::Core

using Totem::Core::HasMutex;
using Totem::Core::MutexContract;
