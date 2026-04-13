#pragma once

#include "Base/HasLifecycle.hh"
#include "Base/HasTaskController.hh"
#include "CommandBackend/Interfaces/Transport.hh"
#include "CommandBackend/detail/Config.hh"
#include "CommandBackend/detail/Dispatcher.hh"
#include "CommandBackend/detail/Registrar.hh"
#include "CommandBackend/detail/Store.hh"
#include "Macros/Facade.hh"
#include "StaticConfig/Command.hh"
#include "TaskController/Interfaces/RegistryHooks.hh"
#include "TaskController/Interfaces/TaskHooks.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <array>

namespace Totem::CommandBackend::detail {

class Controller : public HasLifecycle<Controller, Config>,
                   HasTaskController<Controller, Config> {
    friend class HasLifecycle<Controller>;
    friend struct LifecycleContract<Controller>;

    friend TaskController::TaskHooks;
    friend struct TaskController::TaskHooks::Contract<Controller>;

  public:
    DELETE_COPY(Controller)
    DELETE_MOVE(Controller)

    static constexpr const char *name = "CommandCtrl";

    explicit Controller(TaskController::RegistryHooks registryHooks)
        : HasTaskController(registryHooks), _registrar(_store) {}

    Registrar &registrar() { return _registrar; }

    ReturnCode addTransport(const Transport &transport) {
        FAIL_IF(!transport.validate(), ERR(InvalidArgument),
                "Invalid command transport provided to %s", name);
        for (auto &slot : _transports) {
            if (!slot.validate()) {
                slot = transport;
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

        const char *commandName = commandLine[0].data();
        FAIL_IF_UNEXPECTED_FWD(
            commandEntry,
            _store.get(Store::CommandNameKey::fromCharPtr(commandName)),
            "Command %s not found in store", commandName);

        return Dispatcher::dispatch(commandEntry, commandLine.subspan(1));
    }

    ReturnCode _onTaskStep() {
        for (const auto &transport : _transports) {
            if (!transport.validate()) {
                continue;
            }
            auto pollResult = transport.poll();
            if (!pollResult) {
                if (pollResult.error() == ERR(CoreError, NotFound)) {
                    continue;
                }
                if (pollResult.error().domain == ErrorDomain::Command) {
                    _log_w("Command error from transport %s: " ERR_FMT,
                           transport.name, ERR_ARG(pollResult.error()));
                    continue;
                }
                FAIL(pollResult.error(), "Error from command transport %s",
                     transport.name);
            }

            auto dispatchResult = _dispatch(*pollResult);
            if (dispatchResult == ERR(CoreError, NotFound)) {
                _log_w("Command not found for input from transport %s",
                       transport.name);
                continue;
                FAIL_IF_ERR_FWD(dispatchResult,
                                "Failed to dispatch command from transport %s",
                                transport.name);
            }
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
    std::array<Transport, CommandConfig::maxTransports> _transports;

    using DefaultError = CoreError;
};

inline constexpr LifecycleContract<Controller> _controller_lifecycle;
inline constexpr TaskControllerContract<Controller> _controller_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Controller>
    _controller_task_hook;

} // namespace Totem::CommandBackend::detail
