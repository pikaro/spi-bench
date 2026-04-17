#pragma once

#include "Concepts/Base.hh"
#include "Macros/Facade.hh"
#include "Types/Error.hh"
#include "Types/Signal.hh"
#include <concepts>

namespace Totem::TaskController {

struct TaskHooks {
    template <class T> struct Contract {
        static_assert(
            requires(T &cls) { cls._onTaskStep(); },
            "T must provide _onTaskStep()");
        static_assert(IsNamedEntity<T>, "T must be a named entity");
    };

    // Virtual dispatch for swappable task hook providers
    void *self = nullptr;

    ReturnCode (*stepHook)(void *) = nullptr;
    ReturnCode (*startHook)(void *) = nullptr;
    ReturnCode (*notifyHook)(void *, Signal) = nullptr;
    ReturnCode (*stopHook)(void *) = nullptr;

    ReturnCode onStart() const {
        return startHook != nullptr ? startHook(self) : OK(CoreError);
    }

    ReturnCode onStep() const { return stepHook(self); }

    ReturnCode onNotify(Signal sig) const {
        return notifyHook != nullptr ? notifyHook(self, sig) : OK(CoreError);
    }

    ReturnCode onStop() const {
        return stopHook != nullptr ? stopHook(self) : OK(CoreError);
    }

  private:
    template <class T> static consteval bool hasStep() {
        return requires(T &cls) {
            { cls._onTaskStep() } -> std::same_as<ReturnCode>;
        };
    }

    template <class T> static consteval bool hasStart() {
        return requires(T &cls) {
            { cls._onTaskStart() } -> std::same_as<ReturnCode>;
        };
    }

    template <class T> static consteval bool hasNotify() {
        return requires(T &cls, Signal sig) {
            { cls._onTaskNotify(sig) } -> std::same_as<ReturnCode>;
        };
    }

    template <class T> static consteval bool hasStop() {
        return requires(T &cls) {
            { cls._onTaskStop() } -> std::same_as<ReturnCode>;
        };
    }

  public:
    template <class T>
    static TaskHooks bind(T &obj)
        requires requires { sizeof(Contract<T>); }
    {
        TaskHooks hooks{};
        hooks.self = std::addressof(obj);

        hooks.stepHook = +[](void *ptr) -> ReturnCode {
            return static_cast<T *>(ptr)->_onTaskStep();
        };

        if constexpr (hasStart<T>()) {
            hooks.startHook = +[](void *ptr) -> ReturnCode {
                return static_cast<T *>(ptr)->_onTaskStart();
            };
        }

        if constexpr (hasNotify<T>()) {
            hooks.notifyHook = +[](void *ptr, Signal sig) -> ReturnCode {
                return static_cast<T *>(ptr)->_onTaskNotify(sig);
            };
        }

        if constexpr (hasStop<T>()) {
            hooks.stopHook = +[](void *ptr) -> ReturnCode {
                return static_cast<T *>(ptr)->_onTaskStop();
            };
        }

        return hooks;
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && stepHook != nullptr;
    }
};

} // namespace Totem::TaskController
