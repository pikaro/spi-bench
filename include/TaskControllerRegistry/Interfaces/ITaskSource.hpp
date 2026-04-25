#pragma once

#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <expected>
#include <string_view>

namespace Totem::TaskControllerRegistry {

struct TaskSourceInfo {
    std::string_view displayName;
    TaskSourceKind kind = TaskSourceKind::Unknown;
    uint32_t capabilities = TaskSourceCapability::None;
};

struct ISnapshotSink {
    virtual ~ISnapshotSink() = default;

    virtual ReturnCode
    consume(const Totem::TaskController::TaskRuntimeSnapshot &snapshot) = 0;
};

struct ITaskSource {
    virtual ~ITaskSource() = default;

    virtual ReturnCode forEachTaskSnapshot(ISnapshotSink &sink) = 0;
    [[nodiscard]] virtual std::expected<uint8_t, ReturnCode> taskCount() = 0;
    [[nodiscard]] virtual std::expected<uint8_t, ReturnCode> reap() = 0;
    [[nodiscard]] virtual std::expected<bool, ReturnCode> empty() = 0;
};

} // namespace Totem::TaskControllerRegistry
