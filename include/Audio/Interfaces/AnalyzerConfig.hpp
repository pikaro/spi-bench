#pragma once

#include "Audio/Interfaces/BeatTrackerConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Totem::Audio {

struct FftAnalyzerConfig {
    uint16_t length = 4096;
    uint16_t stride = 4096;
    uint8_t channel = 0;
    FftBandRanges bands = defaultFftBandRanges();
    std::array<float, fftBandCount> bandGains = flatFftBandGains();
    FftWindow window = FftWindow::Hamming;
    FftMagnitudeMode magnitudeMode = FftMagnitudeMode::Average;
    FftWeighting weighting = FftWeighting::None;
    FftMagnitudeCacheConfig magnitudeCache{};
    BeatTrackerConfig beatTracker{};
    uint16_t copyBufferSizeBytes = 1024;
    Totem::TaskController::Config task{
        .name = "AudioFft",
        .priority = 4,
        .stackSize = 8192,
        .intervalMs = 1,
        .noCatchup = true,
    };

    [[nodiscard]] bool validate() const {
        const bool lengthPowerOfTwo =
            length > 0 && (length & (length - 1U)) == 0U;
        if (!lengthPowerOfTwo || stride == 0 || stride > length ||
            copyBufferSizeBytes == 0 || !magnitudeCache.validate() ||
            !beatTracker.validate() || !task.validate()) {
            return false;
        }

        uint16_t previousUpper = 0;
        for (size_t i = 0; i < fftBandCount; ++i) {
            if (!bands[i].validate() || !std::isfinite(bandGains[i]) ||
                bandGains[i] < 0.0F) {
                return false;
            }
            if (i > 0 && bands[i].lowerHz < previousUpper) {
                return false;
            }
            previousUpper = bands[i].upperHz;
        }
        return true;
    }
};

} // namespace Totem::Audio
