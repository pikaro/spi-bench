#pragma once

#include "Common.hh"

#include "Generic/Directory.hh"
#include "TaskController/detail/Runner.hh"
#include "Types/Error.hh"
#include <cstring>
#include <expected>
#include <memory>
#include <optional>

namespace Totem::TaskController::detail {

struct RunnerEntry {
    std::unique_ptr<Runner> runner = nullptr;
    TaskHooks hooks{};
    std::optional<Config> config = std::nullopt;
};

using DirectoryImpl = Generic::Directory<RunnerEntry, Config::maxTasksPerClass,
                                         Config::maxTaskNameLen>;

class Directory : public DirectoryImpl {
  public:
    explicit Directory(const char *ownerName) : DirectoryImpl(ownerName) {
        _setHooks({
            .self = this,
            .beforeRemoveHook = beforeRemove,
        });
    }

    std::expected<EntryNameKey, ReturnCode> add(const char *runnerName,
                                                TaskHooks taskHooks) {
        FAIL_IF_NULL(runnerName, std::unexpected(ERR(InvalidArgument)),
                     "%s: Runner name cannot be null", ownerName());
        auto nameKey = EntryNameKey::fromCharPtr(runnerName);
        return add(nameKey, taskHooks);
    }

    std::expected<EntryNameKey, ReturnCode>
    add(const EntryNameKey &runnerNameKey, TaskHooks taskHooks) {
        auto entry = RunnerEntry{.runner = std::make_unique<Runner>(taskHooks),
                                 .hooks = taskHooks};
        return _addImpl(runnerNameKey, std::move(entry));
    }

    static ReturnCode beforeRemove(void *opaque, const char *name,
                                   const RunnerEntry &entry) {
        auto *self = static_cast<Directory *>(opaque);
        if (entry.runner->hasEverStarted() && !entry.runner->hasStopped()) {
            _log_w("Attempted to remove runner %s->%s that is still running",
                   self->ownerName(), name);
            return ERR(InvalidState);
        }
        return OK();
    }
};

} // namespace Totem::TaskController::detail
