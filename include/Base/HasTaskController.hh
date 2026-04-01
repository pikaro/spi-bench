#pragma once

#include "Concepts/Base.hh"
#include "TaskController/Facade.hh"
#include "Types/Error.hh"

namespace Totem::Core {

template <class Derived, typename ConfT> class HasTaskController {
  protected:
    explicit HasTaskController(TaskController::RegistryHooks registryHooks)
        : _taskController(Derived::name, registryHooks) {}

    ReturnCode _beginTaskController() {
        return _taskController.begin(ConfT::task);
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
