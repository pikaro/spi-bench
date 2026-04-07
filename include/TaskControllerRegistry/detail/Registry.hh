#pragma once

#include "Base/HasLifecycle.hh"
#include "Macros/Facade.hh"
#include "TaskController/Facade.hh"
#include "TaskControllerRegistry/detail/Directory.hh"
#include "TaskControllerRegistry/detail/Metrics.hh"
#include "Types/Error.hh"
#include <cstdint>
#include <expected>
#include <span>

namespace Totem::TaskControllerRegistry::detail {

class Registry : public HasLifecycle<Registry> {
    friend class HasLifecycle<Registry>;
    friend struct LifecycleContract<Registry>;

  public:
    using ControllerNameKey = Directory::EntryNameKey;

    DELETE_COPY(Registry)
    DELETE_MOVE(Registry)

    // NOTE: Must enable here because TaskController wants to register
    //       itself in its constructor
    Registry() : _metrics{Metrics::create()} { _enableRegistration(); }

    static constexpr const char *name = "TaskControllerRegistry::Registry";

    ReturnCode registerController(const char *ownerName,
                                  TaskController::Controller *controller) {
        FAIL_IF_NULL(ownerName, ERR(InvalidArgument),
                     "Cannot register controller with null owner name");
        FAIL_IF_NULL(controller, ERR(InvalidArgument),
                     "Cannot register null controller");
        auto nameKey = ControllerNameKey::fromCharPtr(ownerName);
        return registerController(nameKey, controller);
    }

    ReturnCode registerController(ControllerNameKey ownerNameKey,
                                  TaskController::Controller *controller) {
        auto ret = _directory.add(ownerNameKey, controller);
        FAIL_IF(!ret, ret.error(), "Failed to register controller %s",
                ownerNameKey.name.data());
        FAIL_IF_ERR_FWD(
            _metrics.addTask(),
            "Failed to update metrics for registering controller %s",
            ownerNameKey.name.data());
        return OK();
    }

    ReturnCode deregisterController(const char *ownerName) {
        FAIL_IF_NULL(ownerName, ERR(InvalidArgument),
                     "Cannot deregister controller with null owner name");
        auto nameKey = ControllerNameKey::fromCharPtr(ownerName);
        return deregisterController(nameKey);
    }

    ReturnCode deregisterController(ControllerNameKey ownerNameKey) {
        auto ret = _directory.remove(ownerNameKey);
        FAIL_IF_ERR(ret, ret, "Failed to deregister controller %s",
                    ownerNameKey.name.data());
        FAIL_IF_ERR_FWD(
            _metrics.removeTask(),
            "Failed to update metrics for deregistering controller %s",
            ownerNameKey.name.data());
        return OK();
    }

    ReturnCode reap() {
        return _directory.withAll(
            [](const ControllerNameKey &,
               const ControllerEntry &entry) -> ReturnCode {
                FAIL_IF_UNEXPECTED_FWD(count, entry.controller->reap(),
                                       "Failed to reap tasks for controller %s",
                                       entry.controller->ownerName());
                if (count > 0) {
                    _log_i("Reaped %u tasks for controller %s", count,
                           entry.controller->ownerName());
                }
                return OK();
            });
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

    [[nodiscard]] TaskController::RegistryHooks hooks() {
        return TaskController::RegistryHooks::bind(*this);
    }

    [[nodiscard]] std::expected<uint8_t, ReturnCode> controllerCount() const {
        return _directory.size();
    }

    [[nodiscard]] std::expected<bool, ReturnCode> taskCount() const {
        uint8_t count = 0;
        auto ret = _directory.withAllConst(
            [&count](const ControllerNameKey &, const ControllerEntry &entry) {
                auto taskCountResult = entry.controller->taskCount();
                if (!taskCountResult) {
                    FAIL(taskCountResult.error(),
                         "Failed to get task count for controller "
                         "%s during registry task count",
                         entry.controller->name);
                }
                count += taskCountResult.value();
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
