#pragma once

#include "Macros/Facade.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hpp"
#include "Types/Error.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

namespace Totem::TaskController {

enum class ExitReason : uint8_t {
    Killed,
    StopRequested,
    InvalidStateTransition,
    StartHookFailed,
    StepFailed,
    SignalFailed,
    StopHookFailed,
};

struct Result {
    ExitReason reason;
    ReturnCode error{OK(CoreError)};
    [[nodiscard]] bool isClean() const {
        return error.ok() && reason == ExitReason::StopRequested;
    }
};

enum class State : uint8_t {
    Stopped,
    Starting,
    Running,
    Stopping,
};

enum class PlatformState : uint8_t {
    Running = 0,
    Ready,
    Blocked,
    Suspended,
};

struct TaskPlatformSnapshot {
    PlatformState state;
    uint8_t priority;
    uint32_t runTimeMs = 0;
    uint32_t stackLowestFree = 0;
    int8_t coreId;
};

struct TaskRuntimeSnapshot {
    using TaskSourceCapability = TaskControllerRegistry::TaskSourceCapability;
    using TaskSourceKind = TaskControllerRegistry::TaskSourceKind;

    uint32_t timestamp = 0;
    uint32_t timestampDelta;
    std::string_view name;
    std::string_view sourceName;
    uintptr_t nativeHandle = 0;
    TaskSourceKind sourceKind = TaskSourceKind::Unknown;
    uint32_t sourceCapabilities = TaskSourceCapability::None;
    bool isManaged = false;
    bool hasEverStarted;
    std::optional<Result> lastStopResult;
    State state;
    PlatformState platformState;
    int8_t coreId;
    uint8_t currentPriority;
    float runTimeTotalPct;
    float runTimeDeltaPct;
    uint32_t stackLowestFree;
    float stackUsedPct;
    TaskAllocation allocation = TaskAllocation::Dynamic;
    const Config *config = nullptr;
};

} // namespace Totem::TaskController
