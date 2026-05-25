// IWYU pragma: private

#pragma once

#include "Macros/internal/Fail.hpp"
#include <optional>

#define REGISTER_METRICS_GROUP(cls, group)                                     \
    ABORT_IF_UNEXPECTED(group,                                                 \
                        ::MetricsService::registrar().addGroup(                \
                            CONCAT(group, Def),                                \
                            metrics_enabled(component, CONCAT(group, Def))),   \
                        "Failed to register metrics group for " #cls);

#define REGISTER_METRIC(cls, metric, type, group)                              \
    ABORT_IF_UNEXPECTED(metric,                                                \
                        ::MetricsService::registrar().CONCAT(add, type)(       \
                            group.key(), CONCAT(metric, Def),                  \
                            metrics_enabled(component, CONCAT(group, Def))),   \
                        "Failed to register " #metric " " #type                \
                        " metric for " #cls);

#define METRIC_INCR(group, metricName, incrCount)                              \
    do {                                                                       \
        if constexpr (metrics_enabled(component, CONCAT(group, Def))) {        \
            FAIL_IF_ERR_VOID(                                                  \
                ::MetricsService::recorder().increment(metricName, incrCount), \
                "Failed to increment metric " #metricName);                    \
        }                                                                      \
    } while (0)

#define METRIC_DECR(group, metricName, decrCount)                              \
    do {                                                                       \
        if constexpr (metrics_enabled(component, CONCAT(group, Def))) {        \
            FAIL_IF_ERR_VOID(                                                  \
                ::MetricsService::recorder().decrement(metricName, decrCount), \
                "Failed to decrement metric " #metricName);                    \
        }                                                                      \
    } while (0)

#define METRIC_SET(group, metricName, metricValue)                             \
    do {                                                                       \
        if constexpr (metrics_enabled(component, CONCAT(group, Def))) {        \
            FAIL_IF_ERR_VOID(                                                  \
                ::MetricsService::recorder().set(metricName, metricValue),     \
                "Failed to set metric " #metricName);                          \
        }                                                                      \
    } while (0)

// Avoid function-local static guards in hot paths; prewarm from begin code.
#define DEFINE_PREWARMED_METRICS_ACCESSORS(Type, accessor, prewarm)            \
    namespace metrics_storage {                                                \
    inline std::optional<Type> CONCAT(accessor, Instance);                     \
    }                                                                          \
    inline void prewarm() {                                                    \
        if (metrics_storage::CONCAT(accessor, Instance).has_value()) {         \
            return;                                                            \
        }                                                                      \
        metrics_storage::CONCAT(accessor, Instance).emplace(Type::create());   \
    }                                                                          \
    inline Type &accessor() {                                                  \
        ABORT_IF(!metrics_storage::CONCAT(accessor, Instance).has_value(),     \
                 "Metrics accessor " #accessor " used before " #prewarm);    \
        return *metrics_storage::CONCAT(accessor, Instance);                   \
    }
