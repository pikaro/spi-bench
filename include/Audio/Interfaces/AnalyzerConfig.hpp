#pragma once

#include "Audio/Interfaces/BeatTrackerConfig.hpp"
#include "Audio/Interfaces/Types.hpp"
#include "StaticConfig/Stacks.hpp"
#include "TaskController/Interfaces/Config.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Totem::Audio {

struct FftAnalyzerConfig {
    FftBackendLibrary backend = FftBackendLibrary::EspressifFft;
    uint16_t length = 2048;
    uint16_t stride = 2048;
    uint8_t channel = 0;
    FftBandRanges bands = defaultFftBandRanges();
    FftWindow window = FftWindow::Hamming;
    FftMagnitudeMode magnitudeMode = FftMagnitudeMode::Average;
    FftSignalPipelineConfig signalPipeline{};
    FftMagnitudeCacheConfig magnitudeCache{};
    BeatTrackerConfig beatTracker{};
    BeatResultHandler beatIndicator{};
    uint16_t copyBufferSizeBytes = 2048;
    Totem::TaskController::Config task{
        .name = "AudioFft",
        .priority = 4,
        .stackSize = StaticConfig::TaskStacks::audioFft,
        .intervalMs = 2,
        .noCatchup = true,
    };

    [[nodiscard]] bool validate() const {
        const bool lengthPowerOfTwo =
            length > 0 && (length & (length - 1U)) == 0U;
        if (!lengthPowerOfTwo || stride == 0 || stride > length ||
            !isFftBackendLibrary(backend) || !isFftWindow(window) ||
            !isFftMagnitudeMode(magnitudeMode) || copyBufferSizeBytes == 0 ||
            !signalPipeline.validate() || !magnitudeCache.validate() ||
            !beatTracker.validate() || !task.validate()) {
            return false;
        }

        uint16_t previousUpper = 0;
        for (size_t i = 0; i < fftBandCount; ++i) {
            if (!bands[i].validate()) {
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
