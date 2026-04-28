#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Mutex/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "TaskController/detail/Loop.hpp"
#include "TaskController/detail/PlatformSelect.hpp"
#include "TaskController/detail/StateManager.hpp"
#include "TaskMetrics.hpp"
#include "Types/Error.hpp"
#include "Types/Signal.hpp"
#include <atomic>
#include <cstdint>
#include <expected>
#include <optional>

namespace Totem::TaskController::detail {

class Runner {
  public:
    static constexpr const char *name = "TaskController::Runner";

    explicit Runner(TaskHooks hooks, IRegistry &registry)
        : _hooks(hooks), _registry(registry) {}

    [[nodiscard]] const Config &config() const { return _config; }

    ReturnCode start(Config cfg) {
        _config = cfg;
        _metricsGroupDesc.name = cfg.name;
        _metrics = TaskMetrics::create(_metricsGroupDesc);

        if (!_stateManager.tryStart()) {
            _log_e("Failed to start runner %s: Invalid state", _config.name);
            return ERR(OperationFailed);
        }

        FAIL_IF_NOT(_createTask(), ERR(OperationFailed),
                    "Task creation failed");

        _hasEverStarted.store(true, std::memory_order_release);
        return OK();
    }

    ReturnCode requestStop() { return signal(Signal::Stop); }

    ReturnCode signal(Signal signal = Signal::Ping) {
        Mutex::ScopedSpinlockGuard guard{_lock};
        FAIL_IF_NULL(_handle, ERR(OperationFailed),
                     "Cannot signal unstarted runner");
        auto result = Platform::signal_task(_handle, signal);
        FAIL_IF_ERR(result, ERR(OperationFailed), "Failed to signal task");
        return OK();
    }

    void signalFromIsr(Signal signal = Signal::Ping) {
        auto *handle = _isrSignalHandle.load(std::memory_order_acquire);
        if (handle == nullptr) {
            return;
        }
        Platform::signal_task_from_isr(handle, signal);
    }

    void kill() {
        TaskHandle handle = nullptr;
        {
            Mutex::ScopedSpinlockGuard guard{_lock};
            FAIL_IF_NULL_VOID(_handle, "Cannot kill unstarted runner");
            handle = _handle;
            _handle = nullptr;
            _isrSignalHandle.store(nullptr, std::memory_order_release);
        }
        _stateManager.enterStopping();
        _stopResult =
            Result{.reason = ExitReason::Killed, .error = ERR(Unexpected)};
        _hasStopResult.store(true, std::memory_order_release);
        _stateManager.enterStopped();

        auto deregisterResult = _registry.deregisterManagedTaskHandle(
            reinterpret_cast<uintptr_t>(handle));
        if (!deregisterResult.ok()) {
            _log_e("Failed to deregister killed runner handle for %s: " ERR_FMT,
                   _config.name, ERR_ARG(deregisterResult));
        }
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
        uintptr_t nativeHandle = 0;
        {
            Mutex::ScopedSpinlockGuard guard{self->_lock};
            if (self->_handle != nullptr) {
                nativeHandle = reinterpret_cast<uintptr_t>(self->_handle);
            }
        }

        auto loop = Loop({.config = self->_config,
                          .hooks = self->_hooks,
                          .stateManager = self->_stateManager,
                          .metrics = self->_metrics.value()});
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
            self->_isrSignalHandle.store(nullptr, std::memory_order_release);
        }
        if (nativeHandle != 0) {
            auto deregisterResult =
                self->_registry.deregisterManagedTaskHandle(nativeHandle);
            if (!deregisterResult.ok()) {
                _log_e("Failed to deregister runner handle for %s: " ERR_FMT,
                       self->_config.name, ERR_ARG(deregisterResult));
            }
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
            _isrSignalHandle.store(result.handle, std::memory_order_release);
        }
        auto registerResult = _registry.registerManagedTaskHandle(
            reinterpret_cast<uintptr_t>(result.handle));
        if (!registerResult.ok()) {
            {
                Mutex::ScopedSpinlockGuard guard{_lock};
                _handle = nullptr;
                _isrSignalHandle.store(nullptr, std::memory_order_release);
            }
            Platform::kill_task(result.handle);
            _stateManager.enterStopped();
            FAIL(registerResult,
                 "Failed to register managed task handle for runner %s",
                 _config.name);
        }

        _log_i("Started runner %s", _config.name);

        return OK();
    }

    TaskHandle _handle = nullptr;
    std::atomic<TaskHandle> _isrSignalHandle{nullptr};
    StateManager _stateManager;

    Config _config;
    MetricsBackend::MetricGroupDesc _metricsGroupDesc{
        .name = nullptr,
    };
    std::optional<TaskMetrics> _metrics = std::nullopt;

    TaskRuntimeSnapshot _runtimeSnapshot{};
    TaskPlatformSnapshot _platformSnapshot{};

    TaskHooks _hooks;
    IRegistry &_registry;

    std::optional<Result> _stopResult = std::nullopt;
    std::atomic<bool> _hasEverStarted = false;
    std::atomic<bool> _hasStopResult = false;
    std::atomic<bool> _hasSnapshot = false;
    ::platform::Spinlock _lock = ::platform::create_spinlock();
};

} // namespace Totem::TaskController::detail
