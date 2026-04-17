#pragma once

#include "Base/HasLifecycle.hh"
#include "Concepts/Base.hh"
#include "TaskController/Facade.hh"
#include "TaskController/Interfaces/Config.hh"
#include "TaskController/Interfaces/RegistryHooks.hh"
#include "Types/Error.hh"

template <class Derived, typename ConfT = NoConfig> class HasTaskController {
  protected:
    explicit HasTaskController(
        Totem::TaskController::RegistryHooks registryHooks)
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
