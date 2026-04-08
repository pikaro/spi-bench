#pragma once

#include "Common.hh"

#include "Generic/Directory.hh"
#include "StaticConfig/TaskRegistry.hh"
#include "TaskController/Facade.hh"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceHooks.hh"
#include "Types/Collection.hh"
#include "Types/Error.hh"
#include <cstdint>
#include <cstring>
#include <expected>
#include <type_traits>

namespace Totem::TaskControllerRegistry::detail {

using SourceNameKey = NameKey<TaskRegistryConfig::sourceNameMaxLen>;

struct SourceEntry {
    SourceNameKey key{};
    SourceNameKey displayKey;
    TaskSourceHooks hooks{};
    TaskSourceKind kind = TaskSourceKind::Unknown;
    uint32_t capabilities = TaskSourceCapability::None;
};

using DirectoryImpl =
    Generic::Directory<SourceEntry, TaskRegistryConfig::sourceCountMax,
                       TaskRegistryConfig::sourceNameMaxLen>;

class Directory : public DirectoryImpl {
  public:
    explicit Directory() : DirectoryImpl("TaskControllerRegistry") {
        _setHooks({
            .self = this,
            .beforeRemoveHook = beforeRemove,
        });
    }

    std::expected<EntryNameKey, ReturnCode>
    add(const EntryNameKey &sourceNameKey, TaskSourceHooks hooks,
        TaskSourceInfo info) {
        FAIL_IF(!hooks.validate(), std::unexpected(ERR(InvalidArgument)),
                "%s: Task source hooks are not fully initialized", ownerName());
        FAIL_IF(info.displayName.empty(), std::unexpected(ERR(InvalidArgument)),
                "%s: Task source display name cannot be empty", ownerName());
        auto displayKey = SourceNameKey::fromStringView(info.displayName);
        auto entry = SourceEntry{
            .key = sourceNameKey,
            .displayKey = displayKey,
            .hooks = hooks,
            .kind = info.kind,
            .capabilities = info.capabilities,
        };
        return _addImpl(sourceNameKey, entry);
    }

    static ReturnCode beforeRemove(void *opaque, const char *name,
                                   const SourceEntry &entry) {
        auto *self = static_cast<Directory *>(opaque);
        auto emptyResult = entry.hooks.empty();
        FAIL_IF(!emptyResult, emptyResult.error(),
                "Failed to determine if task source %s->%s can be removed",
                self->ownerName(), name);
        if (!emptyResult.value()) {
            _log_w("Attempted to remove task source %s->%s that still has "
                   "registered tasks",
                   self->ownerName(), name);
            return ERR(InvalidState);
        }
        return OK();
    }

    template <typename Fn>
        requires TaskController::IsSnapshotHandler<Fn>
    ReturnCode forEachTaskSnapshot(Fn &&fun) {
        using Handler = std::remove_reference_t<Fn>;
        return withAllConst([&](const EntryNameKey &sourceName,
                                const SourceEntry &entry) -> ReturnCode {
            auto downstreamSink = TaskSnapshotSink{
                .self = std::addressof(fun),
                .consumeHook =
                    [](void *opaque,
                       const TaskController::TaskRuntimeSnapshot &snapshot)
                    -> ReturnCode {
                    auto &handler = *static_cast<Handler *>(opaque);
                    return handler(snapshot);
                },
            };
            struct DecoratedSinkCtx {
                const SourceEntry *entry;
                TaskSnapshotSink downstream;
            } ctx{.entry = &entry, .downstream = downstreamSink};

            auto decorateResult = entry.hooks.forEachTaskSnapshot({
                .self = &ctx,
                .consumeHook = [](void *opaque,
                                  const TaskController::TaskRuntimeSnapshot
                                      &snapshot) -> ReturnCode {
                    auto &sinkCtx = *static_cast<DecoratedSinkCtx *>(opaque);
                    auto decorated = snapshot;
                    if (decorated.sourceName.empty()) {
                        decorated.sourceName = sinkCtx.entry->displayKey.view();
                    }
                    if (decorated.sourceKind == TaskSourceKind::Unknown) {
                        decorated.sourceKind = sinkCtx.entry->kind;
                    }
                    if (decorated.sourceCapabilities == 0) {
                        decorated.sourceCapabilities =
                            sinkCtx.entry->capabilities;
                    }
                    if (!decorated.isManaged &&
                        decorated.sourceKind ==
                            TaskSourceKind::ManagedController) {
                        decorated.isManaged = true;
                    }
                    return sinkCtx.downstream.consume(decorated);
                },
            });
            FAIL_IF_ERR_FWD(decorateResult,
                            "Failed to enumerate task snapshots for source %s",
                            sourceName.name.data());
            return OK();
        });
    }
};

} // namespace Totem::TaskControllerRegistry::detail
