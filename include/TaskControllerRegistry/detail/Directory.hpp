#pragma once

#include "Generic/Directory.hpp"
#include "Macros/Facade.hpp"
#include "StaticConfig/TaskRegistry.hpp"
#include "TaskController/Facade.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "TaskControllerRegistry/Interfaces/ITaskSource.hpp"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hpp"
#include "TaskControllerRegistry/Interfaces/Types.hpp"
#include "TaskControllerRegistry/detail/Types.hpp" // IWYU pragma: keep
#include "Types/Error.hpp"
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>
#include <type_traits>

namespace Totem::TaskControllerRegistry::detail {

struct SourceEntry {
    ITaskSource *source;
    std::string_view displayName;
    TaskSourceKind kind = TaskSourceKind::Unknown;
    uint32_t capabilities = TaskSourceCapability::None;
};

class Directory;

using DirectoryImpl = BaseDirectory<Directory, SourceKey, SourceEntry,
                                    TaskRegistryConfig::sourceCountMax>;

class Directory : public DirectoryImpl {
  public:
    explicit Directory()
        : DirectoryImpl("TaskControllerRegistry",
                        Totem::TaskControllerRegistry::detail::logComponent) {}

    std::expected<SourceKey, ReturnCode>
    add(SourceKey sourceKey, ITaskSource &source, TaskSourceInfo info) {
        FAIL_IF(info.displayName.empty(), std::unexpected(ERR(InvalidArgument)),
                "%s: Task source display name cannot be empty", ownerName());
        auto entry = SourceEntry{
            .source = &source,
            .displayName = info.displayName,
            .kind = info.kind,
            .capabilities = info.capabilities,
        };
        return _addImpl(sourceKey, entry);
    }

    static ReturnCode beforeRemove(void *directory, std::string_view name,
                                   const SourceEntry &entry) {
        auto *self = static_cast<Directory *>(directory);
        return self->beforeRemove(name, entry);
    }

    ReturnCode beforeRemove(std::string_view name, const SourceEntry &entry) {
        auto emptyResult = entry.source->empty();
        FAIL_IF(!emptyResult, emptyResult.error(),
                "Failed to determine if task source %s->" SV_FMT
                " can be removed",
                ownerName(), SV_ARG(name));
        if (!emptyResult.value()) {
            _log_w("Attempted to remove task source %s->" SV_FMT
                   " that still has registered tasks",
                   ownerName(), SV_ARG(name));
            return ERR(InvalidState);
        }
        return OK();
    }

    template <typename Fn>
        requires TaskController::IsSnapshotHandler<Fn>
    ReturnCode forEachTaskSnapshot(Fn &&fun) {
        using Handler = std::remove_reference_t<Fn>;
        return withAllConst(
            [&](const SourceKey &, const SourceEntry &entry) -> ReturnCode {
                struct DownstreamSink final : ISnapshotSink {
                    explicit DownstreamSink(Handler &handler)
                        : handler(handler) {}

                    ReturnCode consume(
                        const TaskController::TaskRuntimeSnapshot &snapshot)
                        override {
                        return handler(snapshot);
                    }

                    Handler &handler;
                };

                DownstreamSink downstreamSink{fun};
                struct DecoratedSinkCtx {
                    const SourceEntry *entry;
                    ISnapshotSink *downstream;
                } ctx{.entry = &entry, .downstream = &downstreamSink};

                struct DecoratedSink final : ISnapshotSink {
                    explicit DecoratedSink(DecoratedSinkCtx &sinkCtx)
                        : sinkCtx(sinkCtx) {}

                    ReturnCode consume(
                        const TaskController::TaskRuntimeSnapshot &snapshot)
                        override {
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
                        return sinkCtx.downstream->consume(decorated);
                    }

                    DecoratedSinkCtx &sinkCtx;
                };

                DecoratedSink decoratedSink{ctx};
                auto decorateResult =
                    entry.source->forEachTaskSnapshot(decoratedSink);
                FAIL_IF_ERR_FWD(
                    decorateResult,
                    "Failed to enumerate task snapshots for source " SV_FMT,
                    SV_ARG(entry.displayName));
                return OK();
            });
    }
};

} // namespace Totem::TaskControllerRegistry::detail
