#pragma once

#define SV_FMT "%*.*s"
#define INTERNAL_SV_ARG_PLAIN(sv) 0, static_cast<int>((sv).size()), (sv).data()
#define INTERNAL_SV_ARG_PAD(sv, pad)                                           \
    pad, static_cast<int>((sv).size()), (sv).data()
#define SV_ARG(...)                                                            \
    INTERNAL_GET_MACRO_2(__VA_ARGS__, INTERNAL_SV_ARG_PAD,                     \
                         INTERNAL_SV_ARG_PLAIN)(__VA_ARGS__)
