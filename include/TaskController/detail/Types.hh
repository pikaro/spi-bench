#pragma once

#include "Common.hh"

#include "Concepts/Base.hh"
#include "Macros/Facade.hh"
#include "Types/Error.hh"
#include "Types/Signal.hh"
#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Totem::TaskController::detail {

struct TaskHooks {
    template <class T> struct Contract {
        static_assert(
            requires(T &cls) { cls._onTaskStep(); },
            "T must provide _onTaskStep()");
        static_assert(IsNamedEntity<T>, "T must be a named entity");
    };

    // Virtual dispatch for swappable task hook providers
    void *self = nullptr;

    ReturnCode (*stepHook)(void *) = nullptr;
    ReturnCode (*startHook)(void *) = nullptr;
    ReturnCode (*notifyHook)(void *, Signal) = nullptr;
    ReturnCode (*stopHook)(void *) = nullptr;

    ReturnCode onStart() const {
        return startHook != nullptr ? startHook(self) : OK(CoreError);
    }

    ReturnCode onStep() const { return stepHook(self); }

    ReturnCode onNotify(Signal sig) const {
        return notifyHook != nullptr ? notifyHook(self, sig) : OK(CoreError);
    }

    ReturnCode onStop() const {
        return stopHook != nullptr ? stopHook(self) : OK(CoreError);
    }

  private:
    template <class T> static consteval bool hasStep() {
        return requires(T &cls) {
            { cls._onTaskStep() } -> std::same_as<ReturnCode>;
        };
    }

    template <class T> static consteval bool hasStart() {
        return requires(T &cls) {
            { cls._onTaskStart() } -> std::same_as<ReturnCode>;
        };
    }

    template <class T> static consteval bool hasNotify() {
        return requires(T &cls, Signal sig) {
            { cls._onTaskNotify(sig) } -> std::same_as<ReturnCode>;
        };
    }

    template <class T> static consteval bool hasStop() {
        return requires(T &cls) {
            { cls._onTaskStop() } -> std::same_as<ReturnCode>;
        };
    }

  public:
    template <class T>
    static TaskHooks bind(T &obj)
        requires requires { sizeof(Contract<T>); }
    {
        TaskHooks hooks{};
        hooks.self = std::addressof(obj);

        hooks.stepHook = +[](void *ptr) -> ReturnCode {
            return static_cast<T *>(ptr)->_onTaskStep();
        };

        if constexpr (hasStart<T>()) {
            hooks.startHook = +[](void *ptr) -> ReturnCode {
                return static_cast<T *>(ptr)->_onTaskStart();
            };
        }

        if constexpr (hasNotify<T>()) {
            hooks.notifyHook = +[](void *ptr, Signal sig) -> ReturnCode {
                return static_cast<T *>(ptr)->_onTaskNotify(sig);
            };
        }

        if constexpr (hasStop<T>()) {
            hooks.stopHook = +[](void *ptr) -> ReturnCode {
                return static_cast<T *>(ptr)->_onTaskStop();
            };
        }

        return hooks;
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && stepHook != nullptr;
    }
};

class Controller;

template <class T>
concept HasRegister = requires(T &cls, const char *name, Controller *ctrl) {
    { cls.registerController(name, ctrl) } -> std::same_as<ReturnCode>;
};

template <class T>
concept HasDeregister = requires(T &cls, const char *name) {
    { cls.deregisterController(name) } -> std::same_as<ReturnCode>;
};

struct RegistryHooks {
    template <class T> struct Contract {
        static_assert(HasRegister<T>, "T must provide registerController");
        static_assert(HasDeregister<T>, "T must provide deregisterController");
        static_assert(IsNamedEntity<T>, "T must be a named entity");
    };

    // Virtual dispatch for breaking circular dependency between Controller and
    // Registry, low overhead since registry hooks are only used during
    // controller construction and destruction
    void *self = nullptr;

    // All required
    ReturnCode (*registerHook)(void *, const char *, Controller *) = nullptr;
    ReturnCode (*deregisterHook)(void *, const char *) = nullptr;

    ReturnCode registerController(const char *ownerName,
                                  Controller *controller) const {
        return registerHook(self, ownerName, controller);
    }
    ReturnCode deregisterController(const char *ownerName) const {
        return deregisterHook(self, ownerName);
    }

    template <class T>
        requires requires { sizeof(Contract<T>); }
    static RegistryHooks bind(T &obj) {
        return RegistryHooks{
            .self = std::addressof(obj),
            .registerHook = [](void *ptr, const char *name,
                               Controller *ctrl) -> ReturnCode {
                return static_cast<T *>(ptr)->registerController(name, ctrl);
            },
            .deregisterHook = [](void *ptr, const char *name) -> ReturnCode {
                return static_cast<T *>(ptr)->deregisterController(name);
            },
        };
    }

    [[nodiscard]] bool validate() const {
        return self != nullptr && registerHook != nullptr &&
               deregisterHook != nullptr;
    }
};

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
        return "Stopped";
    case State::Starting:
        return "Starting";
    case State::Running:
        return "Running";
    case State::Stopping:
        return "Stopping";
    default:
        return "Unknown";
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
        return "Running";
    case PlatformState::Ready:
        return "Ready";
    case PlatformState::Blocked:
        return "Blocked";
    case PlatformState::Suspended:
        return "Suspended";
    default:
        return "Unknown";
    }
}

struct TaskPlatformSnapshot {
    PlatformState state;
    uint8_t priority;
    uint32_t runTimeMs = 0;
    uint32_t stackLowestFree = 0;
    uint8_t coreId;
};

struct TaskRuntimeSnapshot {
    uint32_t timestamp = 0;
    uint32_t timestampDelta;
    std::string_view name;
    bool hasEverStarted;
    std::optional<Result> lastStopResult;
    State state;
    PlatformState platformState;
    uint8_t currentPriority;
    float runTimeTotalPct;
    float runTimeDeltaPct;
    float stackUsedPct;
};

} // namespace Totem::TaskController::detail
