#pragma once

#include "Concepts/Base.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceHooks.hh"
#include "Types/Error.hh"
#include <concepts>

namespace Totem::TaskController {

template <class T>
concept HasRegisterSource =
    requires(T &cls, const char *name,
             TaskControllerRegistry::TaskSourceHooks hooks,
             TaskControllerRegistry::TaskSourceInfo info) {
        { cls.registerSource(name, hooks, info) } -> std::same_as<ReturnCode>;
    };

template <class T>
concept HasDeregisterSource = requires(T &cls, const char *name) {
    { cls.deregisterSource(name) } -> std::same_as<ReturnCode>;
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
        void *, const char *, TaskControllerRegistry::TaskSourceHooks,
        TaskControllerRegistry::TaskSourceInfo) = nullptr;
    ReturnCode (*deregisterHook)(void *, const char *) = nullptr;

    ReturnCode
    registerSource(const char *sourceName,
                   TaskControllerRegistry::TaskSourceHooks hooks,
                   TaskControllerRegistry::TaskSourceInfo info) const {
        return registerHook(self, sourceName, hooks, info);
    }
    ReturnCode deregisterSource(const char *sourceName) const {
        return deregisterHook(self, sourceName);
    }

    template <class T>
        requires requires { sizeof(Contract<T>); }
    static RegistryHooks bind(T &obj) {
        return RegistryHooks{
            .self = std::addressof(obj),
            .registerHook =
                [](void *ptr, const char *name,
                   TaskControllerRegistry::TaskSourceHooks hooks,
                   TaskControllerRegistry::TaskSourceInfo info) -> ReturnCode {
                return static_cast<T *>(ptr)->registerSource(name, hooks, info);
            },
            .deregisterHook = [](void *ptr, const char *name) -> ReturnCode {
                return static_cast<T *>(ptr)->deregisterSource(name);
            },
        };
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && registerHook != nullptr &&
               deregisterHook != nullptr;
    }
};

} // namespace Totem::TaskController
