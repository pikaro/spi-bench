#pragma once

#include "LedDisplay/Interfaces/Color.hpp"
#include "LedDisplay/Interfaces/PresentBufferMode.hpp"
#include "LedTopology/detail/DenseUmbrella.hpp"
#include "LedTopology/detail/Umbrella.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

#ifndef LED_GROUP_COUNT
#define LED_GROUP_COUNT 1
#endif

#ifndef LED_NODE_GROUP_COUNT
#define LED_NODE_GROUP_COUNT 1
#endif

#ifndef LED_NODE_GROUP0
#define LED_NODE_GROUP0 0
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

#ifndef LED_TOPOLOGY_DENSE_UMBRELLA
#define LED_TOPOLOGY_DENSE_UMBRELLA 0
#endif

#ifndef LED_OUTPUT_SK9822_SPI
#define LED_OUTPUT_SK9822_SPI 0
#endif

struct LedTopologyStaticConfig {
#if LED_TOPOLOGY_DENSE_UMBRELLA
    using Topology = Totem::LedTopology::detail::DenseUmbrella;
#else
    using Topology = Totem::LedTopology::detail::Umbrella;
#endif

    static constexpr size_t stripCount = Topology::stripCount;
    static constexpr size_t segmentsPerStrip = Topology::segmentsPerStrip;
    static constexpr size_t ledsPerSegment = Topology::ledsPerSegment;
    static constexpr size_t ledsPerStrip = Topology::ledsPerStrip;
    static constexpr size_t totalPixelCount = Topology::totalPixelCount;
    static constexpr size_t spokeCount = Topology::spokeCount;
    static constexpr size_t ringCount = Topology::ringCount;
};

struct LedGeometryStaticConfig : LedTopologyStaticConfig {
    static constexpr uint16_t centerGapDiameterMm =
        Topology::centerGapDiameterMm;
    static constexpr uint16_t radialStripLengthMm =
        Topology::radialStripLengthMm;
    static constexpr uint16_t innerRadiusMm = Topology::innerRadiusMm;
    static constexpr uint16_t outerRadiusMm = Topology::outerRadiusMm;

    static_assert(radialStripLengthMm > 0,
                  "LED radial strip length must be greater than zero");
    static_assert(outerRadiusMm > innerRadiusMm,
                  "LED outer radius must exceed inner radius");
};

struct LedOwnershipStaticConfig : LedGeometryStaticConfig {
    static constexpr size_t ledGroupCount = LED_GROUP_COUNT;
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
    static constexpr bool sk9822SpiOutput = LED_OUTPUT_SK9822_SPI != 0;
    static constexpr Totem::LedDisplay::HsvConversion hsvConversion =
        Totem::LedDisplay::HsvConversion::Rainbow;

    static constexpr std::array<uint8_t, 2> outputPins{
        1,
        2,
    };
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
    static constexpr uint8_t targetFps = 100;
    static constexpr uint32_t frameIntervalMs = 1000U / targetFps;
    static constexpr uint32_t taskIntervalMs = frameIntervalMs - 1U;
    static constexpr uint32_t defaultFrameBudgetUs = 1000000UL / targetFps;

    static_assert(taskIntervalMs > 0,
                  "LED task interval must be greater than 0");
};

struct LedDisplayConfig : LedOutputStaticConfig,
                          LedAnimationBounds,
                          LedPipelineBounds {};
