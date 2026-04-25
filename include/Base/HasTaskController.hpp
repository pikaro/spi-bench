#pragma once

#include "Base/HasLifecycle.hpp"
#include "Concepts/Base.hpp"
#include "TaskController/Facade.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "Types/Error.hpp"

template <class Derived, typename ConfT = NoConfig> class HasTaskController {
  protected:
    explicit HasTaskController(Totem::TaskController::IRegistry &registryHooks)
        : _taskController(Derived::name, registryHooks, Derived::logComponent) {
    }

    ReturnCode _beginTaskController(Totem::TaskController::Config taskConfig) {
        return _taskController.begin(taskConfig);
    }
    ReturnCode _endTaskController() { return _taskController.end(); }

    Totem::TaskController::Controller _taskController;
};

template <class T> struct TaskControllerContract {
    static_assert(IsNamedEntity<T>, "Type must be a named entity");
    static_assert(
        requires { T::logComponent; },
        "Type must have a static logComponent member");
};
