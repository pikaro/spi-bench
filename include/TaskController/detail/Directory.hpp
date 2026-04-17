#pragma once

#include "Generic/Directory.hh"
#include "Macros/Facade.hh"
#include "StaticConfig/TaskController.hh"
#include "TaskController/Interfaces/Config.hh"
#include "TaskController/Interfaces/TaskHooks.hh"
#include "TaskController/detail/Concepts.hh"
#include "TaskController/detail/Runner.hh"
#include "Types/Error.hh"
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace Totem::TaskController::detail {

struct RunnerEntry {
    std::unique_ptr<Runner> runner = nullptr;
    TaskHooks hooks{};
    std::optional<Config> config = std::nullopt;
    std::string_view name;
};

using RunnerKey = uintptr_t;
using DirectoryImpl =
    Directory<RunnerKey, RunnerEntry, TaskControllerConfig::maxTasksPerClass>;

class Directory : public DirectoryImpl {
  public:
    explicit Directory(const char *ownerName, LogComponent component)
        : DirectoryImpl(ownerName, component) {
        _setHooks({
            .self = this,
            .beforeRemoveHook = beforeRemove,
        });
    }

    using EntryKey = typename DirectoryImpl::EntryKey;

    std::expected<EntryKey, ReturnCode> add(const char *runnerName,
                                            TaskHooks taskHooks) {
        FAIL_IF_NULL(runnerName, std::unexpected(ERR(InvalidArgument)),
                     "%s: Runner name cannot be null", ownerName());
        auto runner = std::make_unique<Runner>(taskHooks);
        auto key = reinterpret_cast<EntryKey>(runner.get());
        auto entry = RunnerEntry{.runner = std::move(runner),
                                 .hooks = taskHooks,
                                 .name = runnerName};
        return _addImpl(key, std::move(entry));
    }

    static ReturnCode beforeRemove(void *directory, std::string_view name,
                                   const RunnerEntry &entry) {
        auto *self = static_cast<Directory *>(directory);
        return self->beforeRemove(name, entry);
    }

    ReturnCode beforeRemove(std::string_view name, const RunnerEntry &entry) {
        if (entry.runner->hasEverStarted() && !entry.runner->hasStopped()) {
            _log_w("Attempted to remove runner %s->" SV_FMT
                   " that is still running",
                   ownerName(), SV_ARG(name));
            return ERR(InvalidState);
        }
        return OK();
    }

    template <typename Fn>
        requires IsSnapshotHandler<Fn>
    ReturnCode forEachTaskSnapshot(Fn &&fun) {
        return withAll(
            [&](const EntryKey &, const RunnerEntry &entry) -> ReturnCode {
                FAIL_IF_NULL(entry.runner, ERR(InvalidState),
                             "Runner for owner %s is null", ownerName());
                auto takeSnapshotResult = entry.runner->takeSnapshot();
                FAIL_IF_ERR_FWD(takeSnapshotResult,
                                "Failed to take snapshot for runner %s",
                                entry.runner->config().name);
                auto snapshotResult = entry.runner->snapshot();
                if (!snapshotResult) {
                    return snapshotResult.error();
                }
                return fun(*snapshotResult);
            });
    }
};

} // namespace Totem::TaskController::detail
