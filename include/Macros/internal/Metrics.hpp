#pragma once

#include "Macros/internal/Fail.hpp"

#define REGISTER_METRICS_GROUP(cls, group)                                     \
    ABORT_IF_UNEXPECTED(                                                       \
        group, ::MetricsService::registrar().addGroup(CONCAT(group, Def)),     \
        "Failed to register metrics group for " #cls);

#define REGISTER_METRIC(cls, metric, type, group)                              \
    ABORT_IF_UNEXPECTED(metric,                                                \
                        ::MetricsService::registrar().CONCAT(add, type)(       \
                            group.key(), CONCAT(metric, Def)),                 \
                        "Failed to register " #metric " " #type                \
                        " metric for " #cls);
