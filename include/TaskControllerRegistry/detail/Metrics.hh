#pragma once

#include "Macros/Facade.hh"
#include "Support/Metrics.hh"
#include "Types/Error.hh"
#include "Types/Logging.hh"
#include "Types/Metrics.hh"

namespace Totem::TaskControllerRegistry::detail {

struct Metrics {
    static Metrics create() {
        ABORT_IF_UNEXPECTED(
            group,
            ::Metrics::registrar().addGroup({
                .name = "taskRegi",
                .logLevel = LogLevel::Debug,
            }),
            "Failed to register metrics group for TaskControllerRegistry");
        ABORT_IF_UNEXPECTED(
            taskCount,
            ::Metrics::registrar().addCounter({
                .name = "tskCount",
                .group = group.key(),
                .type = MetricType::Counter,
                .unit = MetricUnit::Items,
            }),
            "Failed to register TaskCount metric for TaskControllerRegistry");

        return Metrics{
            .group = group,
            .taskCount = taskCount,
        };
    }

    ReturnCode addTask() const {
        return ::Metrics::recorder().increment(taskCount);
    }

    ReturnCode removeTask() const {
        return ::Metrics::recorder().decrement(taskCount);
    }

    Totem::MetricsBackend::GroupHandle group;
    Totem::MetricsBackend::CounterHandle taskCount;

    using DefaultError = CoreError;
};

} // namespace Totem::TaskControllerRegistry::detail
