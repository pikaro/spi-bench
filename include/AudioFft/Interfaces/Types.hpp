#pragma once

#include "AudioSource/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Totem::AudioFft {

inline constexpr size_t fftBandCount = 8;
inline constexpr size_t fftMaxFrameHandlers = 4;
inline constexpr size_t fftMaxPeakHandlers = 4;
inline constexpr size_t fftMaxBeatHandlers = 4;

using AudioSource::AudioInfo;

enum class FftBand : uint8_t {
    SubBass,
    Bass,
    LowMid,
    Mid,
    HighMid,
    Presence,
    Brilliance,
    Air,
};

struct FftBandRange {
    uint16_t lowerHz = 0;
    uint16_t upperHz = 0;

    [[nodiscard]] bool validate() const {
        return lowerHz > 0 && upperHz > lowerHz;
    }
};

using FftBandRanges = std::array<FftBandRange, fftBandCount>;

constexpr FftBandRanges defaultFftBandRanges() {
    return FftBandRanges{{
        {.lowerHz = 40, .upperHz = 80},
        {.lowerHz = 80, .upperHz = 160},
        {.lowerHz = 160, .upperHz = 320},
        {.lowerHz = 320, .upperHz = 640},
        {.lowerHz = 640, .upperHz = 1250},
        {.lowerHz = 1250, .upperHz = 2500},
        {.lowerHz = 2500, .upperHz = 5000},
        {.lowerHz = 5000, .upperHz = 10000},
    }};
}

constexpr std::array<float, fftBandCount> flatFftBandGains() {
    return std::array<float, fftBandCount>{{
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
        1.0F,
    }};
}

constexpr std::array<float, fftBandCount> defaultFftBandGains() {
    return std::array<float, fftBandCount>{{
        1.0F,
        1.0F,
        1.05F,
        1.15F,
        1.35F,
        1.65F,
        2.1F,
        2.6F,
    }};
}

enum class FftWindow : uint8_t {
    None,
    Hamming,
    Hann,
};

constexpr bool isFftWindow(FftWindow window) {
    switch (window) {
    case FftWindow::None:
    case FftWindow::Hamming:
    case FftWindow::Hann:
        return true;
    default:
        return false;
    }
}

enum class FftMagnitudeMode : uint8_t {
    Average,
    Sum,
    Rms,
};

constexpr bool isFftMagnitudeMode(FftMagnitudeMode mode) {
    switch (mode) {
    case FftMagnitudeMode::Average:
    case FftMagnitudeMode::Sum:
    case FftMagnitudeMode::Rms:
        return true;
    default:
        return false;
    }
}

enum class FftMagnitudeCompression : uint8_t {
    Linear,
    Sqrt,
    Log1p,
};

constexpr bool isFftMagnitudeCompression(FftMagnitudeCompression mode) {
    switch (mode) {
    case FftMagnitudeCompression::Linear:
    case FftMagnitudeCompression::Sqrt:
    case FftMagnitudeCompression::Log1p:
        return true;
    default:
        return false;
    }
}

enum class FftWeighting : uint8_t {
    None,
    AWeighting,
};

constexpr bool isFftWeighting(FftWeighting weighting) {
    switch (weighting) {
    case FftWeighting::None:
    case FftWeighting::AWeighting:
        return true;
    default:
        return false;
    }
}

struct FftBandCalibrationConfig {
    std::array<float, fftBandCount> bandGains = defaultFftBandGains();

    [[nodiscard]] bool validate() const {
        for (const auto gain : bandGains) {
            if (!std::isfinite(gain) || gain < 0.0F) {
                return false;
            }
        }
        return true;
    }
};

struct FftPerceptualWeightingConfig {
    FftWeighting weighting = FftWeighting::AWeighting;
    float amount = 1.0F;

    [[nodiscard]] bool validate() const {
        return isFftWeighting(weighting) && std::isfinite(amount) &&
               amount >= 0.0F && amount <= 1.0F;
    }
};

struct FftMagnitudeCompressionConfig {
    FftMagnitudeCompression mode = FftMagnitudeCompression::Sqrt;
    float logScale = 1.0F;

