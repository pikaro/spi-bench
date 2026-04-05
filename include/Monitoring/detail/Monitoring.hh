#pragma once

#include "Base/HasLifecycle.hh"
#include "Macros/Facade.hh"
#include "Monitoring/detail/Commands.hh"
#include "Monitoring/detail/PlatformSelect.hh"
#include "Monitoring/detail/Types.hh"
#include "Types/Command.hh"
#include "Types/Error.hh"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Totem::Monitoring::detail {

class Monitoring : public Core::HasLifecycle<Monitoring> {
    friend class Core::HasLifecycle<Monitoring>;
    friend struct Core::LifecycleContract<Monitoring>;

  public:
    Monitoring() = default;
    DELETE_COPY(Monitoring)
    DELETE_MOVE(Monitoring)

    static constexpr const char *name = "Monitoring";

    ReturnCode snapshot(const MonitoringSink &sink) {
        auto now = ::platform::get_time();
        if (now < _lastSnapshotTimestamp) {
            // Time went backwards, likely due to overflow. Reset all totals to
            // avoid issues with negative deltas and such.
            _log_w("Time went backwards since last monitoring snapshot");
            _coreIdleTimeTotalMs.fill(0);
            _coreUtilizationPctTotal.fill(0);
        }

        _lastSnapshotTimestamp = now;

        FAIL_IF_ERR_FWD(_populateMemoryStats(),
                        "Failed to populate memory stats");
        FAIL_IF_ERR_FWD(_populateCoreUtilization(),
                        "Failed to populate core utilization");

        MonitoringFrame frame;
        frame.timestamp = now;
        frame.global.memoryStats = _memoryStats;
        frame.global.coreIdleTimeTotalMs = _coreIdleTimeTotalMs;
        frame.global.coreIdleTimeDeltaMs = _coreIdleTimeDeltaMs;
        frame.global.coreUtilizationPctTotal = _coreUtilizationPctTotal;
        frame.global.coreUtilizationPctDelta = _coreUtilizationPctDelta;

        FAIL_IF_ERR_FWD(sink.consume(frame),
                        "Failed to consume monitoring frame in sink");

        return OK();
    }

  private:
    ReturnCode _onBegin() { return register_commands(this); }
    static ReturnCode _onEnd() { return OK(); }

    ReturnCode _populateMemoryStats() {
        return Platform::collect_memory_stats_into(_memoryStats);
    }

    ReturnCode _populateCoreIdleTimes() {
        return Platform::collect_cpu_free_into(_coreIdleTimeTotalMs);
    }

    ReturnCode _populateCoreUtilization() {
        for (size_t i = 0; i < ::platform::CoreCount; i++) {
            auto idleTimeTotal = _coreIdleTimeTotalMs[i];
            auto idleTimeDelta = idleTimeTotal - _coreIdleTimeDeltaMs[i];
            _coreIdleTimeDeltaMs[i] = idleTimeTotal;

            // Utilization is the percentage of time spent not idle since the
            // last snapshot, so 1 - (idle delta / total delta)
            auto utilizationPctDelta =
                1.0F - (static_cast<float>(idleTimeDelta) /
                        static_cast<float>(idleTimeTotal));
            _coreUtilizationPctDelta[i] = utilizationPctDelta;

            // Total utilization is the percentage of time spent not idle since
            // the first snapshot, so 1 - (total idle / total time)
            auto utilizationPctTotal =
                1.0F - (static_cast<float>(idleTimeTotal) /
                        static_cast<float>(_lastSnapshotTimestamp));
            _coreUtilizationPctTotal[i] = utilizationPctTotal;
        }
        return OK();
    }

    uint32_t _lastSnapshotTimestamp = 0;
    std::array<MemoryStats, Platform::MemoryStatCount> _memoryStats{};
    std::array<uint32_t, ::platform::CoreCount> _coreIdleTimeTotalMs{0};
    std::array<uint32_t, ::platform::CoreCount> _coreIdleTimeDeltaMs{0};
    std::array<float, ::platform::CoreCount> _coreUtilizationPctTotal{0.0F};
    std::array<float, ::platform::CoreCount> _coreUtilizationPctDelta{0.0F};

    using DefaultError = CoreError;
};

inline constexpr LifecycleContract<Monitoring> _monitoring_lifecycle;

inline static ReturnCode cmd_handle_monitoring(CommandDesc::Tokens /*unused*/,
                                               void *ctx) {
    auto *monitoring = static_cast<Monitoring *>(ctx);
    return monitoring->snapshot(MonitoringSink{
        .self = monitoring,
        .consumeHook =
            [](void *, const MonitoringFrame &frame) {
                return dump_monitoring_snaphot(frame);
            },
    });
}

} // namespace Totem::Monitoring::detail
