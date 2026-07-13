#pragma once

#include <cstdint>

struct CommandConfig {
    static constexpr uint8_t maxEntries = 64;
    static constexpr uint8_t maxEntriesPerClass = 8;
    static constexpr uint8_t maxNameLength = 16;
    static constexpr uint8_t maxTransports = 4;
    static constexpr uint8_t maxTokens = 14;
    static constexpr uint8_t maxLineLen = 255;
};
