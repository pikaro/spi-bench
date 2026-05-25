#pragma once

#include <cstdint>

namespace Totem::LedRender {

inline constexpr char traceMagic[4] = {'T', 'L', 'E', 'D'};
inline constexpr uint16_t traceVersion = 1;

enum TracePlane : uint32_t {
    PlaneHsvFinal = 1U << 0U,
    PlaneRgbFinal = 1U << 1U,
    PlaneHsvScratch = 1U << 2U,
};

struct TraceHeader {
    char magic[4];
    uint16_t version;
    uint16_t headerSize;
    uint32_t flags;
    uint32_t frameCount;
    uint32_t firstFrame;
    uint32_t fpsNum;
    uint32_t fpsDen;
    uint32_t frameStepUs;
    uint16_t stripCount;
    uint16_t segmentsPerStrip;
    uint16_t spokeCount;
    uint16_t ringCount;
    uint32_t pixelCount;
    uint32_t planeMask;
    uint32_t bytesPerFrame;
    uint32_t metadataBytes;
    uint32_t reserved[10];
};

static_assert(sizeof(TraceHeader) == 96,
              "TraceHeader is part of the on-disk format");

} // namespace Totem::LedRender
