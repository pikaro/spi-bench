#pragma once

#include "Base/HasLifecycle.hpp"
#include "Base/HasTaskController.hpp"
#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "CommandBackend/Interfaces/Config.hpp"
#include "CommandBackend/Interfaces/ITransport.hpp"
#include "CommandBackend/Interfaces/Types.hpp"
#include "CommandBackend/detail/Dispatcher.hpp"
#include "CommandBackend/detail/Registrar.hpp"
#include "CommandBackend/detail/Store.hpp"
#include "CommandBackend/detail/Types.hpp" // IWYU pragma: keep
#include "Macros/Facade.hpp"
#include "StaticConfig/Command.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "Types/Error.hpp"
#include <array>

namespace Totem::CommandBackend::detail {

class Controller : public HasLifecycle<Controller, Config>,
                   HasTaskController<Controller, Config> {
    friend class HasLifecycle<Controller, Config>;
    friend struct LifecycleContract<Controller, Config>;

    friend TaskController::TaskHooks;
    friend class HasTaskController<Controller, Config>;
    friend struct TaskControllerContract<Controller>;
    friend struct TaskController::TaskHooks::Contract<Controller>;

  public:
    DELETE_COPY(Controller)
    DELETE_MOVE(Controller)

    static constexpr const char *name = "CommandCtrl";

    explicit Controller(TaskController::IRegistry &registry)
        : HasTaskController(registry), _registrar(_store) {}

    Registrar &registrar() { return _registrar; }

    ReturnCode addTransport(ITransport &transport) {
        for (auto &slot : _transports) {
            if (slot == nullptr) {
                slot = &transport;
                return OK();
            }
        }
        FAIL(ERR(OutOfMemory), "No available slots for command transport in %s",
             name);
    }

  private:
    ReturnCode _onBegin() {
        auto taskHooks = TaskController::TaskHooks::bind(*this);

        FAIL_IF_ERR_FWD(_beginTaskController(config().task),
                        "Failed to begin task controller for %s", name);

        auto taskAddResult = _taskController.addTask("CommandTask", taskHooks);
        FAIL_IF_UNEXPECTED(task, taskAddResult, taskAddResult.error(),
                           "Failed to add task to task controller for %s",
                           name);

        FAIL_IF_ERR_FWD(_taskController.startTask(task, config().task),
                        "Failed to start task in task controller for %s", name);

        return OK();
    }

    ReturnCode _dispatch(CommandDesc::Tokens commandLine) {
        if (commandLine.size() == 0) {
            _log_e("No command provided");
            return ERR(InvalidArgument);
        }

        FAIL_IF_UNEXPECTED_FWD(
            commandEntry,
            _store.get(CommandNameKey::fromStringView(commandLine[0])),
            "Command " SV_FMT " not found in store", SV_ARG(commandLine[0]));

        return Dispatcher::dispatch(commandEntry.second, commandEntry.first,
                                    commandLine.subspan(1));
    }

    ReturnCode _onTaskStep() {
        for (const auto &transport : _transports) {
            if (transport == nullptr) {
                continue;
            }
            auto pollResult = transport->poll();
            if (!pollResult) {
                if (pollResult.error() == ERR(CoreError, NotFound)) {
                    continue;
                }
                if (pollResult.error().domain == ErrorDomain::Command) {
                    _log_w("Command error from transport " SV_FMT ": " ERR_FMT,
                           SV_ARG(transport->displayName()),
                           ERR_ARG(pollResult.error()));
                    continue;
                }
                FAIL(pollResult.error(),
                     "Error from command transport " SV_FMT ": " ERR_FMT,
                     SV_ARG(transport->displayName()),
                     ERR_ARG(pollResult.error()));
            }

            auto dispatchResult = _dispatch(*pollResult);
            if (dispatchResult == ERR(CoreError, NotFound)) {
                _log_w("Command not found for input from transport " SV_FMT,
                       SV_ARG(transport->displayName()));
                continue;
            }
            FAIL_IF_ERR_FWD(dispatchResult,
                            "Failed to dispatch command from transport " SV_FMT,
                            SV_ARG(transport->displayName()));
        }
        return OK();
    }

    ReturnCode _onEnd() {
        FAIL_IF_ERR_FWD(_taskController.end(),
                        "Failed to end task controller for %s", name);
        return OK();
    }

    Store _store;
    Registrar _registrar;
    std::array<ITransport *, CommandConfig::maxTransports> _transports{nullptr};

    static constexpr LogComponent logComponent =
        Totem::CommandBackend::detail::logComponent;
};

inline constexpr LifecycleContract<Controller, Config> _controller_lifecycle;
inline constexpr TaskControllerContract<Controller> _controller_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Controller>
    _controller_task_hook;

} // namespace Totem::CommandBackend::detail
