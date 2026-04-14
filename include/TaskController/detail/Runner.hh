#pragma once

#include "Macros/Facade.hh"
#include "Mutex/Facade.hh"
#include "Platform/PlatformSelect.hh"
#include "TaskController/Interfaces/Config.hh"
#include "TaskController/Interfaces/TaskHooks.hh"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include "TaskController/detail/Loop.hh"
#include "TaskController/detail/PlatformSelect.hh"
#include "TaskController/detail/StateManager.hh"
#include "Types/Error.hh"
#include "Types/Signal.hh"
#include <atomic>
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::TaskController::detail {

class Runner {
  public:
    static constexpr const char *name = "TaskController::Runner";

    explicit Runner(TaskHooks hooks) : _hooks(hooks) {}

    [[nodiscard]] const Config &config() const { return _config; }

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
        Mutex::ScopedSpinlockGuard guard{_lock};
        FAIL_IF_NULL(_handle, ERR(OperationFailed),
                     "Cannot request stop on unstarted runner");
        auto result = Platform::signal_task(_handle, Signal::Stop);
        FAIL_IF_ERR(result, ERR(OperationFailed),
                    "Failed to signal task to stop");
        return OK();
    }

    void kill() {
        TaskHandle handle = nullptr;
        {
            Mutex::ScopedSpinlockGuard guard{_lock};
            FAIL_IF_NULL_VOID(_handle, "Cannot kill unstarted runner");
            handle = _handle;
            _handle = nullptr;
        }
        _stateManager.enterStopping();
        _stopResult =
            Result{.reason = ExitReason::Killed, .error = ERR(Unexpected)};
        _hasStopResult.store(true, std::memory_order_release);
        _stateManager.enterStopped();

        Platform::kill_task(handle);
        _log_w("Killed runner %s", _config.name);
    }

    [[nodiscard]] bool hasEverStarted() const {
        return _hasEverStarted.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool hasStopped() const {
        return _hasStopResult.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::optional<Result> stopResult() const {
        if (!hasStopped()) {
            _log_w(
                "Stop result requested for runner %s which has not stopped yet",
                _config.name);
            return std::nullopt;
        }
        return _stopResult;
    }

    [[nodiscard]] std::expected<Totem::TaskController::TaskRuntimeSnapshot,
                                ReturnCode>
    snapshot() const {
        if (!_hasSnapshot.load(std::memory_order_acquire)) {
            return std::unexpected(ERR(NotFound));
        }
        return _runtimeSnapshot;
    }

    ReturnCode takeSnapshot() {
        auto timestamp = ::platform::get_time();
        auto timestampDelta = timestamp - _runtimeSnapshot.timestamp;

        std::expected<TaskPlatformSnapshot, ReturnCode> platformResult =
            std::unexpected(ERR(NotFound));
        uintptr_t nativeHandle = 0;
        bool hasHandle = false;
        {
            Mutex::ScopedSpinlockGuard guard{_lock};
            if (_handle != nullptr) {
                hasHandle = true;
                nativeHandle = reinterpret_cast<uintptr_t>(_handle);
                platformResult = Platform::get_snapshot(_handle);
            }
        }

        if (!hasHandle) {
            auto hadSnapshot = _hasSnapshot.load(std::memory_order_acquire);
            _runtimeSnapshot = Totem::TaskController::TaskRuntimeSnapshot{
                .timestamp = timestamp,
                .timestampDelta = timestampDelta,
                .name = _config.name,
                .sourceName = {},
                .nativeHandle = 0,
                .hasEverStarted =
                    _hasEverStarted.load(std::memory_order_acquire),
                .lastStopResult = hasStopped() ? _stopResult : std::nullopt,
                .state = _stateManager.state(),
                .platformState = hadSnapshot ? _platformSnapshot.state
                                             : PlatformState::Suspended,
                .coreId = hadSnapshot ? _platformSnapshot.coreId
                                      : static_cast<int8_t>(-1),
                .currentPriority =
                    hadSnapshot ? _platformSnapshot.priority : _config.priority,
                .runTimeTotalPct =
                    hadSnapshot ? _runtimeSnapshot.runTimeTotalPct : 0.0F,
                .runTimeDeltaPct = 0.0F,
                .stackLowestFree = hadSnapshot
                                       ? _runtimeSnapshot.stackLowestFree
                                       : _config.stackSize,
                .stackUsedPct =
                    hadSnapshot ? _runtimeSnapshot.stackUsedPct : 0.0F,
                .config = &_config,
            };
            _hasSnapshot.store(true, std::memory_order_release);
            return OK();
        }
        if (!platformResult) {
            FAIL(platformResult.error(),
                 "Failed to load platform state for Runner %s", _config.name);
        };

        float runTimeTotalPct = 0.0F;
        float runTimeDeltaPct = 0.0F;

        if (timestamp > 0) {
            runTimeTotalPct = 100.0F *
                              static_cast<float>(platformResult->runTimeMs) /
                              static_cast<float>(timestamp);
        }

        if (timestamp > 0 && timestampDelta > 0) {
            runTimeDeltaPct = 100.0F *
                              static_cast<float>(platformResult->runTimeMs -
                                                 _platformSnapshot.runTimeMs) /
                              static_cast<float>(timestampDelta);
        }

        _runtimeSnapshot = Totem::TaskController::TaskRuntimeSnapshot{
            .timestamp = timestamp,
            .timestampDelta = timestampDelta,
            .name = _config.name,
            .sourceName = {},
            .nativeHandle = nativeHandle,
            .hasEverStarted = _hasEverStarted.load(std::memory_order_acquire),
            .lastStopResult = hasStopped() ? _stopResult : std::nullopt,
            .state = _stateManager.state(),
            .platformState = platformResult->state,
            .coreId = platformResult->coreId,
            .currentPriority = platformResult->priority,
            .runTimeTotalPct = runTimeTotalPct,
            .runTimeDeltaPct = runTimeDeltaPct,
            .stackLowestFree = platformResult->stackLowestFree,
            .stackUsedPct =
                100.0F *
                static_cast<float>(_config.stackSize -
                                   platformResult->stackLowestFree) /
                static_cast<float>(_config.stackSize),
            .config = &_config,
        };

        _platformSnapshot = *platformResult;
        _hasSnapshot.store(true, std::memory_order_release);
        return OK();
    }

  private:
    static void _run(void *runner) {
        auto *self = static_cast<Runner *>(runner);

        auto loop = Loop({.config = self->_config,
                          .hooks = self->_hooks,
                          .stateManager = self->_stateManager});
        auto result = loop.run();
        if (result.reason == ExitReason::StopRequested) {
            _log_i("Runner %s exited successfully", self->_config.name);
        } else {
            _log_e("Runner %s exited with error: " ERR_FMT " (reason code %d)",
                   self->_config.name, ERR_ARG(result.error),
                   static_cast<uint8_t>(result.reason));
        }
        // Publish the stopped state only after the task has finished touching
        // Runner storage. Reap may destroy the Runner as soon as hasStopped()
        // becomes visible.
        {
            Mutex::ScopedSpinlockGuard guard{self->_lock};
            self->_handle = nullptr;
        }
        self->_stopResult = result;
        self->_hasStopResult.store(true, std::memory_order_release);
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

        {
            Mutex::ScopedSpinlockGuard guard{_lock};
            _handle = result.handle;
        }

        _log_i("Started runner %s", _config.name);

        return OK();
    }

    TaskHandle _handle = nullptr;
    StateManager _stateManager;
    Config _config;
    TaskRuntimeSnapshot _runtimeSnapshot{};
    TaskPlatformSnapshot _platformSnapshot{};
    TaskHooks _hooks;
    std::optional<Result> _stopResult = std::nullopt;
    std::atomic<bool> _hasEverStarted = false;
    std::atomic<bool> _hasStopResult = false;
    std::atomic<bool> _hasSnapshot = false;
    ::platform::Spinlock _lock = ::platform::create_spinlock();
};

} // namespace Totem::TaskController::detail
