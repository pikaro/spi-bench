#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "StaticConfig/TaskController.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "TaskController/Interfaces/IRegistry.hpp"
#include "TaskController/Interfaces/TaskHooks.hpp"
#include "TaskController/Interfaces/Types.hpp"
#include "TaskController/detail/Concepts.hpp"
#include "TaskController/detail/Runner.hpp"
#include "Types/Error.hpp"
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

class Directory;

using DirectoryImpl = BaseDirectory<Directory, RunnerKey, RunnerEntry,
                                    TaskControllerConfig::maxTasksPerClass>;

class Directory : public DirectoryImpl {
  public:
    static constexpr LogComponent logComponent =
        LogComponent::TaskControllerRegistry;

    explicit Directory(const char *ownerName, LogComponent,
                       IRegistry &registryHooks)
        : DirectoryImpl(ownerName), _registry(registryHooks) {}

    std::expected<RunnerKey, ReturnCode> add(const char *runnerName,
                                             TaskHooks taskHooks) {
        FAIL_IF_NULL(runnerName, std::unexpected(ERR(InvalidArgument)),
                     "%s: Runner name cannot be null", ownerName());
        auto runner = std::make_unique<Runner>(taskHooks, _registry);
        auto key = reinterpret_cast<RunnerKey>(runner.get());
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
            [&](const RunnerKey &, const RunnerEntry &entry) -> ReturnCode {
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

    template <typename Fn>
        requires(std::is_invocable_r_v<ReturnCode, Fn, RunnerEntry &>)
    ReturnCode withName(std::string_view runnerName, Fn &&fn) {
        bool found = false;
        auto ret = withAll(
            [&found, runnerName, &fn](const RunnerKey &,
                                      RunnerEntry &entry) -> ReturnCode {
                if (entry.name != runnerName) {
                    return OK();
                }
                found = true;
                return std::invoke(std::forward<Fn>(fn), entry);
            },
            [runnerName](const RunnerKey &, const RunnerEntry &entry) {
                return entry.name == runnerName;
            });
        FAIL_IF_ERR_FWD(ret, "Failed to access runner " SV_FMT " in %s",
                        SV_ARG(runnerName), ownerName());
        FAIL_IF(!found, ERR(NotFound), "Runner " SV_FMT " not found in %s",
                SV_ARG(runnerName), ownerName());
        return OK();
    }

  private:
    IRegistry &_registry;
};

} // namespace Totem::TaskController::detail
