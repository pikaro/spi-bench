#pragma once

#include "Macros/internal/Fail.hh"

#define REGISTER_METRICS_GROUP(cls, group)                                     \
    ABORT_IF_UNEXPECTED(group,                                                 \
                        ::Metrics::registrar().addGroup(CONCAT(group, Def)),   \
                        "Failed to register metrics group for " #cls);

#define REGISTER_METRIC(cls, metric, type, group)                              \
    ABORT_IF_UNEXPECTED(                                                       \
        metric,                                                                \
        ::Metrics::registrar().CONCAT(add, type)(                              \
            MetricDesc::withGroup(CONCAT(metric, Def), group.key())),          \
        "Failed to register " #metric " " #type " metric for " #cls);
