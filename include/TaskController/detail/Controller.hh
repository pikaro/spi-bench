#pragma once

#include "Common.hh"

#include "Base/HasLifecycle.hh"
#include "Config.hh"
#include "Directory.hh"
#include "Runner.hh"
#include "TaskController/detail/Concepts.hh"
#include "TaskController/detail/PlatformSelect.hh"
#include "TaskController/detail/Types.hh"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

namespace Totem::TaskController::detail {

static constexpr ::platform::Tick defaultEndKillDelayMs = 100;

class Controller : public HasLifecycle<Controller, Config> {
    friend class HasLifecycle<Controller, Config>;
    friend struct LifecycleContract<Controller, Config>;

    using RunnerKeySnapshot = Directory::EntryKeySnapshot;

  public:
    using RunnerNameKey = Directory::EntryNameKey;

    static constexpr const char *name = "TaskController::Controller";

    Controller(const char *ownerName, RegistryHooks registryHooks)
        : _directory(ownerName), _registryHooks(registryHooks),
          _ownerName(ownerName) {
        ABORT_IF_NOT(_registryHooks.validate(),
                     "Invalid registry hooks for %s of %s", name, _ownerName);
        ABORT_IF_ERR(_registryHooks.registerController(ownerName, this),
                     "Failed to register %s of %s", name, _ownerName);
    }

    ~Controller() {
        ABORT_IF_ERR(_registryHooks.deregisterController(_ownerName),
                     "Failed to deregister %s of %s", name, _ownerName);
    }

    // Non-copyable and non-movable to avoid issues with being registered
    // through this pointer in the registry
    Controller(const Controller &) = delete;
    Controller &operator=(const Controller &) = delete;
    Controller(Controller &&) = delete;
    Controller &operator=(Controller &&) = delete;

    [[nodiscard]] const char *ownerName() const { return _ownerName; }

    [[nodiscard]] std::expected<RunnerNameKey, ReturnCode>
    addTask(const char *refName, TaskHooks hooks) {
        FAIL_IF_INACTIVE_UNEXPECTED("%s of %s", name, _ownerName);
        FAIL_IF_NULL(refName, std::unexpected(ERR(InvalidArgument)),
                     "%s of %s: Task name cannot be null", name, _ownerName);
        FAIL_IF(!hooks.validate(), std::unexpected(ERR(InvalidArgument)),
                "%s of %s: Invalid task hooks provided", name, _ownerName);
        return _directory.add(refName, hooks);
    }

    ReturnCode startTask(const char *refName, Config config) {
        FAIL_IF_NULL(refName, ERR(InvalidArgument),
                     "%s of %s: Task name cannot be null", name, _ownerName);
        return startTask(RunnerNameKey::fromCharPtr(refName), config);
    }

    ReturnCode startTask(RunnerNameKey ref, Config config) {
        FAIL_IF_INACTIVE_ERR("%s of %s", name, _ownerName);
        auto ret = _directory.withEntry(
            ref, [&config](RunnerEntry &entry) -> ReturnCode {
                FAIL_IF(entry.runner->hasEverStarted(), ERR(InvalidState),
                        "Task runner has already been started");
                FAIL_IF(entry.config.has_value(), ERR(InvalidState),
                        "Task runner already has a config, cannot start");
                FAIL_IF_ERR(entry.runner->start(config), ERR(OperationFailed),
                            "Failed to start task runner");
                entry.config = config;
                return OK();
            });
        FAIL_IF_ERR(ret, ret, "%s of %s: Task %s", name, _ownerName,
                    ref.name.data());
        return OK();
    }

    ReturnCode stopTask(const char *refName) {
        FAIL_IF_NULL(refName, ERR(InvalidArgument),
                     "%s of %s: Task name cannot be null", name, _ownerName);
        return stopTask(RunnerNameKey::fromCharPtr(refName));
    }

