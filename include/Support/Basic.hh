#pragma once

#include <cstddef>
#include <cstdint>

[[nodiscard]] inline static size_t hash(const char *str, size_t len) {
    // 64-bit FNV-1a
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(str[i]);
        hash *= 1099511628211ULL;
    }
    return static_cast<size_t>(hash);
}
