#pragma once

#include "Base/HasLifecycle.hpp"
#include "Concepts/Base.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/detail/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include <type_traits>

template <class Derived, typename ConfT = NoConfig> class HasTaskController {
  protected:
    explicit HasTaskController(Totem::TaskController::IRegistry &registryHooks)
        : _taskController(Derived::name, registryHooks, Derived::logComponent) {
    }

    ReturnCode _beginTaskController() { return _taskController.begin(); }
    ReturnCode _endTaskController() { return _taskController.end(); }

    Totem::TaskController::Config
    _taskConfig(Totem::TaskController::Config config) {
        if (config.allocation ==
            Totem::TaskController::TaskAllocation::Static) {
            if constexpr (_useStaticTaskStorage) {
                config.staticMemory = _taskStorage.memory();
            }
        }
        return config;
    }

    Totem::TaskController::Controller _taskController;

  private:
    static constexpr bool _useStaticTaskStorage =
        Totem::TaskController::StaticTaskStorageEnabled<Derived>::value;
    using TaskStorage = std::conditional_t<
        _useStaticTaskStorage,
        Totem::TaskController::detail::StaticTaskStorage<
            Totem::TaskController::StaticStackSize<Derived>::value>,
        Totem::TaskController::detail::NoStaticTaskStorage>;

    TaskStorage _taskStorage{};
};

template <class T> struct TaskControllerContract {
    static_assert(IsNamedEntity<T>, "Type must be a named entity");
    static_assert(
        requires { T::logComponent; },
        "Type must have a static logComponent member");
};