    ReturnCode stopTask(RunnerNameKey ref) {
        FAIL_IF_INACTIVE_ERR("%s of %s", name, _ownerName);
        return _directory.withEntry(
            ref, [this](RunnerEntry &entry) -> ReturnCode {
                FAIL_IF(
                    !entry.runner->hasEverStarted(), ERR(InvalidState),
                    "Cannot stop %s of %s: Task runner has not been started",
                    name, _ownerName);
                FAIL_IF(entry.runner->hasStopped(), ERR(InvalidState),
                        "Cannot stop %s of %s: Task runner has already been "
                        "stopped",
                        name, _ownerName);
                return entry.runner->requestStop();
            });
    }

    std::expected<uint8_t, ReturnCode> reap() {
        FAIL_IF_INACTIVE_UNEXPECTED("%s of %s", name, _ownerName);
        auto filter = [](const RunnerNameKey &,
                         const RunnerEntry &entry) -> bool {
            return entry.runner->hasStopped();
        };
        auto extractedResult = _directory.extract(filter);
        FAIL_IF(
            extractedResult.error(), std::unexpected(extractedResult.error()),
            "Failed to extract stopped runners for %s of %s", name, _ownerName);
        ReturnCode finalRet = OK();
        for (size_t i = 0; i < extractedResult.value().count; ++i) {
            auto &entry = extractedResult.value().entries[i];
            auto ret = _handleStoppedRunner(*this, entry);
            if (!ret.ok()) {
                _log_e("Error while handling stopped runner for %s of %s: %s",
                       name, _ownerName, ret.format());
                if (finalRet.ok()) {
                    finalRet = ret;
                }
            }
        }
        FAIL_IF_ERR(finalRet, std::unexpected(finalRet),
                    "Error while handling stopped runners for %s of %s", name,
                    _ownerName);
        return static_cast<uint8_t>(extractedResult.value().count);
    }

    std::expected<uint8_t, ReturnCode> terminate() {
        FAIL_IF_INACTIVE_UNEXPECTED("%s of %s", name, _ownerName);
        _disableRegistration();
        uint8_t stopCount = 0;
        auto ret =
            _directory.withAll([&stopCount](const RunnerNameKey &runnerName,
                                            RunnerEntry &entry) -> ReturnCode {
                if (!entry.runner->hasEverStarted()) {
                    _log_d("Skipping terminate initial runner %s",
                           runnerName.name.data());
                    return OK();
                }
                if (entry.runner->hasStopped()) {
                    _log_d("Skipping terminate already stopped runner %s",
                           runnerName.name.data());
                    return OK();
                }

                FAIL_IF_ERR(entry.runner->requestStop(), ERR(OperationFailed),
                            "Failed to request stop for runner");
                _log_i("Requested stop for runner %s", runnerName.name.data());
                ++stopCount;
                return OK();
            });
        FAIL_IF_ERR(ret, std::unexpected(ret),
                    "One or more errors occurred while requesting stop for "
                    "runners during termination");
        return stopCount;
    }

    void killAll() {
        FAIL_IF_INACTIVE_VOID("%s of %s", name, _ownerName);
        _disableRegistration();
        (void)_directory.withAll([](const RunnerNameKey &_,
                                    RunnerEntry &entry) -> ReturnCode {
            if (entry.runner->hasEverStarted() && !entry.runner->hasStopped()) {
                entry.runner->kill();
            }
            return OK();
        });
    }

    [[nodiscard]] std::expected<bool, ReturnCode> empty() const {
        FAIL_IF_INACTIVE_UNEXPECTED("%s of %s", name, _ownerName);
        return _directory.empty();
    }

    template <typename Fn>
        requires IsSnapshotHandler<Fn>
    ReturnCode forEachTaskSnapshot(Fn &&fun) {
        return _directory.forEachTaskSnapshot(fun);
    }

    [[nodiscard]] std::expected<uint8_t, ReturnCode> taskCount() const {
        return _directory.size();
    }

  private:
    void _disableRegistration() {
        _directory.disableRegistration();
        _permitRegistration.store(false, std::memory_order_release);
    }
    void _enableRegistration() {
        _directory.enableRegistration();
        _permitRegistration.store(true, std::memory_order_release);
    }

