#pragma once

#include "Macros/Facade.hpp"
#include "PlatformSelect.hpp"
#include "StaticConfig/TaskRegistry.hpp"
#include "TaskController/Interfaces/TaskRuntimeSnapshot.hpp"
#include "TaskControllerRegistry/Interfaces/ITaskSource.hpp"
#include "TaskControllerRegistry/Interfaces/TaskSourceFeatures.hpp"
#include "TaskControllerRegistry/detail/Registry.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace Totem::TaskControllerRegistry::detail {

class SystemTaskSource : public ITaskSource {
    using TaskStatus = ::platform::TaskStatus;

  public:
    static constexpr const char *name = "System";

    explicit SystemTaskSource(Registry &registry) : _registry(registry) {
        auto info = TaskSourceInfo{
            .displayName = name,
            .kind = TaskSourceKind::PlatformSystem,
            .capabilities = TaskSourceCapability::None,
        };
        ABORT_IF_ERR(_registry.registerSource(reinterpret_cast<uintptr_t>(this),
                                              *this, info),
                     "Failed to register system task source");
    }

    ~SystemTaskSource() override {
        ABORT_IF_ERR(
            _registry.deregisterSource(reinterpret_cast<uintptr_t>(this)),
            "Failed to deregister system task source");
    }

    DELETE_COPY(SystemTaskSource)
    DELETE_MOVE(SystemTaskSource)

    ReturnCode forEachTaskSnapshot(ISnapshotSink &sink) override {
        uint32_t totalRuntimeCounter = 0;
        auto countResponse = Platform::get_system_task_statuses(
            _taskStatuses, totalRuntimeCounter);
        FAIL_IF_UNEXPECTED_FWD(count, countResponse,
                               "Failed to get system task statuses");
        auto timestamp = ::platform::get_time();
        auto timestampDelta = timestamp - _lastTimestampMs;

        for (size_t i = 0; i < count; ++i) {
            const auto &taskStatus = _taskStatuses[i];
            auto handle = Platform::get_handle(taskStatus);
            if (_registry.isManagedTaskHandle(handle)) {
                continue;
            }
            auto runTimeMs = Platform::get_run_time_ms(taskStatus);
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

            auto snapshot = TaskController::TaskRuntimeSnapshot{
                .timestamp = timestamp,
                .timestampDelta = timestampDelta,
                .name = Platform::get_task_name(taskStatus),
                .sourceName = {},
                .nativeHandle = handle,
                .hasEverStarted = true,
                .lastStopResult = std::nullopt,
                .state = TaskController::State::Running,
                .platformState = *platformState,
                .coreId = Platform::get_core_id(taskStatus),
                .currentPriority = Platform::get_priority(taskStatus),
                .runTimeTotalPct = runTimeTotalPct,
                .runTimeDeltaPct = runTimeDeltaPct,
                .stackLowestFree = Platform::get_stack_watermark(taskStatus),
                .stackUsedPct = 0.0F,
                .allocation = TaskController::TaskAllocation::Dynamic,
                .config = nullptr,
            };

            FAIL_IF_ERR_FWD(sink.consume(snapshot),
                            "Failed to emit system task snapshot");
            _rememberRuntimeMs(handle, runTimeMs);
        }

        _lastTimestampMs = timestamp;
        return OK();
    }

    [[nodiscard]] std::expected<uint8_t, ReturnCode> taskCount() override {
        return Platform::get_task_count();
    }

    [[nodiscard]] std::expected<uint8_t, ReturnCode> reap() override {
        return 0;
    }

    [[nodiscard]] std::expected<bool, ReturnCode> empty() override {
        return true;
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
};

} // namespace Totem::TaskControllerRegistry::detail
