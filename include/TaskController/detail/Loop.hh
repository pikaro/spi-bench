#pragma once

#include "Macros/Facade.hh"
#include "TaskController/detail/Config.hh"
#include "TaskController/detail/ScopedWatchdog.hh"
#include "TaskController/detail/SignalHandler.hh"
#include "TaskController/detail/StateManager.hh"
#include "TaskController/detail/StepScheduler.hh"
#include "TaskController/detail/Types.hh"
#include "Types/Error.hh"

namespace Totem::TaskController::detail {

class Loop {
  public:
    struct Context {
        Config config;
        TaskHooks &hooks;
        StateManager &stateManager;
    };

    explicit Loop(const Context &context)
        : _config(context.config), _hooks(context.hooks),
          _stateManager(context.stateManager) {}

    Result run() {
        ScopedWatchdog wdt(_config.useWdt);
        StepScheduler scheduler(_config.intervalMs, _config.noCatchup);

        if (!_stateManager.tryEnterRunning()) {
            _log_e("Runner %s: Invalid state transition to Running",
                   _config.name);
            _stateManager.enterStopped();
            return Result{.reason = ExitReason::InvalidStateTransition,
                          .error = ERR(OperationFailed)};
        }

        if (auto result = _hooks.onStart(); !result.ok()) {
            _log_e("Runner %s: onStart hook failed", _config.name);
            (void)_shutdown();
            return Result{.reason = ExitReason::StartHookFailed,
                          .error = result};
        }

        Result runResult{.reason = ExitReason::StopRequested};
        while (true) {
            if (auto result = _step(scheduler); !result.ok()) {
                _log_e("Runner %s: onStep hook failed", _config.name);
                runResult =
                    Result{.reason = ExitReason::StepFailed, .error = result};
                break;
            }

            if (auto result = _signalHandler.handleSignal();
                !result.continueRunning) {
                if (!result.ok()) {
                    _log_e("Runner %s: signal handling failed", _config.name);
                    runResult = Result{.reason = ExitReason::SignalFailed,
                                       .error = result.error};
                } else {
                    _log_i("Runner %s: signal handling requested to stop",
                           _config.name);
                    runResult = Result{.reason = ExitReason::StopRequested};
                }
                break;
            }

            wdt.reset();
        }

        auto stopResult = _shutdown();
        if (!stopResult.ok()) {
            if (runResult.isClean()) {
                runResult = Result{.reason = ExitReason::StopHookFailed,
                                   .error = stopResult};
            } else {
                _log_e("Runner %s: onStop hook failed with error %s while "
                       "already exiting with error %s",
                       _config.name, stopResult.format(),
                       runResult.error.format());
            }
        }

        return runResult;
    }

  private:
    ReturnCode _step(StepScheduler &scheduler) {
        scheduler.wait();
        return _hooks.onStep();
    }

    ReturnCode _shutdown() {
        _stateManager.enterStopping();
        auto stopResult = _hooks.onStop();
        _log_i("Runner %s stopped", _config.name);
        _stateManager.enterStopped();
        return stopResult;
    }

    Config _config;
    TaskHooks &_hooks;
    StateManager &_stateManager;
    SignalHandler _signalHandler{_hooks, _config};

    using DefaultError = CoreError;
};

} // namespace Totem::TaskController::detail