    ReturnCode _onBegin() {
        _enableRegistration();
        _log_i("Task controller for %s open", _ownerName);
        return OK();
    }

    ReturnCode _onEnd() {
        _log_i("Beginning shutdown of task controller for %s", _ownerName);
        auto retTerminate = terminate();
        if (!retTerminate) {
            _log_e("Error while requesting termination during shutdown: %s",
                   retTerminate.error().format());
        }
        if (retTerminate.value() == 0) {
            _log_i("Task controller for %s closed successfully", _ownerName);
            return OK();
        }

        auto now = ::platform::get_tick();
        auto endKillDeadline = now + defaultEndKillDelayMs;
        Platform::delay_task_until(&now, endKillDeadline);
        _log_i("Reaping stopped runners for %s", _ownerName);

        auto retReap = reap();
        if (!retReap) {
            _log_e("Error while performing shutdown work for %s: %s",
                   _ownerName, retReap.error().format());
            return retReap.error();
        }
        if (retReap.value() == retTerminate.value()) {
            _log_i("Task controller for %s closed successfully", _ownerName);
            return OK();
        }

        _log_w("Not all runners stopped during shutdown of %s", _ownerName);

        killAll();
        return ERR(OperationFailed);
    }

    static ReturnCode _handleStoppedRunner(Controller &self,
                                           RunnerEntry &entry) {
        auto result = entry.runner->stopResult();
        FAIL_IF(!result.has_value(), ERR(NotFound),
                "Stopped runner missing stop result");
        FAIL_IF(!entry.config.has_value(), ERR(InvalidState),
                "Stopped runner missing config");
        auto config = *entry.config;
        auto hooks = entry.hooks;
        auto ref = RunnerNameKey::fromCharPtr(config.name);
        auto ret = self._directory.remove(ref);
        FAIL_IF_ERR(ret, ret, "Failed to remove %s->%s from directory",
                    self._ownerName, ref.name.data());
        if (!result->isClean()) {
            _log_e("Task runner %s->%s stopped with error: "
                   "%s (reason code %d)",
                   self._ownerName, ref.name.data(), result->error.format(),
                   static_cast<int>(result->reason));
            if (!self._permitRegistration.load(std::memory_order_acquire)) {
                _log_i("Not auto-restarting task runner %s->%s "
                       "because termination has been requested",
                       self._ownerName, ref.name.data());
                return OK();
            }
            if (config.autoRestart) {
                _log_i("Auto-restarting task runner %s->%s", self._ownerName,
                       ref.name.data());
                auto restartResult = _restartTask(self, ref, hooks, config);
                if (!restartResult.ok()) {
                    _log_e("Failed to auto-restart task runner %s->%s: %s",
                           self._ownerName, ref.name.data(),
                           restartResult.format());
                }
            }
        } else {
            _log_i("Task runner %s->%s stopped successfully", self._ownerName,
                   ref.name.data());
        }
        return OK();
    }

    static ReturnCode _restartTask(Controller &self, RunnerNameKey ref,
                                   TaskHooks hooks, Config config) {
        auto addResult = self._directory.add(ref, hooks);
        FAIL_IF(
            !addResult, addResult.error(),
            "Failed to add task runner %s->%s back to directory for restart",
            self._ownerName, ref.name.data());
        auto startResult = self.startTask(ref, config);
        if (!startResult.ok()) {
            _log_e("Failed to restart task runner %s->%s: %s", self._ownerName,
                   ref.name.data(), startResult.format());
            (void)self._directory.remove(ref);
            return startResult;
        }
        return OK();
    }

    Directory _directory;
    std::atomic<bool> _permitRegistration{false};
    RegistryHooks _registryHooks;
    const char *const _ownerName;

    using DefaultError = CoreError;
};

inline constexpr LifecycleContract<Controller, Config> _controller_lifecycle;

} // namespace Totem::TaskController::detail
