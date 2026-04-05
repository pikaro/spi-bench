#pragma once

#include "TaskController/Facade.hh"
#include <cstdint>

struct CommandConfig {
    static constexpr uint8_t maxEntries = 64;
    static constexpr uint8_t maxNameLength = 16;
    static constexpr uint8_t maxTransports = 4;
    static constexpr uint8_t maxTokens = 10;
    static constexpr uint8_t maxLineLen = 128;

    static constexpr Totem::TaskController::Config task{
        .name = "Command",
        .stackSize = 8192,
        .intervalMs = 10,
    };
};
