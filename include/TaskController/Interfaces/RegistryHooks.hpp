#pragma once

#include "Concepts/Base.hpp"
#include "TaskControllerRegistry/Interfaces/TaskSourceHooks.hpp"
#include "Types/Error.hpp"
#include <concepts>
#include <cstdint>

namespace Totem::TaskController {

template <class T>
concept HasRegisterSource =
    requires(T &cls, uintptr_t key,
             TaskControllerRegistry::TaskSourceHooks hooks,
             TaskControllerRegistry::TaskSourceInfo info) {
        { cls.registerSource(key, hooks, info) } -> std::same_as<ReturnCode>;
    };

template <class T>
concept HasDeregisterSource = requires(T &cls, uintptr_t key) {
    { cls.deregisterSource(key) } -> std::same_as<ReturnCode>;
};

struct RegistryHooks {
    template <class T> struct Contract {
        static_assert(HasRegisterSource<T>, "T must provide registerSource");
        static_assert(HasDeregisterSource<T>,
                      "T must provide deregisterSource");
        static_assert(IsNamedEntity<T>, "T must be a named entity");
    };

    // Virtual dispatch for breaking circular dependency between Controller and
    // Registry, low overhead since registry hooks are only used during
    // controller construction and destruction
    void *self = nullptr;

    // All required
    ReturnCode (*registerHook)(
        void *, uintptr_t, TaskControllerRegistry::TaskSourceHooks,
        TaskControllerRegistry::TaskSourceInfo) = nullptr;
    ReturnCode (*deregisterHook)(void *, uintptr_t) = nullptr;

    ReturnCode
    registerSource(uintptr_t sourceKey,
                   TaskControllerRegistry::TaskSourceHooks hooks,
                   TaskControllerRegistry::TaskSourceInfo info) const {
        return registerHook(self, sourceKey, hooks, info);
    }
    ReturnCode deregisterSource(uintptr_t sourceKey) const {
        return deregisterHook(self, sourceKey);
    }

    template <class T>
        requires requires { sizeof(Contract<T>); }
    static RegistryHooks bind(T &obj) {
        return RegistryHooks{
            .self = std::addressof(obj),
            .registerHook =
                [](void *ptr, uintptr_t key,
                   TaskControllerRegistry::TaskSourceHooks hooks,
                   TaskControllerRegistry::TaskSourceInfo info) -> ReturnCode {
                return static_cast<T *>(ptr)->registerSource(key, hooks, info);
            },
            .deregisterHook = [](void *ptr, uintptr_t key) -> ReturnCode {
                return static_cast<T *>(ptr)->deregisterSource(key);
            },
        };
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && registerHook != nullptr &&
               deregisterHook != nullptr;
    }
};

} // namespace Totem::TaskController