    [[nodiscard]] bool validate() const {
        return isFftMagnitudeCompression(mode) && std::isfinite(logScale) &&
               logScale > 0.0F;
    }
};

struct FftSignalPipelineConfig {
    std::optional<FftBandCalibrationConfig> calibration =
        FftBandCalibrationConfig{};
    std::optional<FftPerceptualWeightingConfig> perceptualWeighting =
        std::nullopt;
    std::optional<FftMagnitudeCompressionConfig> compression =
        FftMagnitudeCompressionConfig{};

    [[nodiscard]] bool validate() const {
        return (!calibration.has_value() || calibration->validate()) &&
               (!perceptualWeighting.has_value() ||
                perceptualWeighting->validate()) &&
               (!compression.has_value() || compression->validate());
    }
};

enum class FftBackendLibrary : uint8_t {
    RealFft,
    EspressifFft,
};

constexpr bool isFftBackendLibrary(FftBackendLibrary library) {
    switch (library) {
    case FftBackendLibrary::RealFft:
    case FftBackendLibrary::EspressifFft:
        return true;
    default:
        return false;
    }
}

enum class FftMagnitudeCacheMode : uint8_t {
    PerBandAdaptive,
    TotalEnergyAdaptive,
};

constexpr bool isFftMagnitudeCacheMode(FftMagnitudeCacheMode mode) {
    switch (mode) {
    case FftMagnitudeCacheMode::PerBandAdaptive:
    case FftMagnitudeCacheMode::TotalEnergyAdaptive:
        return true;
    default:
        return false;
    }
}

struct FftMagnitudeCacheConfig {
    FftMagnitudeCacheMode mode = FftMagnitudeCacheMode::PerBandAdaptive;
    float floorAdaptAlpha = 0.0002F;
    float peakAdaptAlpha = 0.002F;
    float minimumRange = 0.25F;
    uint8_t scaledNoiseGate = 8;
    float scaledAttackAlpha = 0.65F;
    float scaledReleaseAlpha = 0.25F;

    [[nodiscard]] bool validate() const {
        return isFftMagnitudeCacheMode(mode) &&
               std::isfinite(floorAdaptAlpha) && floorAdaptAlpha >= 0.0F &&
               floorAdaptAlpha <= 1.0F && std::isfinite(peakAdaptAlpha) &&
               peakAdaptAlpha >= 0.0F && peakAdaptAlpha <= 1.0F &&
               std::isfinite(minimumRange) && minimumRange > 0.0F &&
               scaledNoiseGate < 255 && std::isfinite(scaledAttackAlpha) &&
               scaledAttackAlpha >= 0.0F && scaledAttackAlpha <= 1.0F &&
               std::isfinite(scaledReleaseAlpha) &&
               scaledReleaseAlpha >= 0.0F && scaledReleaseAlpha <= 1.0F;
    }
};

struct FftBandIndexRange {
    uint8_t lower = 0;
    uint8_t upper = 1;

    [[nodiscard]] bool validate() const {
        return lower <= upper && upper < fftBandCount;
    }
};

inline constexpr size_t peakGroupCount = 3;

enum class PeakGroup : uint8_t {
    Bass,
    Mid,
    High,
};

constexpr bool isPeakGroup(PeakGroup group) {
    switch (group) {
    case PeakGroup::Bass:
    case PeakGroup::Mid:
    case PeakGroup::High:
        return true;
    default:
        return false;
    }
}

constexpr size_t peakGroupIndex(PeakGroup group) {
    switch (group) {
    case PeakGroup::Bass:
        return 0;
    case PeakGroup::Mid:
        return 1;
    case PeakGroup::High:
        return 2;
    default:
        return peakGroupCount;
    }
}

enum class BeatEventKind : uint8_t {
    ExpectedHit,
    ExpectedMiss,
    Reacquired,
    Lost,
};

