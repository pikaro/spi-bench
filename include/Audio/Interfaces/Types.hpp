#pragma once

#include "TaskController/Interfaces/Config.hpp"
#include "Types/Error.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Totem::Audio {

inline constexpr size_t fftBandCount = 8;
inline constexpr size_t fftMaxFrameHandlers = 4;
inline constexpr size_t fftMaxBeatHandlers = 4;
inline constexpr size_t i2sMaxProbeBytes = 256;

enum class I2SDevicePreset : uint8_t {
    LegacySoundCard,
    Custom,
};

enum class I2SRole : uint8_t {
    Slave,
    Master,
};

enum class I2SFormat : uint8_t {
    Standard,
    Lsb,
    Msb,
    Philips,
    RightJustified,
    LeftJustified,
    Pcm,
};

enum class I2SChannelSelect : uint8_t {
    Stereo,
    Left,
    Right,
};

struct AudioInfo {
    uint32_t sampleRate = 96000;
    uint16_t channels = 1;
    uint8_t bitsPerSample = 32;

    [[nodiscard]] bool validate() const {
        return sampleRate > 0 && sampleRate <= 384000 && channels > 0 &&
               channels <= 8 &&
               (bitsPerSample == 8 || bitsPerSample == 16 ||
                bitsPerSample == 24 || bitsPerSample == 32);
    }

    [[nodiscard]] uint16_t bytesPerFrame() const {
        return static_cast<uint16_t>((bitsPerSample / 8U) * channels);
    }
};

struct I2SPins {
    int bitClock = -1;
    int wordSelect = -1;
    int dataIn = -1;

    [[nodiscard]] bool validate() const {
        return bitClock >= 0 && wordSelect >= 0 && dataIn >= 0;
    }
};

struct I2SDeviceConfig {
    AudioInfo audio{};
    I2SPins pins{};
    I2SRole role = I2SRole::Slave;
    I2SFormat format = I2SFormat::Lsb;
    I2SChannelSelect channel = I2SChannelSelect::Left;
    uint8_t port = 0;
    uint16_t dmaBufferSize = 512;
    uint8_t dmaBufferCount = 6;
    bool useApll = true;

    [[nodiscard]] bool validate() const {
        return audio.validate() && pins.validate() && dmaBufferSize > 0 &&
               dmaBufferCount > 0;
    }
};

struct I2SSourceReadinessConfig {
    uint16_t probeBytes = 64;
    uint16_t probeIntervalMs = 250;
    uint16_t readTimeoutMs = 1;
    uint8_t emptyReadsBeforeOffline = 4;

    [[nodiscard]] bool validate() const {
        return probeBytes > 0 && probeBytes <= i2sMaxProbeBytes &&
               probeIntervalMs > 0 && readTimeoutMs <= 100 &&
               emptyReadsBeforeOffline > 0;
    }
};

inline constexpr I2SDeviceConfig legacySoundCardI2SDeviceConfig() {
    return I2SDeviceConfig{
        .audio =
            {
                .sampleRate = 96000,
                .channels = 1,
                .bitsPerSample = 32,
            },
        .pins =
            {
                .bitClock = 10,
                .wordSelect = 21,
                .dataIn = 20,
            },
        .role = I2SRole::Slave,
        .format = I2SFormat::Lsb,
        .channel = I2SChannelSelect::Left,
        .port = 0,
        .dmaBufferSize = 512,
        .dmaBufferCount = 6,
        .useApll = true,
    };
}

struct I2SSourceConfig {
    I2SDevicePreset device = I2SDevicePreset::LegacySoundCard;
    I2SDeviceConfig customDevice{};
    I2SSourceReadinessConfig readiness{};

    [[nodiscard]] constexpr I2SDeviceConfig resolvedDevice() const {
        switch (device) {
        case I2SDevicePreset::LegacySoundCard:
            return legacySoundCardI2SDeviceConfig();
        case I2SDevicePreset::Custom:
            return customDevice;
        default:
            return {};
        }
    }

    [[nodiscard]] bool validate() const {
        return resolvedDevice().validate() && readiness.validate();
    }
};

