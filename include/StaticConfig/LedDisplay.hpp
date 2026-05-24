#pragma once

#include "LedDisplay/Interfaces/PresentBufferMode.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

#ifndef LED_GROUP_COUNT
#define LED_GROUP_COUNT 1
#endif

#ifndef LED_GROUP_INDEX
#define LED_GROUP_INDEX 0
#endif

#ifndef LED_NODE_GROUP_COUNT
#define LED_NODE_GROUP_COUNT 1
#endif

#ifndef LED_NODE_GROUP0
#define LED_NODE_GROUP0 LED_GROUP_INDEX
#endif

#ifndef LED_NODE_GROUP1
#define LED_NODE_GROUP1 0
#endif

#ifndef LED_NODE_GROUP2
#define LED_NODE_GROUP2 0
#endif

#ifndef LED_NODE_GROUP3
#define LED_NODE_GROUP3 0
#endif

#ifndef LED_DATA_LINE_COUNT
#define LED_DATA_LINE_COUNT 2
#endif

#ifndef LED_DISPLAY_GENERIC_RENDERER
#define LED_DISPLAY_GENERIC_RENDERER 0
#endif

struct LedTopologyStaticConfig {
    static constexpr size_t stripCount = 4;
    static constexpr size_t segmentsPerStrip = 4;
    static constexpr size_t ledsPerSegment = 46;
    static constexpr size_t ledsPerStrip = segmentsPerStrip * ledsPerSegment;
    static constexpr size_t totalPixelCount = stripCount * ledsPerStrip;
    static constexpr size_t spokeCount = stripCount * segmentsPerStrip;
    static constexpr size_t ringCount = ledsPerSegment;
};

struct LedOwnershipStaticConfig : LedTopologyStaticConfig {
    static constexpr size_t ledGroupCount = LED_GROUP_COUNT;
    static constexpr size_t ledGroupIndex = LED_GROUP_INDEX;
    static constexpr size_t nodeGroupCount = LED_NODE_GROUP_COUNT;
    static constexpr std::array<size_t, nodeGroupCount> nodeGroups = [] {
        std::array<size_t, nodeGroupCount> groups{};
        constexpr std::array<size_t, 4> configuredGroups{
            LED_NODE_GROUP0,
            LED_NODE_GROUP1,
            LED_NODE_GROUP2,
            LED_NODE_GROUP3,
        };
        for (size_t i = 0; i < nodeGroupCount; ++i) {
            groups[i] = configuredGroups[i];
        }
        return groups;
    }();

    static_assert(ledGroupCount > 0, "LED_GROUP_COUNT must be greater than 0");
    static_assert(ledGroupIndex < ledGroupCount,
                  "LED_GROUP_INDEX must be less than LED_GROUP_COUNT");
    static_assert(nodeGroupCount > 0,
                  "LED_NODE_GROUP_COUNT must be greater than 0");
    static_assert(nodeGroupCount <= 4,
                  "Add more LED_NODE_GROUPn macros before increasing "
                  "LED_NODE_GROUP_COUNT");
    static_assert(totalPixelCount % ledGroupCount == 0,
                  "LED count must be divisible by LED group count");

    static_assert(
        [] consteval {
            constexpr std::array<size_t, 4> configuredGroups{
                LED_NODE_GROUP0,
                LED_NODE_GROUP1,
                LED_NODE_GROUP2,
                LED_NODE_GROUP3,
            };
            for (size_t i = 0; i < nodeGroupCount; ++i) {
                if (configuredGroups[i] >= ledGroupCount) {
                    return false;
                }
                for (size_t j = i + 1; j < nodeGroupCount; ++j) {
                    if (configuredGroups[i] == configuredGroups[j]) {
                        return false;
                    }
                }
            }
            return true;
        }(),
        "LED_NODE_GROUPn values must be unique valid group IDs");

    static constexpr size_t groupPixelCount = totalPixelCount / ledGroupCount;
    static constexpr size_t ownedPixelCount = groupPixelCount * nodeGroupCount;
};

struct LedOutputStaticConfig : LedOwnershipStaticConfig {
    static constexpr size_t dataLineCount = LED_DATA_LINE_COUNT;
    static constexpr bool genericRenderer = LED_DISPLAY_GENERIC_RENDERER != 0;

    static_assert(dataLineCount > 0,
                  "LED_DATA_LINE_COUNT must be greater than 0");
    static_assert(dataLineCount >= nodeGroupCount,
                  "Data lines must cover every explicitly owned LED group");
    static_assert(dataLineCount % nodeGroupCount == 0,
                  "Data line count must be divisible by owned group count");

    static constexpr size_t dataLinesPerNodeGroup =
        dataLineCount / nodeGroupCount;

    static_assert(groupPixelCount % dataLinesPerNodeGroup == 0,
                  "Owned LED groups must split evenly across data lines");

    static constexpr size_t dataLinePixelCount =
        groupPixelCount / dataLinesPerNodeGroup;

    static constexpr std::array<uint8_t, 2> outputPins{
        1,
        2,
    };

    static_assert(dataLineCount <= outputPins.size(),
                  "Add more LED output pins before increasing dataLineCount");
};

struct LedAnimationBounds {
    static constexpr size_t commandQueueSize = 32;
    static constexpr size_t animationPublishPoolSize = 32;
    static constexpr size_t maxActiveAnimations = 32;
    static constexpr size_t animationCommandPayloadBytes = 32;
};

struct LedPipelineBounds {
    static constexpr Totem::LedDisplay::PresentBufferMode presentBufferMode =
        Totem::LedDisplay::PresentBufferMode::Triple;
    static constexpr uint8_t targetFps = 125;
    static constexpr uint32_t frameIntervalMs = 1000U / targetFps;
    static constexpr uint32_t taskIntervalMs = frameIntervalMs - 1U;
    static constexpr uint32_t defaultFrameBudgetUs = 1000000UL / targetFps;

    static_assert(taskIntervalMs > 0,
                  "LED task interval must be greater than 0");
};

struct LedDisplayConfig : LedOutputStaticConfig,
                          LedAnimationBounds,
                          LedPipelineBounds {};