struct FftBandValue {
    FftBand band = FftBand::SubBass;
    uint16_t lowerHz = 0;
    uint16_t upperHz = 0;
    uint16_t lowerBin = 0;
    uint16_t upperBin = 0;
    float magnitude = 0.0F;
    float weightedMagnitude = 0.0F;
    float floor = 0.0F;
    float peak = 0.0F;
    uint8_t scaled = 0;
};

struct FftResult {
    AudioInfo audio{};
    uint32_t sequence = 0;
    uint64_t timestampUs = 0;
    uint16_t length = 0;
    uint16_t stride = 0;
    std::array<FftBandValue, fftBandCount> bands{};
};

struct PeakResult {
    PeakGroup group = PeakGroup::Bass;
    FftBandIndexRange bands{};
    uint32_t frameSequence = 0;
    uint64_t timestampUs = 0;
    uint8_t energy = 0;
    float ratePerMinute = 0.0F;
};

struct BeatResult {
    BeatEventKind kind = BeatEventKind::Lost;
    uint8_t bpm = 0;
    uint8_t confidence = 0;
    uint8_t energy = 0;
    uint32_t sequence = 0;
    uint64_t timestampUs = 0;
};

struct PeakGroupStatus {
    bool hasPeak = false;
    uint32_t peaks = 0;
    uint32_t ratePerMinuteHundredths = 0;
    uint8_t energy = 0;
    uint32_t lastPeakAgeMs = 0;
};

struct PeakDetectorStatus {
    PeakGroup indicatorGroup = PeakGroup::Bass;
    PeakGroupStatus indicator{};
    std::array<PeakGroupStatus, peakGroupCount> groups{};
};

struct TempoTrackerStatus {
    bool locked = false;
    BeatEventKind lastKind = BeatEventKind::Lost;
    uint8_t bpm = 0;
    uint8_t confidence = 0;
    uint32_t beats = 0;
    uint32_t hits = 0;
    uint32_t misses = 0;
    uint32_t reacquired = 0;
    uint32_t lost = 0;
    uint32_t lastEventAgeMs = 0;
};

struct BackgroundCalibrationStatus {
    bool active = false;
    uint32_t requestId = 0;
    uint32_t requests = 0;
    uint32_t completed = 0;
    uint32_t frames = 0;
    uint32_t remainingMs = 0;
};

struct FftRuntimeStats {
    uint32_t copyCalls = 0;
    uint32_t copiedBytes = 0;
    uint32_t emptyCopies = 0;
    uint32_t readinessProbes = 0;
    uint32_t sourceUnavailableSkips = 0;
    uint32_t frames = 0;
    uint32_t droppedFrames = 0;
    uint32_t peaks = 0;
    uint32_t beats = 0;
    uint32_t maxCopyUs = 0;
    uint32_t maxReadinessProbeUs = 0;
    uint32_t maxFrameUs = 0;
    uint32_t maxBandComputeUs = 0;
    uint32_t maxMagnitudeCacheUs = 0;
    uint32_t maxFrameDispatchUs = 0;
    uint32_t maxPeakUpdateUs = 0;
    uint32_t maxPeakDispatchUs = 0;
    uint32_t maxTempoUpdateUs = 0;
    uint32_t maxBeatDispatchUs = 0;
    uint32_t backgroundCalibrationRequests = 0;
    uint32_t backgroundCalibrationCompleted = 0;
    uint32_t backgroundCalibrationFrames = 0;
};

using FftFrameCallback = ReturnCode (*)(void *owner, const FftResult &frame);
using PeakCallback = ReturnCode (*)(void *owner, const PeakResult &event);
using BeatCallback = ReturnCode (*)(void *owner, const BeatResult &event);

struct FftResultHandler {
    void *owner = nullptr;
    FftFrameCallback callback = nullptr;

    [[nodiscard]] bool valid() const { return callback != nullptr; }
};

struct PeakResultHandler {
    void *owner = nullptr;
    PeakCallback callback = nullptr;

    [[nodiscard]] bool valid() const { return callback != nullptr; }
};

struct BeatResultHandler {
    void *owner = nullptr;
    BeatCallback callback = nullptr;

    [[nodiscard]] bool valid() const { return callback != nullptr; }
};

} // namespace Totem::AudioFft
