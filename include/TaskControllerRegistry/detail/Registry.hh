#pragma once

#include "Base/HasLifecycle.hh"
#include "Macros/Facade.hh"
#include "TaskController/Facade.hh"
#include "TaskController/Interfaces/RegistryHooks.hh"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceHooks.hh"
#include "TaskControllerRegistry/detail/Directory.hh"
#include "TaskControllerRegistry/detail/Metrics.hh"
#include "Types/Error.hh"
#include <array>
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::TaskControllerRegistry::detail {

class Registry : public HasLifecycle<Registry> {
    friend class HasLifecycle<Registry>;
    friend struct LifecycleContract<Registry>;

  public:
    using SourceKey = Directory::EntryKey;

    DELETE_COPY(Registry)
    DELETE_MOVE(Registry)

    // NOTE: Must enable here because TaskController wants to register
    //       itself in its constructor
    Registry() : _metrics{Metrics::create()} { _enableRegistration(); }

    static constexpr const char *name = "TaskControllerRegistry::Registry";

    ReturnCode registerSource(SourceKey sourceKey, TaskSourceHooks hooks,
                              TaskSourceInfo info) {
        auto ret = _directory.add(sourceKey, hooks, info);
        FAIL_IF(!ret, ret.error(), "Failed to register task source " SV_FMT,
                SV_ARG(info.displayName));
        FAIL_IF_ERR_FWD(
            _metrics.addTask(),
            "Failed to update metrics for registering task source " SV_FMT,
            SV_ARG(info.displayName));
        return OK();
    }

    ReturnCode deregisterSource(SourceKey sourceKey) {
        auto ret = _directory.remove(sourceKey);
        FAIL_IF_ERR(ret, ret, "Failed to deregister task source");
        FAIL_IF_ERR_FWD(
            _metrics.removeTask(),
            "Failed to update metrics for deregistering task source");
        return OK();
    }

    ReturnCode reap() {
        return _directory.withAll([](const SourceKey &, SourceEntry &entry)
                                      -> ReturnCode {
            if (entry.hooks.reapHook == nullptr) {
                return OK();
            }
            FAIL_IF_UNEXPECTED_FWD(count, entry.hooks.reap(),
                                   "Failed to reap tasks for source " SV_FMT,
                                   SV_ARG(entry.displayName));
            if (count > 0) {
                _log_i("Reaped %u tasks for source " SV_FMT, count,
                       SV_ARG(entry.displayName));
            }
            return OK();
        });
    }

    template <typename Fn>
        requires TaskController::IsSnapshotHandler<Fn>
    ReturnCode forEachTaskSnapshot(Fn &&fun) {
        std::array<uintptr_t, TaskRegistryConfig::observedTaskCountMax>
            seenHandles{};
        size_t seenHandleCount = 0;
        return _directory.forEachTaskSnapshot(
            [&fun, &seenHandles,
             &seenHandleCount](const TaskController::TaskRuntimeSnapshot &snap) {
                if (snap.nativeHandle != 0) {
                    for (size_t i = 0; i < seenHandleCount; ++i) {
                        if (seenHandles[i] == snap.nativeHandle) {
                            return OK();
                        }
                    }
                    FAIL_IF(seenHandleCount >= seenHandles.size(),
                            ERR(OutOfMemory),
                            "Not enough space to deduplicate task snapshots");
                    seenHandles[seenHandleCount++] = snap.nativeHandle;
                }
                return fun(snap);
            });
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

    [[nodiscard]] TaskController::RegistryHooks hooks() {
        return TaskController::RegistryHooks::bind(*this);
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
    Metrics _metrics;

    using DefaultError = CoreError;
};

inline constexpr LifecycleContract<Registry> _registry_lifecycle;
inline constexpr TaskController::RegistryHooks::Contract<Registry>
    _registry_hooks_contract;

} // namespace Totem::TaskControllerRegistry::detail
