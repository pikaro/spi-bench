#pragma once

#include "Common.hh"

#include "Generic/Directory.hh"
#include "StaticConfig/TaskRegistry.hh"
#include "TaskController/Facade.hh"
#include "Types/Error.hh"
#include <cstring>
#include <expected>

namespace Totem::TaskControllerRegistry::detail {

struct ControllerEntry {
    TaskController::Controller *controller = nullptr;
};

using DirectoryImpl =
    Generic::Directory<ControllerEntry, TaskRegistryConfig::controllerCountMax,
                       TaskRegistryConfig::controllerNameMaxLen>;

class Directory : public DirectoryImpl {
  public:
    explicit Directory() : DirectoryImpl("TaskControllerRegistry") {
        _setHooks({
            .self = this,
            .beforeRemoveHook = beforeRemove,
        });
    }

    std::expected<EntryNameKey, ReturnCode>
    add(TaskController::Controller *controller) {
        FAIL_IF_NULL(controller, std::unexpected(ERR(InvalidArgument)),
                     "%s: Controller cannot be null", ownerName());
        auto nameKey = EntryNameKey::fromCharPtr(controller->ownerName());
        return add(nameKey, controller);
    }

    std::expected<EntryNameKey, ReturnCode>
    add(const EntryNameKey &controllerNameKey,
        TaskController::Controller *controller) {
        auto entry = ControllerEntry{.controller = controller};
        return _addImpl(controllerNameKey, entry);
    }

    static ReturnCode beforeRemove(void *opaque, const char *name,
                                   const ControllerEntry &entry) {
        auto *self = static_cast<Directory *>(opaque);
        if (!entry.controller->empty()) {
            _log_w("Attempted to remove controller %s->%s that still has "
                   "registered tasks",
                   self->ownerName(), name);
            return ERR(InvalidState);
        }
        return OK();
    }
};

} // namespace Totem::TaskControllerRegistry::detail
