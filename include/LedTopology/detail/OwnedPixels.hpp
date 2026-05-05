#pragma once

#include "LedTopology/Interfaces/Types.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include <array>
#include <cstddef>

namespace Totem::LedTopology::detail {

struct OwnedPixels {
    static constexpr PhysicalPixelIndex physicalStart =
        static_cast<PhysicalPixelIndex>(LedDisplayConfig::ownedPixelCount *
                                        LedDisplayConfig::ledGroupIndex);
    static constexpr PhysicalPixelIndex physicalEnd =
        static_cast<PhysicalPixelIndex>(physicalStart +
                                        LedDisplayConfig::ownedPixelCount);

    [[nodiscard]] static constexpr bool owns(PhysicalPixelIndex pixel) {
        return pixel >= physicalStart && pixel < physicalEnd;
    }

    [[nodiscard]] static constexpr LocalPixelIndex
    localIndex(PhysicalPixelIndex pixel) {
        return static_cast<LocalPixelIndex>(pixel - physicalStart);
    }

    [[nodiscard]] static constexpr PhysicalPixelIndex
    physicalIndex(LocalPixelIndex pixel) {
        return static_cast<PhysicalPixelIndex>(physicalStart + pixel);
    }

    [[nodiscard]] static constexpr DataLineSpan dataLine(uint8_t line) {
        const auto localStart =
            static_cast<LocalPixelIndex>(line *
                                         LedDisplayConfig::dataLinePixelCount);
        return DataLineSpan{
            .line = line,
            .physicalStart = physicalIndex(localStart),
            .localStart = localStart,
            .count = static_cast<uint16_t>(LedDisplayConfig::dataLinePixelCount),
        };
    }

    [[nodiscard]] static consteval auto dataLines() {
        std::array<DataLineSpan, LedDisplayConfig::dataLineCount> lines{};
        for (size_t i = 0; i < lines.size(); ++i) {
            lines[i] = dataLine(static_cast<uint8_t>(i));
        }
        return lines;
    }
};

} // namespace Totem::LedTopology::detail
