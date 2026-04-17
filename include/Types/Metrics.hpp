#pragma once

#include "Macros/Facade.hpp"
#include "StaticConfig/Metrics.hpp" // IWYU pragma: export
#include "Types/Error.hpp"
#include "Types/Logging.hpp"
#include <cstdint>
#include <string_view>

enum class MetricType : uint8_t { Counter, Gauge };

enum class MetricUnit : uint8_t {
    None = 0,
    Bytes,
    Items,
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
    case MetricUnit::Items:
        return "it";
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
    LogLevel logLevel = LogLevel::Info;

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