struct I2SSourceStatus {
    bool ready = false;
    uint32_t probes = 0;
    uint32_t emptyReads = 0;
    uint32_t observedBytes = 0;
    uint32_t lastDataMs = 0;
};

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

inline constexpr FftBandRanges defaultFftBandRanges() {
    return FftBandRanges{{
        {.lowerHz = 20, .upperHz = 60},
        {.lowerHz = 60, .upperHz = 150},
        {.lowerHz = 150, .upperHz = 400},
        {.lowerHz = 400, .upperHz = 1000},
        {.lowerHz = 1000, .upperHz = 2500},
        {.lowerHz = 2500, .upperHz = 5000},
        {.lowerHz = 5000, .upperHz = 10000},
        {.lowerHz = 10000, .upperHz = 20000},
    }};
}

inline constexpr std::array<float, fftBandCount> flatFftBandGains() {
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

enum class FftWindow : uint8_t {
    None,
    Hamming,
    Hann,
};

enum class FftMagnitudeMode : uint8_t {
    Average,
    Sum,
    Rms,
};

enum class FftWeighting : uint8_t {
    None,
    AWeighting,
};

struct FftMagnitudeCacheConfig {
    float floorAdaptAlpha = 0.005F;
    float peakAdaptAlpha = 0.02F;
    float minimumRange = 1.0F;

    [[nodiscard]] bool validate() const {
        return floorAdaptAlpha >= 0.0F && floorAdaptAlpha <= 1.0F &&
               peakAdaptAlpha >= 0.0F && peakAdaptAlpha <= 1.0F &&
               minimumRange > 0.0F;
    }
};

struct FftBandIndexRange {
    uint8_t lower = 0;
    uint8_t upper = 1;

    [[nodiscard]] bool validate() const {
        return lower <= upper && upper < fftBandCount;
    }
};

enum class BeatEventKind : uint8_t {
    Detected,
};

struct BeatTrackerConfig {
    FftBandIndexRange energyBands{.lower = 0, .upper = 1};
    uint8_t minEnergy = 16;
    float sensitivity = 1.55F;
    float baselineAlpha = 0.04F;
    uint8_t onsetDelta = 8;
    uint32_t refractoryMs = 110;
    uint8_t ibiHistorySize = 8;
    uint16_t minBpm = 60;
    uint16_t maxBpm = 200;

    [[nodiscard]] bool validate() const {
        return energyBands.validate() && sensitivity >= 1.0F &&
               baselineAlpha > 0.0F && baselineAlpha <= 1.0F &&
               refractoryMs > 0 && ibiHistorySize >= 2 &&
               ibiHistorySize <= 16 && minBpm > 0 && maxBpm > minBpm;
    }
};

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

struct FftFrame {
    AudioInfo audio{};
    uint32_t sequence = 0;
    uint64_t timestampUs = 0;
    uint16_t length = 0;
    uint16_t stride = 0;
    std::array<FftBandValue, fftBandCount> bands{};
};

struct BeatEvent {
    BeatEventKind kind = BeatEventKind::Detected;
    uint32_t frameSequence = 0;
    uint64_t timestampUs = 0;
    uint8_t energy = 0;
    float bpm = 0.0F;
};

struct FftRuntimeStats {
    uint32_t copiedBytes = 0;
    uint32_t emptyCopies = 0;
    uint32_t sourceUnavailableSkips = 0;
    uint32_t frames = 0;
    uint32_t droppedFrames = 0;
    uint32_t beats = 0;
    uint32_t maxCopyUs = 0;
    uint32_t maxFrameUs = 0;
};

using FftFrameCallback = ReturnCode (*)(void *owner, const FftFrame &frame);
using BeatCallback = ReturnCode (*)(void *owner, const BeatEvent &event);

struct FftFrameHandler {
    void *owner = nullptr;
    FftFrameCallback callback = nullptr;

    [[nodiscard]] bool valid() const { return callback != nullptr; }
};

struct BeatHandler {
    void *owner = nullptr;
    BeatCallback callback = nullptr;

    [[nodiscard]] bool valid() const { return callback != nullptr; }
};

} // namespace Totem::Audio
