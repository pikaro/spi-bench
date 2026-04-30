#pragma once

#include "CommandBackend/Interfaces/CommandDesc.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "MetricsBackend/Interfaces/IFrameSink.hpp"
#include "StaticConfig/Metrics.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <magic_enum/magic_enum.hpp>
#include <span>

namespace Totem::MetricsBackend::detail {

constexpr uint8_t metricsPerLine = 4;

inline static ReturnCode
dump_metric_snapshot(const MetricsBackend::MetricFrame &snap) {
    uint8_t printedMetrics = 0;

    while (printedMetrics < snap.group.metricCount) {
        std::array<char, logMaxLength> buf;

        constexpr size_t groupWidth = MetricConfig::maxMetricGroupNameLength +
                                      10   // max uint32_t digits
                                      + 3  // max uint8_t digits
                                      + 8; // overhead

        snprintf(buf.data(), groupWidth + 1, // +1 for null terminator
                 "@%10" PRIu32 " [%3" PRIu8 " " SV_FMT "] | ", snap.timestampMs,
                 snap.group.metricCount,
                 SV_ARG(snap.group.displayName(),
                        MetricConfig::maxMetricGroupNameLength));

        constexpr size_t metricWidth = MetricConfig::maxMetricNameLength +
                                       10   // max uint32_t digits
                                       + 2  // unit
                                       + 4; // overhead

        auto *start = buf.data() + groupWidth;
        for (size_t i = 0;
             i < metricsPerLine && printedMetrics < snap.group.metricCount;
             ++i) {
            const auto &metric = snap.metrics[printedMetrics];

            snprintf(
                start, metricWidth + 1, SV_FMT " %10" PRIu32 SV_FMT "%s",
                SV_ARG(metric.displayName(), MetricConfig::maxMetricNameLength),
                metric.value, SV_ARG(metric.unit(), 2),
                (i < snap.group.metricCount - 1) ? " | " : "");

            start += metricWidth;
            FAIL_IF(start >= buf.data() + buf.size(), ERR(OutOfMemory),
                    "Metric snapshot output exceeds buffer size");

            printedMetrics++;
        }

        _log_i("%s", buf.data());
    }

    return OK(CoreError);
}

template <typename Owner> struct Commands {
    static ReturnCode handle_metrics(CommandDesc::ParsedArgs /*unused*/,
                                     void *ctx) {
        auto *metrics = static_cast<Owner *>(ctx);
        struct DumpSink final : MetricsBackend::IFrameSink {
            ReturnCode
            consume(const MetricsBackend::MetricFrame &snap) override {
                return dump_metric_snapshot(snap);
            }
        } sink;

        return metrics->snapshot(sink);
    }

    static inline CommandDesc metricsCmd = {
        .needsContext = true,
        .name = "metrics",
        .description = "Output metrics data and stats",
        .args = {},
        .handler = handle_metrics,
        .subcommands = {},
    };

    static constexpr std::span<CommandDesc *> commands() {
        static auto commands = std::to_array<CommandDesc *>({
            &metricsCmd,
        });
        return commands;
    }
};

} // namespace Totem::MetricsBackend::detail
