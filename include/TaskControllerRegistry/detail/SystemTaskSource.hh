#pragma once

#include "Macros/Facade.hh"
#include "PlatformSelect.hh"
#include "StaticConfig/TaskRegistry.hh"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hh"
#include "TaskControllerRegistry/Interfaces/TaskSourceHooks.hh"
#include "TaskControllerRegistry/detail/Registry.hh"
#include "Types/Error.hh"
#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <string_view>

namespace Totem::TaskControllerRegistry::detail {

class SystemTaskSource {
    using TaskStatus = ::platform::TaskStatus;

  public:
    static constexpr const char *name = "System";

    explicit SystemTaskSource(Registry &registry) : _registry(registry) {
        auto info = TaskSourceInfo{
            .displayName = name,
            .kind = TaskSourceKind::PlatformSystem,
            .capabilities = TaskSourceCapability::None,
        };
        ABORT_IF_ERR(
            _registry.registerSource(reinterpret_cast<uintptr_t>(this),
                                     TaskSourceHooks::bind(*this), info),
            "Failed to register system task source");
    }

    ~SystemTaskSource() {
        ABORT_IF_ERR(_registry.deregisterSource(reinterpret_cast<uintptr_t>(this)),
                     "Failed to deregister system task source");
    }

    DELETE_COPY(SystemTaskSource)
    DELETE_MOVE(SystemTaskSource)

    ReturnCode forEachTaskSnapshot(TaskSnapshotSink sink) {
        FAIL_IF(!sink.validate(), ERR(InvalidArgument),
                "System task source requires a valid sink");

        uint32_t totalRuntimeCounter = 0;
        auto countResponse = Platform::get_system_task_statuses(
            _taskStatuses, totalRuntimeCounter);
        FAIL_IF_UNEXPECTED_FWD(count, countResponse,
                               "Failed to get system task statuses");
        auto timestamp = ::platform::get_time();
        auto timestampDelta = timestamp - _lastTimestampMs;

        for (size_t i = 0; i < count; ++i) {
            const auto &taskStatus = _taskStatuses[i];
            auto handle = reinterpret_cast<uintptr_t>(taskStatus.xHandle);
            auto runTimeMs =
                Platform::runtime_counter_to_ms(taskStatus.ulRunTimeCounter);
            auto previousRunTimeMs = _findPreviousRuntimeMs(handle);

            float runTimeTotalPct = 0.0F;
            if (timestamp > 0) {
                runTimeTotalPct = 100.0F * static_cast<float>(runTimeMs) /
                                  static_cast<float>(timestamp);
            }

            float runTimeDeltaPct = 0.0F;
            if (timestampDelta > 0 && runTimeMs >= previousRunTimeMs) {
                runTimeDeltaPct =
                    100.0F * static_cast<float>(runTimeMs - previousRunTimeMs) /
                    static_cast<float>(timestampDelta);
            }

            auto platformState = Platform::map_platform_state(taskStatus);
            if (!platformState) {
                continue;
            }

            auto taskNameLen =
                strnlen(taskStatus.pcTaskName, ::platform::MaxTaskNameLen);
            auto snapshot = TaskController::TaskRuntimeSnapshot{
                .timestamp = timestamp,
                .timestampDelta = timestampDelta,
                .name = std::string_view(taskStatus.pcTaskName, taskNameLen),
                .sourceName = {},
                .nativeHandle = handle,
                .hasEverStarted = true,
                .lastStopResult = std::nullopt,
                .state = TaskController::State::Running,
                .platformState = *platformState,
                .coreId = static_cast<int8_t>(taskStatus.xCoreID),
                .currentPriority =
                    static_cast<uint8_t>(taskStatus.uxCurrentPriority),
                .runTimeTotalPct = runTimeTotalPct,
                .runTimeDeltaPct = runTimeDeltaPct,
                .stackLowestFree = taskStatus.usStackHighWaterMark,
                .stackUsedPct = 0.0F,
                .config = nullptr,
            };

            FAIL_IF_ERR_FWD(sink.consume(snapshot),
                            "Failed to emit system task snapshot");
            _rememberRuntimeMs(handle, runTimeMs);
        }

        _lastTimestampMs = timestamp;
        return OK();
    }

    [[nodiscard]] static std::expected<uint8_t, ReturnCode> taskCount() {
        return Platform::get_task_count();
    }

  private:
    struct RuntimeSample {
        uintptr_t handle = 0;
        uint32_t runTimeMs = 0;
    };

    [[nodiscard]] uint32_t _findPreviousRuntimeMs(uintptr_t handle) const {
        for (const auto &sample : _runtimeSamples) {
            if (sample.handle == handle) {
                return sample.runTimeMs;
            }
        }
        return 0;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void _rememberRuntimeMs(uintptr_t handle, uint32_t runTimeMs) {
        for (auto &sample : _runtimeSamples) {
            if (sample.handle == handle || sample.handle == 0) {
                sample.handle = handle;
                sample.runTimeMs = runTimeMs;
                return;
            }
        }
    }

    Registry &_registry;
    std::array<TaskStatus, TaskRegistryConfig::observedTaskCountMax>
        _taskStatuses{};
    std::array<RuntimeSample, TaskRegistryConfig::observedTaskCountMax>
        _runtimeSamples{};
    uint32_t _lastTimestampMs = 0;

    using DefaultError = CoreError;
};

} // namespace Totem::TaskControllerRegistry::detail
