#pragma once

#include "Macros/Facade.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "TaskController/detail/ScopedWatchdog.hpp"
#include "TaskController/detail/SignalHandler.hpp"
#include "TaskController/detail/StateManager.hpp"
#include "TaskController/detail/StepScheduler.hpp"
#include "TaskController/detail/TaskMetrics.hpp"
#include "Types/Error.hpp"
#include <expected>

namespace Totem::TaskController::detail {

class Loop {
  public:
    struct Context {
        Config config;
        TaskHooks &hooks;
        StateManager &stateManager;
        TaskMetrics &metrics;
    };

    explicit Loop(const Context &context)
        : _config(context.config), _hooks(context.hooks),
          _stateManager(context.stateManager), _metrics(context.metrics) {}

    Result run() {
        auto initResult = _init();
        if (!initResult) {
            _log_e("Runner %s: initialization failed", _config.name);
            return initResult.error();
        }

        auto loopResult = _loop();

        auto stopResult = _shutdown();
        if (!_metrics.stopped()) {
            _log_w("Failed to record stopped metric for runner %s: " ERR_FMT,
                   _config.name, ERR_ARG(stopResult));
        }

        if (!stopResult.ok()) {
            if (loopResult.isClean()) {
                return Result{.reason = ExitReason::StopHookFailed,
                              .error = stopResult};
            }
            _log_e("Runner %s: onStop hook failed with error " ERR_FMT
                   " while already exiting with error " ERR_FMT,
                   _config.name, ERR_ARG(stopResult),
                   ERR_ARG(loopResult.error));
        }

        return loopResult;
    }

  private:
    struct StepResult {
        ReturnCode error{OK()};
        bool stopRequested = false;
        bool signalFailure = false;
    };

    std::expected<void, Result> _init() {
        if (!_stateManager.tryEnterRunning()) {
            _log_e("Runner %s: Invalid state transition to Running",
                   _config.name);
            _stateManager.enterStopped();
            return std::unexpected(
                Result{.reason = ExitReason::InvalidStateTransition,
                       .error = ERR(OperationFailed)});
        }

        if (auto result = _hooks.onStart(); !result.ok()) {
            _log_e("Runner %s: onStart hook failed", _config.name);
            (void)_shutdown();
            return std::unexpected(
                Result{.reason = ExitReason::StartHookFailed, .error = result});
        }

        _log_i("Runner %s started", _config.name);

        if (!_metrics.started()) {
            _log_w("Failed to record started metric for runner %s",
                   _config.name);
        }

        return {};
    }

    Result _loop() {
        ScopedWatchdog wdt(_config.useWdt);
        StepScheduler scheduler(_config.intervalMs, _config.noCatchup);

        while (true) {
            auto notifyStepResult = StepResult{};
            auto stepResult = OK();
            if (_config.useNotify) {
                notifyStepResult = _stepOnNotify(scheduler);
                stepResult = notifyStepResult.error;
            } else {
                stepResult = _step(scheduler);
            }

            if (!stepResult.ok() || notifyStepResult.stopRequested) {
                if (_config.useNotify && notifyStepResult.stopRequested) {
                    _log_i("Runner %s: signal handling requested to stop",
                           _config.name);
                    return Result{.reason = ExitReason::StopRequested};
                }

                if (_config.useNotify && notifyStepResult.signalFailure) {
                    _log_e("Runner %s: signal handling failed", _config.name);
                    return Result{.reason = ExitReason::SignalFailed,
                                  .error = stepResult};
                }
                _log_e("Runner %s: onStep hook failed", _config.name);
                return Result{.reason = ExitReason::StepFailed,
                              .error = stepResult};
            }

            if (!_config.useNotify) {
                if (auto result = _signalHandler.handleSignal();
                    !result.continueRunning) {
                    if (!result.ok()) {
                        _log_e("Runner %s: signal handling failed",
                               _config.name);
                        return Result{.reason = ExitReason::SignalFailed,
                                      .error = result.error};
                    }
                    _log_i("Runner %s: signal handling requested to stop",
                           _config.name);
                    return Result{.reason = ExitReason::StopRequested};
                }
            }

            wdt.reset();
        }
    }

    ReturnCode _step(StepScheduler &scheduler) {
        scheduler.wait();
        return _hooks.onStep();
    }

    StepResult _stepOnNotify(StepScheduler &scheduler) {
        auto signalResult = _signalHandler.handleSignal(
            scheduler.timeoutMsUntilNextStep(), true);
        if (!signalResult.continueRunning) {
            return StepResult{
                .error = signalResult.error,
                .stopRequested = signalResult.error.ok(),
                .signalFailure = !signalResult.error.ok(),
            };
        }
        if (signalResult.timeout) {
            scheduler.markPeriodicStep();
        }
        return StepResult{
            .error = _hooks.onStep(),
            .stopRequested = false,
            .signalFailure = false,
        };
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
    TaskMetrics &_metrics;
};

} // namespace Totem::TaskController::detail
