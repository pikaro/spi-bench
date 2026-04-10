#pragma once

#include "Macros/Facade.hh"
#include "TaskController/Interfaces/Config.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hh"
#include "Types/Error.hh"
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

static constexpr std::string_view exit_reason_to_string(ExitReason reason) {
    switch (reason) {
    case ExitReason::Killed:
        return "Killed";
    case ExitReason::StopRequested:
        return "StopRequested";
    case ExitReason::InvalidStateTransition:
        return "InvalidStateTransition";
    case ExitReason::StartHookFailed:
        return "StartHookFailed";
    case ExitReason::StepFailed:
        return "StepFailed";
    case ExitReason::SignalFailed:
        return "SignalFailed";
    case ExitReason::StopHookFailed:
        return "StopHookFailed";
    default:
        return "Unknown";
    }
}

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

static constexpr std::string_view state_to_string(State state) {
    switch (state) {
    case State::Stopped:
        return "H";
    case State::Starting:
        return "S";
    case State::Running:
        return "R";
    case State::Stopping:
        return "T";
    default:
        return "?";
    }
}

enum class PlatformState : uint8_t {
    Running = 0,
    Ready,
    Blocked,
    Suspended,
};

static constexpr std::string_view
platform_state_to_string(PlatformState state) {
    switch (state) {
    case PlatformState::Running:
        return "R";
    case PlatformState::Ready:
        return "A";
    case PlatformState::Blocked:
        return "B";
    case PlatformState::Suspended:
        return "S";
    default:
        return "?";
    }
}

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
    const Config *config = nullptr;
};

} // namespace Totem::TaskController
