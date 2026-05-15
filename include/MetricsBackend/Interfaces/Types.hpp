#pragma once

#include "Macros/Facade.hpp"
#include "Types/Collection.hpp"
#include "Types/Error.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Totem::MetricsBackend::detail {

class Registrar;

}

namespace Totem::MetricsBackend {

enum class MetricComponent : uint8_t {
    PubSub = 0,
    TaskControllerRegistry,
    TaskController,
    Rs485,
    Spi,
    Mutex,
    Logging,
    Audio,
    I2C,
    LedPwm,
    Buttons,
};

enum class MetricLevel : uint8_t {
    // Most detailed, highest-volume probes for profiling a code path.
    Profiling = 0,
    // Detailed counters and timings used while diagnosing a subsystem.
    Diagnostic,
    // Cheap steady-state health metrics suitable for normal runs.
    Baseline,
    // Rare loss, recovery, and failure counters worth keeping even in lean runs.
    Core,
};

enum class MetricType : uint8_t { Counter, Gauge };

enum class MetricUnit : uint8_t {
    None = 0,
    Bytes,
    Milliseconds,
    Microseconds,
    Percent,
    Hertz,
    Decibels,
    Ticks,
    Volts,
    Millivolts,
};

constexpr std::string_view metric_unit_to_string(MetricUnit unit) {
    switch (unit) {
    case MetricUnit::None:
        return "";
    case MetricUnit::Bytes:
        return "B";
    case MetricUnit::Milliseconds:
        return "ms";
    case MetricUnit::Microseconds:
        return "us";
    case MetricUnit::Percent:
        return "%";
    case MetricUnit::Hertz:
        return "Hz";
    case MetricUnit::Decibels:
        return "dB";
    case MetricUnit::Ticks:
        return "t";
    case MetricUnit::Volts:
        return "V";
    case MetricUnit::Millivolts:
        return "mV";
    }
    return "?";
}

struct MetricGroupDesc {
    const char *name;
    MetricLevel level = MetricLevel::Baseline;

    ReturnCode validate() const {
        if (name == nullptr || name[0] == '\0') {
            return ERR(InvalidArgument);
        }
        return OK();
    }
};

struct MetricDesc {
    using MetricGroupKey = uintptr_t;

    const char *name;
    MetricType type = MetricType::Counter;
    MetricUnit unit = MetricUnit::None;
    bool clearOnRead = false;
    bool logIfZero = false;
    bool rollover = true;
    uint32_t gaugeDiscardIfUnder = 1;
    uint32_t gaugeDiscardIfOver = UINT32_MAX - 1;
    bool gaugeIsMin = false;
    bool gaugeIsMax = false;

    [[nodiscard]] std::string_view unitString() const {
        return metric_unit_to_string(unit);
    }

    ReturnCode validate() const {
        if (name == nullptr || name[0] == '\0') {
            return ERR(InvalidArgument);
        }
        if (type == MetricType::Gauge) {
            if (gaugeDiscardIfUnder >= gaugeDiscardIfOver) {
                return ERR(InvalidArgument);
            }
        }
        return OK();
    }
};

struct MetricGroup {
    MetricGroupDesc desc{};
    size_t metricCount = 0;

    [[nodiscard]] std::string_view displayName() const { return desc.name; }
};

struct Metric {
    const MetricDesc *desc{};
    uint32_t value = 0;

    [[nodiscard]] std::string_view displayName() const { return desc->name; }
    [[nodiscard]] std::string_view unit() const { return desc->unitString(); }
};

struct GroupTag {};
struct CounterTag {};
struct GaugeTag {};

using MetricGroupKey = uintptr_t;
using MetricKey = uintptr_t;
using GroupHandle = StrongHandle<GroupTag, uintptr_t, detail::Registrar>;
using CounterHandle = StrongHandle<CounterTag, uintptr_t, detail::Registrar>;
using GaugeHandle = StrongHandle<GaugeTag, uintptr_t, detail::Registrar>;

} // namespace Totem::MetricsBackend
