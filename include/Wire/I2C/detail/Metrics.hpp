#pragma once

#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/Types.hpp"
#include "Services/Metrics.hpp" // IWYU pragma: keep
#include "Types/Error.hpp"
#include <cstdint>
#include <cstddef>

namespace Totem::Wire::I2C::detail {

struct Metrics {
    using GroupHandle = Totem::MetricsBackend::GroupHandle;
    using CounterHandle = Totem::MetricsBackend::CounterHandle;
    using GaugeHandle = Totem::MetricsBackend::GaugeHandle;
    using MetricGroupDesc = Totem::MetricsBackend::MetricGroupDesc;
    using MetricDesc = Totem::MetricsBackend::MetricDesc;
    using MetricType = Totem::MetricsBackend::MetricType;
    using MetricUnit = Totem::MetricsBackend::MetricUnit;

    static constexpr MetricGroupDesc coreGroupDef = {
        .name = "i2cCore",
        .level = Totem::MetricsBackend::MetricLevel::Core,
    };
    static constexpr MetricGroupDesc groupDef = {
        .name = "i2c",
        .level = Totem::MetricsBackend::MetricLevel::Diagnostic,
    };
    static constexpr MetricGroupDesc profileGroupDef = {
        .name = "i2cProf",
        .level = Totem::MetricsBackend::MetricLevel::Profiling,
    };

    static constexpr MetricDesc failuresDef = {
        .name = "fail",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc timeoutsDef = {
        .name = "timeout",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc lockTimeoutsDef = {
        .name = "lockTo",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc devicesDef = {
        .name = "devices",
        .type = MetricType::Gauge,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc writesDef = {
        .name = "write",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc readsDef = {
        .name = "read",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc writeReadsDef = {
        .name = "wrRead",
        .type = MetricType::Counter,
        .unit = MetricUnit::None,
    };
    static constexpr MetricDesc txBytesDef = {
        .name = "txByte",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc rxBytesDef = {
        .name = "rxByte",
        .type = MetricType::Counter,
        .unit = MetricUnit::Bytes,
    };
    static constexpr MetricDesc txnUsDef = {
        .name = "txnUs",
        .type = MetricType::Counter,
        .unit = MetricUnit::Microseconds,
    };
    static constexpr MetricDesc txnMaxDef = {
        .name = "txnMax",
        .type = MetricType::Gauge,
        .unit = MetricUnit::Microseconds,
        .gaugeIsMax = true,
    };

    static Metrics create() {
        REGISTER_METRICS_GROUP("I2CCore", coreGroup);
        REGISTER_METRIC("I2CCore", failures, Counter, coreGroup);
        REGISTER_METRIC("I2CCore", timeouts, Counter, coreGroup);
        REGISTER_METRIC("I2CCore", lockTimeouts, Counter, coreGroup);

        REGISTER_METRICS_GROUP("I2C", group);
        REGISTER_METRIC("I2C", devices, Gauge, group);
        REGISTER_METRIC("I2C", writes, Counter, group);
        REGISTER_METRIC("I2C", reads, Counter, group);
        REGISTER_METRIC("I2C", writeReads, Counter, group);
        REGISTER_METRIC("I2C", txBytes, Counter, group);
        REGISTER_METRIC("I2C", rxBytes, Counter, group);

        REGISTER_METRICS_GROUP("I2CProfiling", profileGroup);
        REGISTER_METRIC("I2CProfiling", txnUs, Counter, profileGroup);
        REGISTER_METRIC("I2CProfiling", txnMax, Gauge, profileGroup);

        return Metrics{
            .coreGroup = coreGroup,
            .failures = failures,
            .timeouts = timeouts,
            .lockTimeouts = lockTimeouts,
            .group = group,
            .devices = devices,
            .writes = writes,
            .reads = reads,
            .writeReads = writeReads,
            .txBytes = txBytes,
            .rxBytes = rxBytes,
            .profileGroup = profileGroup,
            .txnUs = txnUs,
            .txnMax = txnMax,
        };
    }

    void addDevice() {
        ++deviceCount;
        METRIC_SET(group, devices, deviceCount);
    }

    void removeDevice() {
        if (deviceCount > 0) {
            --deviceCount;
        }
        METRIC_SET(group, devices, deviceCount);
    }

    void clearDevices() {
        deviceCount = 0;
        METRIC_SET(group, devices, deviceCount);
    }

    void addLockTimeout() const { METRIC_INCR(coreGroup, lockTimeouts, 1); }

    void addWrite(size_t bytes, ReturnCode result, uint32_t durationUs) {
        METRIC_INCR(group, writes, 1);
        METRIC_INCR(group, txBytes, static_cast<uint32_t>(bytes));
        addResult(result, durationUs);
    }

    void addRead(size_t bytes, ReturnCode result, uint32_t durationUs) {
        METRIC_INCR(group, reads, 1);
        METRIC_INCR(group, rxBytes, static_cast<uint32_t>(bytes));
        addResult(result, durationUs);
    }

    void addWriteRead(size_t tx, size_t rx, ReturnCode result,
                      uint32_t durationUs) {
        METRIC_INCR(group, writeReads, 1);
        METRIC_INCR(group, txBytes, static_cast<uint32_t>(tx));
        METRIC_INCR(group, rxBytes, static_cast<uint32_t>(rx));
        addResult(result, durationUs);
    }

    void addResult(ReturnCode result, uint32_t durationUs) {
        if (!result.ok()) {
            METRIC_INCR(coreGroup, failures, 1);
            if (result == ERR(CoreError, Timeout) || result == ERR(Timeout)) {
                METRIC_INCR(coreGroup, timeouts, 1);
            }
        }
        METRIC_INCR(profileGroup, txnUs, durationUs);
        if (durationUs <= txnMaxValue) {
            return;
        }
        txnMaxValue = durationUs;
        METRIC_SET(profileGroup, txnMax, durationUs);
    }

    GroupHandle coreGroup;
    CounterHandle failures;
    CounterHandle timeouts;
    CounterHandle lockTimeouts;
    GroupHandle group;
    GaugeHandle devices;
    CounterHandle writes;
    CounterHandle reads;
    CounterHandle writeReads;
    CounterHandle txBytes;
    CounterHandle rxBytes;
    GroupHandle profileGroup;
    CounterHandle txnUs;
    GaugeHandle txnMax;
    uint32_t deviceCount = 0;
    uint32_t txnMaxValue = 0;

    static constexpr auto component = MetricsBackend::MetricComponent::I2C;
    static constexpr bool profilingEnabled =
        metrics_enabled(component, profileGroupDef);
};

DEFINE_PREWARMED_METRICS_ACCESSORS(Metrics, metrics, prewarmMetrics)

} // namespace Totem::Wire::I2C::detail
