// IWYU pragma: private

#pragma once

#include "Macros/internal/Fail.hpp"

#define REGISTER_METRICS_GROUP(cls, group)                                     \
    ABORT_IF_UNEXPECTED(                                                       \
        group,                                                                 \
        ::MetricsService::registrar().addGroup(CONCAT(group, Def),             \
                                               minimumLogLevel),               \
        "Failed to register metrics group for " #cls);

#define REGISTER_METRIC(cls, metric, type, group)                              \
    ABORT_IF_UNEXPECTED(metric,                                                \
                        ::MetricsService::registrar().CONCAT(add, type)(       \
                            group.key(), CONCAT(metric, Def),                  \
                            groupDef.logLevel, minimumLogLevel),               \
                        "Failed to register " #metric " " #type                \
                        " metric for " #cls);

#define METRIC_INCR(metricName, incrCount)                                     \
    do {                                                                       \
        if constexpr (enabled) {                                               \
            FAIL_IF_ERR_VOID(                                                  \
                ::MetricsService::recorder().increment(metricName, incrCount), \
                "Failed to increment metric " #metricName);                    \
        }                                                                      \
    } while (0)

#define METRIC_DECR(metricName, decrCount)                                     \
    do {                                                                       \
        if constexpr (enabled) {                                               \
            FAIL_IF_ERR_VOID(                                                  \
                ::MetricsService::recorder().decrement(metricName, decrCount), \
                "Failed to decrement metric " #metricName);                    \
        }                                                                      \
    } while (0)

#define METRIC_SET(metricName, metricValue)                                    \
    do {                                                                       \
        if constexpr (enabled) {                                               \
            FAIL_IF_ERR_VOID(                                                  \
                ::MetricsService::recorder().set(metricName, metricValue),     \
                "Failed to set metric " #metricName);                          \
        }                                                                      \
    } while (0)
