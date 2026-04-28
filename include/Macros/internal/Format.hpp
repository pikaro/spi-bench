#pragma once

#include "magic_enum/magic_enum.hpp" // IWYU pragma: export

#define SV_FMT "%*.*s"
#define INTERNAL_SV_ARG_PLAIN(sv)                                             \
    0, static_cast<int>((sv).size()),                                          \
        ((sv).data() != nullptr ? (sv).data() : "")
#define INTERNAL_SV_ARG_PAD(sv, pad)                                           \
    pad, static_cast<int>((sv).size()),                                        \
        ((sv).data() != nullptr ? (sv).data() : "")
#define SV_ARG(...)                                                            \
    INTERNAL_GET_MACRO_2(__VA_ARGS__, INTERNAL_SV_ARG_PAD,                     \
                         INTERNAL_SV_ARG_PLAIN)(__VA_ARGS__)

#define INTERNAL_MAGIC_SV_ARG_CAST(T, v)                                       \
    SV_ARG(magic_enum::enum_name(static_cast<T>(v)))
#define INTERNAL_MAGIC_SV_ARG_NOCAST(v) SV_ARG(magic_enum::enum_name(v))
#define MAGIC_SV_ARG(...)                                                      \
    INTERNAL_GET_MACRO_2(__VA_ARGS__, INTERNAL_MAGIC_SV_ARG_CAST,              \
                         INTERNAL_MAGIC_SV_ARG_NOCAST)(__VA_ARGS__)

#define MAGIC_CHR_CAST(T, v)                                                   \
    magic_enum::enum_name(static_cast<T>(v))                                   \
        .data() // NOLINT(bugprone-suspicious-stringview-data-usage)
#define MAGIC_CHR_NOCAST(v)                                                    \
    magic_enum::enum_name(v)                                                   \
        .data() // NOLINT(bugprone-suspicious-stringview-data-usage)
#define MAGIC_CHR(...)                                                         \
    INTERNAL_GET_MACRO_2(__VA_ARGS__, MAGIC_CHR_CAST,                          \
                         MAGIC_CHR_NOCAST)(__VA_ARGS__)

#define MAGIC_PUBSUB_SV_FMT                                                    \
    "record with topic " SV_FMT " from node " SV_FMT                         \
    " with message ID %lu at %llu us"
#define MAGIC_PUBSUB_SV_ARG(header)                                            \
    MAGIC_SV_ARG(Topic, header.topic), MAGIC_SV_ARG(NodeId, header.source),    \
        header.messageId, static_cast<unsigned long long>(header.timestampUs)

#define ERR_FMT "[%d] %s::%s"
#define ERR_ARG(err)                                                           \
    (err).format().code, (err).format().domain, (err).format().name
