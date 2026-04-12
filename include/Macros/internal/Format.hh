#pragma once

#include "magic_enum/magic_enum.hpp" // IWYU pragma: export

#define SV_FMT "%*.*s"
#define INTERNAL_SV_ARG_PLAIN(sv) 0, static_cast<int>((sv).size()), (sv).data()
#define INTERNAL_SV_ARG_PAD(sv, pad)                                           \
    pad, static_cast<int>((sv).size()), (sv).data()
#define SV_ARG(...)                                                            \
    INTERNAL_GET_MACRO_2(__VA_ARGS__, INTERNAL_SV_ARG_PAD,                     \
                         INTERNAL_SV_ARG_PLAIN)(__VA_ARGS__)

#define INTERNAL_MAGIC_SV_ARG_CAST(T, sv)                                      \
    SV_ARG(magic_enum::enum_name(static_cast<T>(sv)))
#define INTERNAL_MAGIC_SV_ARG_NOCAST(sv) SV_ARG(magic_enum::enum_name(sv))
#define MAGIC_SV_ARG(...)                                                      \
    INTERNAL_GET_MACRO_2(__VA_ARGS__, INTERNAL_MAGIC_SV_ARG_CAST,              \
                         INTERNAL_MAGIC_SV_ARG_NOCAST)(__VA_ARGS__)

#define MAGIC_PUBSUB_SV_FMT                                                    \
    "record with topic " SV_FMT " from node " SV_FMT " with message ID %lu"
#define MAGIC_PUBSUB_SV_ARG(header)                                            \
    MAGIC_SV_ARG(Topic, header.topic), MAGIC_SV_ARG(NodeId, header.source),    \
        header.messageId
