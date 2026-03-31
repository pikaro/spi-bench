#pragma once

#include "TaskController/detail/Config.hh"
#include "TaskController/detail/Loop.hh"
#include "TaskController/detail/PlatformSelect.hh"
#include "TaskController/detail/StateManager.hh"
#include "TaskController/detail/Types.hh"
#include <optional>

namespace Totem::TaskController::detail {

class Runner {
  public:
    static constexpr const char *name = "TaskController::Runner";

    explicit Runner(TaskHooks hooks) : _hooks(hooks) {}

    ReturnCode start(Config cfg) {
        _config = cfg;

        if (!_stateManager.tryStart()) {
            _log_e("Failed to start runner %s: Invalid state", _config.name);
            return ERR(OperationFailed);
        }

        FAIL_IF_NOT(_createTask(), ERR(OperationFailed),
                    "Task creation failed");

        _hasEverStarted.store(true, std::memory_order_release);
        return OK();
    }

    ReturnCode requestStop() {
        FAIL_IF_NULL(_handle, ERR(OperationFailed),
                     "Cannot request stop on unstarted runner");
        FAIL_IF_NOT_NULL(_handle, ERR(OperationFailed),
                         "Cannot request stop on unstarted runner");
        auto result = Platform::signal_task(_handle, Signal::Stop);
        FAIL_IF_ERR(result, ERR(OperationFailed),
                    "Failed to signal task to stop");
        return OK();
    }

    void kill() {
        FAIL_IF_NULL_VOID(_handle, "Cannot kill unstarted runner");
        _stateManager.enterStopping();
        _stopResult = Loop::Result{.reason = Loop::ExitReason::Killed,
                                   .error = ERR(Unexpected)};
        _hasStopResult.store(true, std::memory_order_release);
        _stateManager.enterStopped();
        auto *handle = _handle;
        _handle = nullptr;

        Platform::kill_task(handle);
        _log_w("Killed runner %s", _config.name);
    }

    [[nodiscard]] bool hasEverStarted() const {
        return _hasEverStarted.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool hasStopped() const {
        return _hasStopResult.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::optional<Loop::Result> stopResult() const {
        if (!hasStopped()) {
            _log_w(
                "Stop result requested for runner %s which has not stopped yet",
                _config.name);
            return std::nullopt;
        }
        return _stopResult;
    }

  private:
    static void _run(void *opaque) {
        auto *self = static_cast<Runner *>(opaque);

        auto loop = Loop({.config = self->_config,
                          .hooks = self->_hooks,
                          .stateManager = self->_stateManager});
        auto result = loop.run();
        if (result.reason == Loop::ExitReason::StopRequested) {
            _log_i("Runner %s exited successfully", self->_config.name);
        } else {
            _log_e("Runner %s exited with error: %s (reason code %d)",
                   self->_config.name, result.error.format(),
                   static_cast<uint8_t>(result.reason));
        }
        self->_stopResult = result;
        self->_hasStopResult.store(true, std::memory_order_release);
        self->_handle = nullptr;
        Platform::delete_current_task();
    }

    ReturnCode _createTask() {
        _log_d("Creating task %s with stack size %lu, priority %d, on core %d",
               _config.name, _config.stackSize, _config.priority,
               static_cast<int>(_config.core.kind ==
                                        Config::CorePreference::Kind::Any
                                    ? -1
                                    : _config.core.core));

        auto result = Platform::create_task(_config, _run, this);
        if (!result.ok) {
            _log_e("Failed to create task for runner %s", _config.name);
            _stateManager.enterStopped();
            FAIL(ERR(OperationFailed), "Task creation failed");
        }

        _handle = result.handle;

        _log_i("Started runner %s", _config.name);

        return OK();
    }

    TaskHandle _handle;
    StateManager _stateManager;
    Config _config;
    TaskHooks _hooks;
    std::optional<Loop::Result> _stopResult = std::nullopt;
    std::atomic<bool> _hasEverStarted = false;
    std::atomic<bool> _hasStopResult = false;

    using DefaultError = CoreError;
};

} // namespace Totem::TaskController::detail
