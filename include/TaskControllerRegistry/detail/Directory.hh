#pragma once

#include "Common.hh"

#include "Generic/Directory.hh"
#include "StaticConfig/TaskRegistry.hh"
#include "TaskController/Facade.hh"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceHooks.hh"
#include "Types/Error.hh"
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>
#include <type_traits>

namespace Totem::TaskControllerRegistry::detail {

using SourceKey = uintptr_t;

struct SourceEntry {
    TaskSourceHooks hooks{};
    std::string_view displayName;
    TaskSourceKind kind = TaskSourceKind::Unknown;
    uint32_t capabilities = TaskSourceCapability::None;
};

using DirectoryImpl =
    Directory<SourceKey, SourceEntry, TaskRegistryConfig::sourceCountMax>;

class Directory : public DirectoryImpl {
  public:
    explicit Directory() : DirectoryImpl("TaskControllerRegistry") {
        _setHooks({
            .self = this,
            .beforeRemoveHook = beforeRemove,
        });
    }

    using EntryKey = typename DirectoryImpl::EntryKey;

    std::expected<EntryKey, ReturnCode>
    add(EntryKey sourceKey, TaskSourceHooks hooks, TaskSourceInfo info) {
        FAIL_IF(!hooks.validate(), std::unexpected(ERR(InvalidArgument)),
                "%s: Task source hooks are not fully initialized", ownerName());
        FAIL_IF(info.displayName.empty(), std::unexpected(ERR(InvalidArgument)),
                "%s: Task source display name cannot be empty", ownerName());
        auto entry = SourceEntry{
            .hooks = hooks,
            .displayName = info.displayName,
            .kind = info.kind,
            .capabilities = info.capabilities,
        };
        return _addImpl(sourceKey, entry);
    }

    static ReturnCode beforeRemove(void *directory, std::string_view name,
                                   const SourceEntry &entry) {
        auto *self = static_cast<Directory *>(directory);
        auto emptyResult = entry.hooks.empty();
        FAIL_IF(!emptyResult, emptyResult.error(),
                "Failed to determine if task source %s->" SV_FMT
                " can be removed",
                self->ownerName(), SV_ARG(name));
        if (!emptyResult.value()) {
            _log_w("Attempted to remove task source %s->" SV_FMT
                   " that still has registered tasks",
                   self->ownerName(), SV_ARG(name));
            return ERR(InvalidState);
        }
        return OK();
    }

    template <typename Fn>
        requires TaskController::IsSnapshotHandler<Fn>
    ReturnCode forEachTaskSnapshot(Fn &&fun) {
        using Handler = std::remove_reference_t<Fn>;
        return withAllConst(
            [&](const EntryKey &, const SourceEntry &entry) -> ReturnCode {
                auto downstreamSink = TaskSnapshotSink{
                    .self = std::addressof(fun),
                    .consumeHook =
                        [](void *handlerPtr,
                           const TaskController::TaskRuntimeSnapshot &snapshot)
                        -> ReturnCode {
                        auto &handler = *static_cast<Handler *>(handlerPtr);
                        return handler(snapshot);
                    },
                };
                struct DecoratedSinkCtx {
                    const SourceEntry *entry;
                    TaskSnapshotSink downstream;
                } ctx{.entry = &entry, .downstream = downstreamSink};

                auto decorateResult = entry.hooks.forEachTaskSnapshot({
                    .self = &ctx,
                    .consumeHook = [](void *decoratedSinkCtx,
                                      const TaskController::TaskRuntimeSnapshot
                                          &snapshot) -> ReturnCode {
                        auto &sinkCtx =
                            *static_cast<DecoratedSinkCtx *>(decoratedSinkCtx);
                        auto decorated = snapshot;
                        if (decorated.sourceName.empty()) {
                            decorated.sourceName = sinkCtx.entry->displayName;
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
                FAIL_IF_ERR_FWD(
                    decorateResult,
                    "Failed to enumerate task snapshots for source " SV_FMT,
                    SV_ARG(entry.displayName));
                return OK();
            });
    }
};

} // namespace Totem::TaskControllerRegistry::detail
