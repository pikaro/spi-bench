#pragma once

#include "Base/HasLifecycle.hh"
#include "Concepts/Base.hh"
#include "TaskController/Facade.hh"
#include "Types/Error.hh"

namespace Totem::Core {

template <class Derived, typename ConfT = NoConfig> class HasTaskController {
  protected:
    explicit HasTaskController(TaskController::RegistryHooks registryHooks)
        : _taskController(Derived::name, registryHooks) {}

    ReturnCode _beginTaskController(Totem::TaskController::Config taskConfig) {
        return _taskController.begin(taskConfig);
    }
    ReturnCode _endTaskController() { return _taskController.end(); }

    TaskController::Controller _taskController;
};

template <class T> struct TaskControllerContract {
    static_assert(IsNamedEntity<T>, "Type must be a named entity");
};

} // namespace Totem::Core

using Totem::Core::HasTaskController;
using Totem::Core::TaskControllerContract;
