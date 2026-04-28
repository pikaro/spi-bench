#pragma once

#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Mutex/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/TaskRegistry.hpp"
#include "TaskController/Facade.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "TaskControllerRegistry/Interfaces/ITaskSource.hpp"
#include "TaskControllerRegistry/detail/Directory.hpp"
#include "TaskControllerRegistry/detail/Metrics.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::TaskControllerRegistry::detail {

class Registry : public HasLifecycle<Registry>,
                 public TaskController::IRegistry {
    friend class HasLifecycle<Registry>;
    friend struct LifecycleContract<Registry>;

  public:
    DELETE_COPY(Registry)
    DELETE_MOVE(Registry)

    // NOTE: Must enable here because TaskController wants to register
    //       itself in its constructor
    Registry() : _metrics{Metrics::create()} { _enableRegistration(); }

    static constexpr const char *name = "TaskControllerRegistry::Registry";

    ReturnCode registerSource(const SourceKey &sourceKey, ITaskSource &source,
                              TaskSourceInfo info) override {
        auto ret = _directory.add(sourceKey, source, info);
        FAIL_IF(!ret, ret.error(), "Failed to register task source " SV_FMT,
                SV_ARG(info.displayName));
        _metrics.addTask();
        return OK();
    }

    ReturnCode deregisterSource(const SourceKey &sourceKey) override {
        auto ret = _directory.remove(sourceKey);
        FAIL_IF_ERR(ret, ret, "Failed to deregister task source");
        _metrics.removeTask();
        return OK();
    }

    ReturnCode registerManagedTaskHandle(uintptr_t handle) override {
        FAIL_IF(handle == 0, ERR(InvalidArgument),
                "Cannot register null managed task handle");
        Mutex::ScopedSpinlockGuard guard{_managedTaskHandleLock};
        for (size_t i = 0; i < _managedTaskHandleCount; ++i) {
            if (_managedTaskHandles[i] == handle) {
                return OK();
            }
        }
        FAIL_IF(_managedTaskHandleCount >= _managedTaskHandles.size(),
                ERR(OutOfMemory),
                "Not enough space to track managed task handles");
        _managedTaskHandles[_managedTaskHandleCount++] = handle;
        return OK();
    }

    ReturnCode deregisterManagedTaskHandle(uintptr_t handle) override {
        FAIL_IF(handle == 0, ERR(InvalidArgument),
                "Cannot deregister null managed task handle");
        Mutex::ScopedSpinlockGuard guard{_managedTaskHandleLock};
        for (size_t i = 0; i < _managedTaskHandleCount; ++i) {
            if (_managedTaskHandles[i] != handle) {
                continue;
            }
            for (size_t j = i + 1; j < _managedTaskHandleCount; ++j) {
                _managedTaskHandles[j - 1] = _managedTaskHandles[j];
            }
            --_managedTaskHandleCount;
            _managedTaskHandles[_managedTaskHandleCount] = 0;
            return OK();
        }
        return ERR(NotFound);
    }

    [[nodiscard]] bool isManagedTaskHandle(uintptr_t handle) const {
        if (handle == 0) {
            return false;
        }
        Mutex::ScopedSpinlockGuard guard{_managedTaskHandleLock};
        for (size_t i = 0; i < _managedTaskHandleCount; ++i) {
            if (_managedTaskHandles[i] == handle) {
                return true;
            }
        }
        return false;
    }

    ReturnCode reap() {
        size_t reapedCount = 0;

        auto ret = _directory.withAll([&reapedCount](
                                          const SourceKey &,
                                          SourceEntry &entry) -> ReturnCode {
            FAIL_IF_UNEXPECTED_FWD(count, entry.source->reap(),
                                   "Failed to reap tasks for source " SV_FMT,
                                   SV_ARG(entry.displayName));

            if (count > 0) {
                reapedCount += count;
                _log_w("Reaped %u tasks for source " SV_FMT, reapedCount,
                       SV_ARG(entry.displayName));
            }

            return OK();
        });

        FAIL_IF_ERR_FWD(ret, "Failed to reap tasks for registry");
        _metrics.addReaped(reapedCount);
        return OK();
    }

    template <typename Fn>
        requires TaskController::IsSnapshotHandler<Fn>
    ReturnCode forEachTaskSnapshot(Fn &&fun) {
        return _directory.forEachTaskSnapshot(fun);
    }

    ReturnCode collectTaskSnapshotsInto(
        std::span<TaskController::TaskRuntimeSnapshot> out) {
        return forEachTaskSnapshot(
            [&out](const TaskController::TaskRuntimeSnapshot &snap) {
                FAIL_IF(out.empty(), ERR(OutOfMemory),
                        "Not enough space in output span to collect all task "
                        "snapshots");
                out.front() = snap;
                out = out.subspan(1);
                return OK();
            });
    }

    [[nodiscard]] std::expected<uint8_t, ReturnCode> sourceCount() const {
        return _directory.size();
    }

    [[nodiscard]] std::expected<uint8_t, ReturnCode> taskCount() const {
        uint8_t count = 0;
        auto ret = const_cast<Registry *>(this)->forEachTaskSnapshot(
            [&count](const TaskController::TaskRuntimeSnapshot &) {
                FAIL_IF(count == UINT8_MAX, ERR(Overflow),
                        "Task count overflow in registry");
                ++count;
                return OK();
            });
        FAIL_IF_ERR_FWD_UNEXPECTED(ret,
                                   "Failed to get task count for registry");
        return count;
    }

  private:
    void _disableRegistration() { _directory.disableRegistration(); }
    void _enableRegistration() { _directory.enableRegistration(); }

    static ReturnCode _onBegin() { return OK(); }

    ReturnCode _onEnd() {
        _disableRegistration();
        return OK();
    }

    Directory _directory;
    mutable ::platform::Spinlock _managedTaskHandleLock =
        ::platform::create_spinlock();
    std::array<uintptr_t, TaskRegistryConfig::managedTaskCountMax>
        _managedTaskHandles{};
    size_t _managedTaskHandleCount = 0;
    Metrics _metrics;
};

inline constexpr LifecycleContract<Registry> _registry_lifecycle;

} // namespace Totem::TaskControllerRegistry::detail
