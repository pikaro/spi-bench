#pragma once

#include "LedTopology/Interfaces/Types.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include <cstddef>
#include <optional>

namespace Totem::LedTopology::detail {

struct OwnedPixels {
    [[nodiscard]] static constexpr bool owns(PhysicalPixelIndex pixel) {
        return ownedGroupSlot(pixel).has_value();
    }

    [[nodiscard]] static constexpr LocalPixelIndex
    localIndex(PhysicalPixelIndex pixel) {
        const auto slot = *ownedGroupSlot(pixel);
        const auto offset =
            static_cast<size_t>(pixel) % LedDisplayConfig::groupPixelCount;
        return static_cast<LocalPixelIndex>(
            (slot * LedDisplayConfig::groupPixelCount) + offset);
    }

    [[nodiscard]] static constexpr PhysicalPixelIndex
    physicalIndex(LocalPixelIndex pixel) {
        const auto slot =
            static_cast<size_t>(pixel) / LedDisplayConfig::groupPixelCount;
        const auto offset =
            static_cast<size_t>(pixel) % LedDisplayConfig::groupPixelCount;
        return static_cast<PhysicalPixelIndex>(
            (LedDisplayConfig::nodeGroups[slot] *
             LedDisplayConfig::groupPixelCount) +
            offset);
    }

  private:
    [[nodiscard]] static constexpr std::optional<size_t>
    ownedGroupSlot(PhysicalPixelIndex pixel) {
        const auto group =
            static_cast<size_t>(pixel) / LedDisplayConfig::groupPixelCount;
        for (size_t i = 0; i < LedDisplayConfig::nodeGroupCount; ++i) {
            if (LedDisplayConfig::nodeGroups[i] == group) {
                return i;
            }
        }
        return std::nullopt;
    }
};

} // namespace Totem::LedTopology::detail
