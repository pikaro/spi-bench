#pragma once

#include "Macros/Facade.hh"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hh"
#include "Types/Error.hh"
#include <concepts>
#include <cstdint>
#include <expected>
#include <string_view>

namespace Totem::TaskControllerRegistry {

struct TaskSourceInfo {
    std::string_view displayName;
    TaskSourceKind kind = TaskSourceKind::Unknown;
    uint32_t capabilities = TaskSourceCapability::None;
};

struct TaskSnapshotSink {
    void *self = nullptr;
    ReturnCode (*consumeHook)(
        void *, const Totem::TaskController::TaskRuntimeSnapshot &) = nullptr;

    ReturnCode
    consume(const Totem::TaskController::TaskRuntimeSnapshot &snapshot) const {
        return consumeHook(self, snapshot);
    }

    ReturnCode operator()(
        const Totem::TaskController::TaskRuntimeSnapshot &snapshot) const {
        return consume(snapshot);
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && consumeHook != nullptr;
    }
};

struct TaskSourceHooks {
    void *self = nullptr;

    ReturnCode (*forEachSnapshotHook)(void *, TaskSnapshotSink) = nullptr;
    std::expected<uint8_t, ReturnCode> (*taskCountHook)(void *) = nullptr;
    std::expected<uint8_t, ReturnCode> (*reapHook)(void *) = nullptr;
    std::expected<bool, ReturnCode> (*emptyHook)(void *) = nullptr;

    ReturnCode forEachTaskSnapshot(TaskSnapshotSink sink) const {
        FAIL_IF(!validate(), ERR(InvalidState),
                "Task source hooks are not fully initialized");
        FAIL_IF(!sink.validate(), ERR(InvalidArgument),
                "Task snapshot sink is not fully initialized");
        return forEachSnapshotHook(self, sink);
    }

    [[nodiscard]] std::expected<uint8_t, ReturnCode> taskCount() const {
        FAIL_IF(!validate(), std::unexpected(ERR(InvalidState)),
                "Task source hooks are not fully initialized");
        FAIL_IF_NULL(taskCountHook, std::unexpected(ERR(InvalidState)),
                     "Task source does not expose taskCount()");
        return taskCountHook(self);
    }

    [[nodiscard]] std::expected<uint8_t, ReturnCode> reap() const {
        FAIL_IF(!validate(), std::unexpected(ERR(InvalidState)),
                "Task source hooks are not fully initialized");
        FAIL_IF_NULL(reapHook, std::unexpected(ERR(InvalidState)),
                     "Task source does not expose reap()");
        return reapHook(self);
    }

    [[nodiscard]] std::expected<bool, ReturnCode> empty() const {
        FAIL_IF(!validate(), std::unexpected(ERR(InvalidState)),
                "Task source hooks are not fully initialized");
        if (emptyHook == nullptr) {
            return true;
        }
        return emptyHook(self);
    }

  private:
    template <class T> static consteval bool hasReap() {
        return requires(T &cls) {
            { cls.reap() } -> std::same_as<std::expected<uint8_t, ReturnCode>>;
        };
    }

    template <class T> static consteval bool hasEmpty() {
        return requires(T &cls) {
            { cls.empty() } -> std::same_as<std::expected<bool, ReturnCode>>;
        };
    }

  public:
    template <class T>
    static TaskSourceHooks bind(T &obj)
        requires requires(T &cls, TaskSnapshotSink sink) {
            { cls.forEachTaskSnapshot(sink) } -> std::same_as<ReturnCode>;
            {
                cls.taskCount()
            } -> std::same_as<std::expected<uint8_t, ReturnCode>>;
        }
    {
        TaskSourceHooks hooks{};
        hooks.self = std::addressof(obj);
        hooks.forEachSnapshotHook =
            +[](void *ptr, TaskSnapshotSink sink) -> ReturnCode {
            return static_cast<T *>(ptr)->forEachTaskSnapshot(sink);
        };
        hooks.taskCountHook =
            +[](void *ptr) -> std::expected<uint8_t, ReturnCode> {
            return static_cast<T *>(ptr)->taskCount();
        };

        if constexpr (hasReap<T>()) {
            hooks.reapHook =
                +[](void *ptr) -> std::expected<uint8_t, ReturnCode> {
                return static_cast<T *>(ptr)->reap();
            };
        }

        if constexpr (hasEmpty<T>()) {
            hooks.emptyHook =
                +[](void *ptr) -> std::expected<bool, ReturnCode> {
                return static_cast<T *>(ptr)->empty();
            };
        }

        return hooks;
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && forEachSnapshotHook != nullptr &&
               taskCountHook != nullptr;
    }
};

} // namespace Totem::TaskControllerRegistry
