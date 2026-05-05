#pragma once

#include "TaskController/Interfaces/Config.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

#ifndef LED_GROUP_COUNT
#define LED_GROUP_COUNT 1
#endif

#ifndef LED_GROUP_INDEX
#define LED_GROUP_INDEX 0
#endif

#ifndef LED_DATA_LINE_COUNT
#define LED_DATA_LINE_COUNT 2
#endif

#ifndef LED_DISPLAY_GENERIC_RENDERER
#define LED_DISPLAY_GENERIC_RENDERER 0
#endif

struct LedDisplayConfig {
    static constexpr size_t stripCount = 4;
    static constexpr size_t segmentsPerStrip = 4;
    static constexpr size_t ledsPerSegment = 46;
    static constexpr size_t ledsPerStrip = segmentsPerStrip * ledsPerSegment;
    static constexpr size_t totalPixelCount = stripCount * ledsPerStrip;
    static constexpr size_t spokeCount = stripCount * segmentsPerStrip;
    static constexpr size_t ringCount = ledsPerSegment;

    static constexpr size_t ledGroupCount = LED_GROUP_COUNT;
    static constexpr size_t ledGroupIndex = LED_GROUP_INDEX;
    static constexpr size_t dataLineCount = LED_DATA_LINE_COUNT;

    static_assert(ledGroupCount > 0, "LED_GROUP_COUNT must be greater than 0");
    static_assert(ledGroupIndex < ledGroupCount,
                  "LED_GROUP_INDEX must be less than LED_GROUP_COUNT");
    static_assert(totalPixelCount % ledGroupCount == 0,
                  "LED count must be divisible by LED group count");
    static_assert(dataLineCount > 0,
                  "LED_DATA_LINE_COUNT must be greater than 0");

    static constexpr size_t ownedPixelCount = totalPixelCount / ledGroupCount;

    static_assert(ownedPixelCount % dataLineCount == 0,
                  "Owned LED count must be divisible by data line count");

    static constexpr size_t dataLinePixelCount =
        ownedPixelCount / dataLineCount;

    static constexpr size_t commandQueueSize = 6;
    static constexpr size_t maxActiveAnimations = 4;
    static constexpr size_t animationCommandPayloadBytes = 32;

    static constexpr uint8_t targetFps = 100;
    static constexpr uint32_t frameIntervalMs = 1000U / targetFps;
    // Schedule slightly faster than target so RTOS jitter does not disable
    // FastLED temporal dithering by dropping below 100 FPS.
    static constexpr uint32_t taskIntervalMs = frameIntervalMs - 1U;
    static constexpr uint32_t frameBudgetUs = 1000000UL / targetFps;

    static_assert(taskIntervalMs > 0,
                  "LED task interval must be greater than 0");

    static constexpr uint8_t globalBrightness = 96;
    static constexpr bool temporalDithering = true;
    static constexpr bool genericRenderer = LED_DISPLAY_GENERIC_RENDERER != 0;

    static constexpr std::array<uint8_t, 2> outputPins{
        1,
        2,
    };

    static_assert(dataLineCount <= outputPins.size(),
                  "Add more LED output pins before increasing dataLineCount");

    static constexpr Totem::TaskController::Config task{
        .name = "LedDisplay",
        .priority = 3,
        .stackSize = 8192,
        .intervalMs = taskIntervalMs,
        .noCatchup = true,
        .useNotify = true,
        .notifyExpectTimeout = true,
        .notifyTimeoutMs = taskIntervalMs,
    };

    [[nodiscard]] static constexpr bool validate() { return true; }
};
