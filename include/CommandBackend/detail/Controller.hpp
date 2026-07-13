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
#include "Types/Signal.hpp"
#include <array>
#include <optional>

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
    [[nodiscard]] const ICommandCatalog &catalog() const { return _store; }

    ReturnCode wake(Signal signal = Signal::Ping) {
        FAIL_IF(!_taskKey.has_value(), ERR(InvalidState),
                "Cannot wake command task before task registration");
        return _taskController.signalTask(*_taskKey, signal);
    }

    static ReturnCode wake(void *controller, Signal signal = Signal::Ping) {
        auto *self = static_cast<Controller *>(controller);
        return self->wake(signal);
    }

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
        DEFAULT_TASK();
        _taskKey = task;
        START_TASK();

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

        return Dispatcher::dispatch(*commandEntry.second, commandEntry.first,
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
            if (!dispatchResult.ok()) {
                _log_w("Command dispatch failed for transport " SV_FMT
                       ": " ERR_FMT,
                       SV_ARG(transport->displayName()),
                       ERR_ARG(dispatchResult));
                continue;
            }
        }
        return OK();
    }

    ReturnCode _onEnd() {
        _taskKey.reset();
        FAIL_IF_ERR_FWD(_taskController.end(),
                        "Failed to end task controller for %s", name);
        return OK();
    }

    static ReturnCode _onTaskNotify(Signal /*signal*/) { return OK(); }

    Store _store;
    Registrar _registrar;
    std::array<ITransport *, CommandConfig::maxTransports> _transports{nullptr};
    std::optional<TaskController::RunnerKey> _taskKey;

    static constexpr LogComponent logComponent =
        Totem::CommandBackend::detail::logComponent;
};

inline constexpr LifecycleContract<Controller, Config> _controller_lifecycle;
inline constexpr TaskControllerContract<Controller> _controller_task_controller;
inline constexpr TaskController::TaskHooks::Contract<Controller>
    _controller_task_hook;

} // namespace Totem::CommandBackend::detail
